/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/*
 * W5500 Ethernet + Wi-Fi loopback (echo) demo for ESP32-S3.
 *
 * app_main brings up BOTH interfaces and starts a loopback echo server on each
 * as its OWN task, at the same level (see loopback_start):
 *   - Ethernet (W5500) on LOOPBACK_PORT       (vtable: plain lwIP, below)
 *   - Wi-Fi STA        on WIFI_LOOPBACK_PORT   (vtable: wifi_loopback_ops())
 * app_main itself just orchestrates: init both stacks, start both tasks, return.
 *
 * The echo logic lives in the backend-neutral engine loopback.c; each interface
 * supplies a socket vtable. This file uses the plain lwIP BSD entry points for
 * Ethernet — and there is deliberately NO #if WIZNET_TOE here:
 *   - WIZNET_TOE=1: these lwip_* symbols are redirected to the W5500 hardware
 *                   sockets at link time via -Wl,--wrap (see wiztoe/wiztoe_wrap.c).
 *   - WIZNET_TOE=0: software LwIP over esp_eth (W5500 as a MACRAW MAC).
 * The Wi-Fi path's --wrap awareness is isolated to wifi_loopback.c.
 *
 * LOOPBACK_MODE (0=TCP server, 1=TCP client, 2=UDP) is a compile-time switch in
 * loopback.c (override with -DLOOPBACK_MODE=n); it applies to both interfaces.
 */

#include <stdio.h>

#include "sdkconfig.h"

#include "net_backend.h"
#include "wifi_backend.h"
#include "net_config.h"
#include "loopback.h"

void app_main(void)
{
    /* Ethernet (W5500) first: it initializes esp_netif + the default event loop
     * that Wi-Fi then reuses. */
    wiznet_net_init();
    wifi_net_init();

    /* Start both echo servers as sibling tasks; each waits for its own link.
     * Same call shape — only the label, vtable, port and readiness predicate
     * differ. Ethernet uses the standard lwIP vtable; Wi-Fi uses its own
     * (__real_lwip_* in TOE=1 to bypass the W5500 --wrap). */
    loopback_start("eth",  &loopback_lwip_ops,  LOOPBACK_PORT,
                   LOOPBACK_TARGET_IP, LOOPBACK_TARGET_PORT, wiznet_net_is_up);
    loopback_start("wifi", &wifi_loopback_ops,  WIFI_LOOPBACK_PORT,
                   LOOPBACK_TARGET_IP, LOOPBACK_TARGET_PORT, wifi_net_is_up);
}
