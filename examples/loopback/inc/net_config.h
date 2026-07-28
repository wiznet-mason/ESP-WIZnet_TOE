/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Board + network configuration for this example. app_main (loopback_main.c)
 * packs these into a wiznet_cfg_t and passes it to the port layer's
 * wiznet_net_init(); the port component hardcodes none of it. Loopback-app
 * settings (ports, echo buffer, TCP-client target) are below.
 */
#ifndef NET_CONFIG_H
#define NET_CONFIG_H

#include "driver/spi_master.h"   /* SPI2_HOST (spi_host_device_t) for ETH_SPI_HOST */

/* ---- static network config ---- */
#define ETH_MAC_ADDR          {0x02, 0x00, 0x00, 0x12, 0x34, 0x56}  /* locally-administered */
#define STATIC_IP             "192.168.11.2"
#define STATIC_NETMASK        "255.255.255.0"
#define STATIC_GATEWAY        "192.168.11.1"
#define STATIC_DNS            "8.8.8.8"

/* ---- SPI / GPIO wiring (actual board) ---- */
#define ETH_SPI_HOST          SPI2_HOST
#define ETH_SPI_CLOCK_MHZ     20
#define PIN_ETH_SCLK          12
#define PIN_ETH_MOSI          11
#define PIN_ETH_MISO          13
#define PIN_ETH_CS            10
#define PIN_ETH_RST           9
#define PIN_ETH_INT           14     /* interrupt pin (used by esp_eth in TOE=0; -1 to poll) */
#define ETH_POLL_PERIOD_MS    10     /* esp_eth polling fallback (TOE=0, PIN_ETH_INT<0) */

/* ---- Wi-Fi STA config (fill in your AP credentials) ---- */
#define WIFI_SSID             "Mason_RT-AX57"//"your-ssid"
#define WIFI_PASS             "wiznet1206!"//"your-password"

/* ---- loopback application config (used by loopback_main.c + loopback.c) ---- */
#define LOOPBACK_PORT         5000              /* Ethernet (W5500) echo port */
#define WIFI_LOOPBACK_PORT    5001              /* Wi-Fi echo port — kept != Ethernet so the
                                                   shared LwIP stack in TOE=0 has no bind clash */
#define LOOPBACK_TARGET_IP    "192.168.11.100"  /* TCP-client mode destination */
#define LOOPBACK_TARGET_PORT  5000
#define LOOPBACK_BUF_SIZE     2048

#endif /* NET_CONFIG_H */
