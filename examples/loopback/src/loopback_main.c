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
 *   - Ethernet (W5500) on LOOPBACK_PORT       (ops: net_eth_ops)
 *   - Wi-Fi STA        on WIFI_LOOPBACK_PORT   (ops: net_wifi_ops)
 * app_main itself just orchestrates: init both stacks, start both tasks, return.
 *
 * The echo logic lives in the backend-neutral engine loopback.c; each interface
 * supplies a socket vtable from `port` (net_eth_ops / net_wifi_ops, see
 * net_sock_ops.h). There is deliberately NO #if WIZNET_TOE anywhere in the app:
 * the W5500 --wrap and the __real_lwip_* bypass are entirely inside `port`.
 *   - WIZNET_TOE=1: net_eth_ops' lwip_* are redirected to the W5500 hardware
 *                   sockets at link time; net_wifi_ops uses __real_lwip_*.
 *   - WIZNET_TOE=0: software LwIP over esp_eth; both ops are plain lwip_*.
 *
 * LOOPBACK_MODE (0=TCP server, 1=TCP client, 2=UDP) is a compile-time switch in
 * loopback.c (override with -DLOOPBACK_MODE=n); it applies to both interfaces.
 */

#include <stdio.h>

#include "sdkconfig.h"

#include "net_backend.h"
#include "wifi_backend.h"
#include "net_sock_ops.h"
#include "net_config.h"
#include "loopback.h"

/* Board/network config for this example (values from net_config.h). The port
 * layer hardcodes none of this — the example owns it and passes it in. */
static const wiznet_cfg_t s_wiznet_cfg = {
    .ip = STATIC_IP, .netmask = STATIC_NETMASK, .gateway = STATIC_GATEWAY,
    .mac = ETH_MAC_ADDR,
    .spi_host = ETH_SPI_HOST, .spi_clock_mhz = ETH_SPI_CLOCK_MHZ,
    .pin_sclk = PIN_ETH_SCLK, .pin_mosi = PIN_ETH_MOSI, .pin_miso = PIN_ETH_MISO,
    .pin_cs = PIN_ETH_CS, .pin_rst = PIN_ETH_RST, .pin_int = PIN_ETH_INT,
    .poll_period_ms = ETH_POLL_PERIOD_MS,
};

void app_main(void)
{
    /* Ethernet (W5500) first: it initializes esp_netif + the default event loop
     * that Wi-Fi then reuses. */
    wiznet_net_init(&s_wiznet_cfg);
    wifi_net_init(WIFI_SSID, WIFI_PASS);

    /* Start both echo servers as sibling tasks; each waits for its own link.
     * Same call shape — only the label, vtable, port and readiness predicate
     * differ. Ethernet uses net_eth_ops; Wi-Fi uses net_wifi_ops (both from
     * port; the --wrap details are hidden there). */
    loopback_start("eth",  &net_eth_ops,  LOOPBACK_PORT,
                   LOOPBACK_TARGET_IP, LOOPBACK_TARGET_PORT, wiznet_net_is_up);
    loopback_start("wifi", &net_wifi_ops, WIFI_LOOPBACK_PORT,
                   LOOPBACK_TARGET_IP, LOOPBACK_TARGET_PORT, wifi_net_is_up);
}
