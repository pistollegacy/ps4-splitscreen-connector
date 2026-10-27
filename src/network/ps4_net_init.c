/**
 * @file ps4_net_init.c
 * @brief PS4 Network Stack Initialization
 * 
 * Initializes SceNet, SceNetCtl, and related subsystems for PS4.
 */

#include <ps4splitscreen/ps4splitscreen.h>
#include <ps4/net.h>
#include <ps4/netctl.h>
#include <ps4/sysmodule.h>
#include <ps4/error.h>
#include <stdio.h>
#include <string.h>

// -----------------------------------------------------------------------------
// Internal State
// -----------------------------------------------------------------------------
static bool g_net_initialized = false;
static int g_net_refcount = 0;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

int ps4splitscreen_net_init(void) {
    if (g_net_initialized) {
        g_net_refcount++;
        return PS4SPLITSCREEN_OK;
    }

    // Load required system modules
    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0 && ret != SCE_ERROR_SYSMODULE_ALREADY_LOADED) {
        return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    }

    ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NETCTL);
    if (ret < 0 && ret != SCE_ERROR_SYSMODULE_ALREADY_LOADED) {
        return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    }

    // Initialize net library
    SceNetInitParam init_param = {0};
    init_param.memory = malloc(1024 * 1024); // 1MB for net stack
    if (!init_param.memory) {
        return PS4SPLITSCREEN_ERR_OUT_OF_MEMORY;
    }
    init_param.size = 1024 * 1024;
    init_param.flags = 0;

    ret = sceNetInit(&init_param);
    if (ret < 0) {
        free(init_param.memory);
        return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    }

    // Initialize netctl
    ret = sceNetCtlInit();
    if (ret < 0) {
        sceNetTerm();
        free(init_param.memory);
        return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    }

    g_net_initialized = true;
    g_net_refcount = 1;
    return PS4SPLITSCREEN_OK;
}

void ps4splitscreen_net_term(void) {
    if (!g_net_initialized) return;

    g_net_refcount--;
    if (g_net_refcount > 0) return;

    sceNetCtlTerm();
    sceNetTerm();
    g_net_initialized = false;
}

bool ps4splitscreen_net_is_initialized(void) {
    return g_net_initialized;
}

// -----------------------------------------------------------------------------
// Network Interface Utilities
// -----------------------------------------------------------------------------

int ps4splitscreen_get_local_ip(char* out_ip, size_t ip_size) {
    if (!g_net_initialized) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    if (!out_ip || ip_size < 16) return PS4SPLITSCREEN_ERR_INVALID_PARAM;

    SceNetCtlInfo info;
    int ret = sceNetCtlInetGetInfo(SCE_NET_CTL_INFO_GET_IP_ADDRESS, &info);
    if (ret < 0) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;

    strncpy(out_ip, info.ip_address, ip_size - 1);
    out_ip[ip_size - 1] = '\0';
    return PS4SPLITSCREEN_OK;
}

int ps4splitscreen_get_netmask(char* out_netmask, size_t size) {
    if (!g_net_initialized) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    
    SceNetCtlInfo info;
    int ret = sceNetCtlInetGetInfo(SCE_NET_CTL_INFO_GET_NETMASK, &info);
    if (ret < 0) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;

    strncpy(out_netmask, info.netmask, size - 1);
    out_netmask[size - 1] = '\0';
    return PS4SPLITSCREEN_OK;
}

int ps4splitscreen_get_gateway(char* out_gateway, size_t size) {
    if (!g_net_initialized) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;
    
    SceNetCtlInfo info;
    int ret = sceNetCtlInetGetInfo(SCE_NET_CTL_INFO_GET_GATEWAY, &info);
    if (ret < 0) return PS4SPLITSCREEN_ERR_NET_INIT_FAILED;

    strncpy(out_gateway, info.gateway, size - 1);
    out_gateway[size - 1] = '\0';
    return PS4SPLITSCREEN_OK;
}

// -----------------------------------------------------------------------------
// Broadcast Address Calculation
// -----------------------------------------------------------------------------

int ps4splitscreen_get_broadcast_ip(char* out_broadcast, size_t size) {
    char ip[16], netmask[16];
    int ret = ps4splitscreen_get_local_ip(ip, sizeof(ip));
    if (ret < 0) return ret;
    
    ret = ps4splitscreen_get_netmask(netmask, sizeof(netmask));
    if (ret < 0) return ret;

    // Parse IP and netmask to calculate broadcast
    uint32_t ip_addr = 0, mask = 0;
    sscanf(ip, "%hhu.%hhu.%hhu.%hhu", 
           (unsigned char*)&ip_addr + 3, (unsigned char*)&ip_addr + 2,
           (unsigned char*)&ip_addr + 1, (unsigned char*)&ip_addr + 0);
    sscanf(netmask, "%hhu.%hhu.%hhu.%hhu",
           (unsigned char*)&mask + 3, (unsigned char*)&mask + 2,
           (unsigned char*)&mask + 1, (unsigned char*)&mask + 0);

    uint32_t broadcast = ip_addr | ~mask;
    
    snprintf(out_broadcast, size, "%u.%u.%u.%u",
             (broadcast >> 24) & 0xFF,
             (broadcast >> 16) & 0xFF,
             (broadcast >> 8) & 0xFF,
             broadcast & 0xFF);
    
    return PS4SPLITSCREEN_OK;
}