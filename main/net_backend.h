/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Backend-neutral network bring-up harness. The loopback example calls only
 * these two functions, so its source is byte-identical across backends:
 *   - WIZNET_TOE=1 -> wiztoe/net_backend_toe.c  (W5500 hardware TCP/IP, ioLibrary)
 *   - WIZNET_TOE=0 -> net_backend_eth.c         (esp_eth MACRAW + software LwIP)
 */
#ifndef NET_BACKEND_H
#define NET_BACKEND_H

#include <stdbool.h>

/* Bring up the chip + network (static IP). Blocks only for chip init, not link. */
void wiznet_net_init(void);

/* True once the interface is usable (link up / IP configured). */
bool wiznet_net_is_up(void);

#endif /* NET_BACKEND_H */
