/**
 * @file ps4_net.h
 * @brief Network initialization and utilities
 */

#ifndef PS4SPLITSCREEN_PS4_NET_H
#define PS4SPLITSCREEN_PS4_NET_H

#include <ps4splitscreen/ps4splitscreen.h>

#ifdef __cplusplus
extern "C" {
#endif

int ps4splitscreen_net_init(void);
void ps4splitscreen_net_term(void);
bool ps4splitscreen_net_is_initialized(void);

int ps4splitscreen_get_local_ip(char* out_ip, size_t ip_size);
int ps4splitscreen_get_netmask(char* out_netmask, size_t size);
int ps4splitscreen_get_gateway(char* out_gateway, size_t size);
int ps4splitscreen_get_broadcast_ip(char* out_broadcast, size_t size);

#ifdef __cplusplus
}
#endif

#endif // PS4SPLITSCREEN_PS4_NET_H