/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Wi-Fi (STA) bring-up, running ALONGSIDE the W5500 Ethernet backend. Built into
 * BOTH backends (WIZNET_TOE=0 and =1). This is the "port" layer: it only brings
 * the interface up; what runs on top (the loopback echo server and its socket
 * vtable) lives in the example. In WIZNET_TOE=1 the Wi-Fi traffic reaches the
 * software LwIP via __real_lwip_* to bypass the W5500 --wrap (see the example's
 * wifi_loopback.c).
 */
#ifndef WIFI_BACKEND_H
#define WIFI_BACKEND_H

#include <stdbool.h>

/* Bring up Wi-Fi in STA mode and start connecting to `ssid`/`pass` (async). The
 * example owns the credentials (its net_config.h) and passes them in. Safe to
 * call after wiznet_net_init() — tolerates esp_netif/event-loop already being
 * initialized. The strings are copied into the driver during the call. */
void wifi_net_init(const char *ssid, const char *pass);

/* True once the STA has an IPv4 address (IP_EVENT_STA_GOT_IP). */
bool wifi_net_is_up(void);

#endif /* WIFI_BACKEND_H */
