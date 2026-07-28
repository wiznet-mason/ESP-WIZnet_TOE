/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * WIZNET_TOE=1 implementation of the backend-neutral harness (net_backend.h).
 *
 * Brings up: (a) lwIP core + a SHADOW esp_netif that holds the IPv4 identity —
 * esp_netif_init() also registers the lwIP socket VFS fd-range, which is what
 * makes close()/read()/write() on our TOE fds dispatch to __wrap_lwip_* (see
 * wiztoe_wrap.c); (b) the W5500 over SPI via ioLibrary; (c) the same static IP
 * mirrored into the chip's hardware TCP/IP stack. No esp_eth; no data through
 * lwIP.
 *
 * Does NOT include the ioLibrary "socket.h" (only the neutral wiztoe_* API), so
 * no socket()/close() name clash here.
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "net_backend.h"
#include "wizchip_spi_esp.h"
#include "wiznet_toe.h"

static const char *TAG = "wiztoe_net";
static esp_netif_t *s_shadow;
static bool s_net_up;

static void parse4(const char *s, uint8_t out[4])
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d);
    out[0] = (uint8_t)a; out[1] = (uint8_t)b; out[2] = (uint8_t)c; out[3] = (uint8_t)d;
}

void wiznet_net_init(const wiznet_cfg_t *cfg)
{
    /* 1) lwIP core up -> registers the socket VFS fd-range (so close(fd) works). */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 2) shadow netif holding the IPv4 identity (no driver attached; no data). */
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t netif_cfg = {
        .base = &base,
        .driver = NULL,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
    };
    s_shadow = esp_netif_new(&netif_cfg);
    if (s_shadow) {
        esp_netif_dhcpc_stop(s_shadow);
        esp_netif_ip_info_t ip = {0};
        esp_netif_str_to_ip4(cfg->ip, &ip.ip);
        esp_netif_str_to_ip4(cfg->netmask, &ip.netmask);
        esp_netif_str_to_ip4(cfg->gateway, &ip.gw);
        esp_netif_set_ip_info(s_shadow, &ip);
    }

    /* 3) W5500 over SPI + ioLibrary. */
    wizchip_spi_cfg_t spicfg = {
        .spi_host = cfg->spi_host, .spi_clock_mhz = cfg->spi_clock_mhz,
        .pin_sclk = cfg->pin_sclk, .pin_mosi = cfg->pin_mosi, .pin_miso = cfg->pin_miso,
        .pin_cs = cfg->pin_cs, .pin_rst = cfg->pin_rst,
    };
    wizchip_spi_esp_init(&spicfg);

    /* 4) mirror the identity into the chip's hardware TCP/IP stack. */
    uint8_t mac[6];
    memcpy(mac, cfg->mac, sizeof(mac));
    uint8_t ip[4], mask[4], gw[4];
    parse4(cfg->ip, ip);
    parse4(cfg->netmask, mask);
    parse4(cfg->gateway, gw);
    wiztoe_network_init(ip, mask, gw, mac);

    ESP_LOGI(TAG, "TOE up: %s (W5500 hardware TCP/IP)", cfg->ip);
    s_net_up = true;
}

bool wiznet_net_is_up(void)
{
    return s_net_up;
}
