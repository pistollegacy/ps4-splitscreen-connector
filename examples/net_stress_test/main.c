/**
 * @file main.c
 * @brief Network Stress Test for PS4SplitScreenConnector
 * 
 * Tests reliability layer, packet loss handling, and bandwidth.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/kernel.h>
#include <ps4/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

// -----------------------------------------------------------------------------
// Test Configuration
// -----------------------------------------------------------------------------
#define TEST_DURATION_SEC 30
#define TEST_PACKET_SIZE 1024
#define TEST_PACKETS_PER_FRAME 10
#define TARGET_FPS 60

// -----------------------------------------------------------------------------
// Test State
// -----------------------------------------------------------------------------
typedef struct {
    ps4splitscreen_context_t* ctx;
    reliability_context_t* reliability;
    bool is_host;
    bool running;
    uint64_t start_time;
    uint64_t packets_sent;
    uint64_t packets_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t last_report;
    pthread_t sender_thread;
    pthread_t receiver_thread;
} stress_test_t;

// -----------------------------------------------------------------------------
// Sender Thread
// -----------------------------------------------------------------------------
static void* sender_thread(void* arg) {
    stress_test_t* test = (stress_test_t*)arg;
    uint8_t payload[TEST_PACKET_SIZE];
    
    // Fill with pattern
    for (int i = 0; i < TEST_PACKET_SIZE; i++) {
        payload[i] = (uint8_t)(i & 0xFF);
    }
    
    uint64_t frame_start = 0;
    uint32_t frame = 0;
    
    while (test->running) {
        uint64_t now = get_time_us();
        if (frame_start == 0) frame_start = now;
        
        // Send multiple packets per frame
        for (int i = 0; i < TEST_PACKETS_PER_FRAME && test->running; i++) {
            // Use reliability layer channel 0
            int ret = ps4splitscreen_reliability_send(test->reliability, 0, payload, TEST_PACKET_SIZE, 0);
            if (ret > 0) {
                test->packets_sent++;
                test->bytes_sent += TEST_PACKET_SIZE;
            }
        }
        
        // Frame timing
        uint64_t elapsed = get_time_us() - frame_start;
        uint64_t target_frame_us = 1000000 / TARGET_FPS;
        if (elapsed < target_frame_us) {
            sceKernelUsleep(target_frame_us - elapsed);
        }
        frame_start = get_time_us();
        frame++;
        
        // Check duration
        if ((get_time_us() - test->start_time) > TEST_DURATION_SEC * 1000000) {
            test->running = false;
        }
    }
    
    return NULL;
}

// -----------------------------------------------------------------------------
// Receiver Thread
// -----------------------------------------------------------------------------
static void* receiver_thread(void* arg) {
    stress_test_t* test = (stress_test_t*)arg;
    
    while (test->running) {
        // Process reliability layer
        ps4splitscreen_reliability_update(test->reliability);
        
        // Check for received data on channel 0
        // (In real implementation, would have a callback)
        
        sceKernelUsleep(1000); // 1ms
    }
    
    return NULL;
}

// -----------------------------------------------------------------------------
// Data Callback
// -----------------------------------------------------------------------------
static void on_data_received(void* user, uint8_t channel, const uint8_t* data, size_t size) {
    stress_test_t* test = (stress_test_t*)user;
    (void)channel;
    (void)data;
    test->packets_received++;
    test->bytes_received += size;
}

// -----------------------------------------------------------------------------
// Stats Reporting
// -----------------------------------------------------------------------------
static void report_stats(stress_test_t* test) {
    uint64_t elapsed = get_time_us() - test->start_time;
    float elapsed_sec = elapsed / 1000000.0f;
    
    float pps_sent = test->packets_sent / elapsed_sec;
    float pps_recv = test->packets_received / elapsed_sec;
    float mbps_sent = (test->bytes_sent * 8.0f) / (elapsed_sec * 1000000.0f);
    float mbps_recv = (test->bytes_received * 8.0f) / (elapsed_sec * 1000000.0f);
    float loss_pct = test->packets_sent > 0 ? 
        (1.0f - (float)test->packets_received / test->packets_sent) * 100.0f : 0.0f;
    
    uint64_t sent, recv, retrans, lost;
    ps4splitscreen_reliability_get_stats(test->reliability, 0, &sent, &recv, &retrans, &lost);
    float rtt = ps4splitscreen_reliability_get_rtt(test->reliability);
    
    sceDbgPrintf("\n=== Stress Test Stats ===\n");
    sceDbgPrintf("Duration: %.1fs\n", elapsed_sec);
    sceDbgPrintf("Sent:     %llu packets (%.1f pps, %.2f Mbps)\n", test->packets_sent, pps_sent, mbps_sent);
    sceDbgPrintf("Received: %llu packets (%.1f pps, %.2f Mbps)\n", test->packets_received, pps_recv, mbps_recv);
    sceDbgPrintf("Loss:     %.2f%%\n", loss_pct);
    sceDbgPrintf("RTT:      %.2fms\n", rtt);
    sceDbgPrintf("Retrans:  %llu\n", retrans);
    sceDbgPrintf("========================\n");
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    
    sceDbgPrintf("=== PS4 SplitScreen Network Stress Test ===\n");
    
    bool is_host = true; // Parse from argv
    const char* target_ip = "192.168.1.100"; // Parse from argv
    
    stress_test_t test = {0};
    test.is_host = is_host;
    test.running = true;
    test.start_time = get_time_us();
    test.last_report = test.start_time;
    
    // Initialize framework
    ps4splitscreen_net_config_t net_cfg = ps4splitscreen_net_config_default();
    net_cfg.game_port = 9337;
    net_cfg.send_buffer_size = 512 * 1024;
    net_cfg.recv_buffer_size = 512 * 1024;
    
    ps4splitscreen_sync_config_t sync_cfg = ps4splitscreen_sync_config_default();
    sync_cfg.mode = PS4SPLITSCREEN_SYNC_LOCKSTEP; // Simpler for stress test
    
    ps4splitscreen_viewport_config_t vp_cfg = ps4splitscreen_viewport_config_default(PS4SPLITSCREEN_LAYOUT_HORIZONTAL);
    vp_cfg.render_debug_overlay = false;
    
    test.ctx = ps4splitscreen_init(&net_cfg, &sync_cfg, &vp_cfg, NULL, &test);
    if (!test.ctx) {
        sceDbgPrintf("Failed to initialize\n");
        return -1;
    }
    
    // Create reliability layer
    struct sockaddr_in remote = {0};
    remote.sin_family = SCE_NET_AF_INET;
    remote.sin_port = sceNetHtons(net_cfg.game_port);
    sceNetInetPton(SCE_NET_AF_INET, is_host ? "0.0.0.0" : target_ip, &remote.sin_addr);
    
    test.reliability = ps4splitscreen_reliability_create(
        test.ctx->game_socket, &remote, 1);
    
    if (!test.reliability) {
        sceDbgPrintf("Failed to create reliability layer\n");
        return -1;
    }
    
    // Set data callback
    ps4splitscreen_reliability_set_callback(test.reliability, 0, on_data_received, &test);
    
    // Start session
    int ret;
    if (is_host) {
        ret = ps4splitscreen_host_session(test.ctx, "StressTest", 2);
    } else {
        ret = ps4splitscreen_join_session(test.ctx, target_ip);
    }
    
    if (ret < 0) {
        sceDbgPrintf("Session failed: %s\n", ps4splitscreen_strerror(ret));
        return -1;
    }
    
    sceDbgPrintf("Session started. Running for %d seconds...\n", TEST_DURATION_SEC);
    
    // Start threads
    pthread_create(&test.sender_thread, NULL, sender_thread, &test);
    pthread_create(&test.receiver_thread, NULL, receiver_thread, &test);
    
    // Main thread - periodic reporting
    while (test.running) {
        sceKernelSleep(1);
        
        uint64_t now = get_time_us();
        if ((now - test.last_report) > 5000000) { // Every 5 seconds
            report_stats(&test);
            test.last_report = now;
        }
    }
    
    // Wait for threads
    pthread_join(test.sender_thread, NULL);
    pthread_join(test.receiver_thread, NULL);
    
    // Final report
    report_stats(&test);
    
    // Cleanup
    ps4splitscreen_reliability_destroy(test.reliability);
    ps4splitscreen_shutdown(test.ctx);
    
    sceDbgPrintf("Stress test complete.\n");
    return 0;
}

// Helper
static uint64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}