/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Wi-Fi (STA) bring-up + Wi-Fi loopback, running ALONGSIDE the W5500 Ethernet
 * loopback. Built into BOTH backends (WIZNET_TOE=0 and =1). The Wi-Fi echo
 * server always runs on the software LwIP stack (see wifi_loopback.c) — in
 * WIZNET_TOE=1 it reaches LwIP via __real_lwip_* to bypass the W5500 --wrap.
 */
#ifndef WIFI_BACKEND_H
#define WIFI_BACKEND_H

#include <stdbool.h>

#include "loopback.h"   /* loopback_ops_t */

/* Bring up Wi-Fi in STA mode and start connecting (async). Safe to call after
 * wiznet_net_init() — tolerates esp_netif/event-loop already being initialized. */
void wifi_net_init(void);

/* True once the STA has an IPv4 address (IP_EVENT_STA_GOT_IP). */
bool wifi_net_is_up(void);

/* Wi-Fi socket vtable for loopback_start() (see wifi_loopback.c). In WIZNET_TOE=1
 * this is the __real_lwip_* set that bypasses the W5500 --wrap. */
extern const loopback_ops_t wifi_loopback_ops;

#endif /* WIFI_BACKEND_H */
