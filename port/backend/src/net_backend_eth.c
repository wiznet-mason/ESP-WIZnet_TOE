/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WIZNET_TOE=0 implementation of the backend-neutral harness (net_backend.h):
 * the W5500 is a SPI Ethernet MAC via ESP-IDF esp_eth (MACRAW), and the LwIP
 * software stack runs on the ESP32-S3. This is the project's original bring-up,
 * moved verbatim out of loopback_main.c so the example stays backend-agnostic.
 */
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_eth_mac_w5500.h"
#include "esp_eth_phy_w5500.h"
#include "driver/spi_master.h"

#include "net_backend.h"

static const char *TAG = "w5500_eth";

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t     *s_eth_netif  = NULL;
static volatile bool    s_eth_connected = false;

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet link up");
        s_eth_connected = true;
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet link down");
        s_eth_connected = false;
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

static void got_ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
}

static void set_static_ip(const wiznet_cfg_t *cfg)
{
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_eth_netif));
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr      = esp_ip4addr_aton(cfg->ip);
    ip.netmask.addr = esp_ip4addr_aton(cfg->netmask);
    ip.gw.addr      = esp_ip4addr_aton(cfg->gateway);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_eth_netif, &ip));
    ESP_LOGI(TAG, "Static IP set: %s", cfg->ip);
}

void wiznet_net_init(const wiznet_cfg_t *cfg)
{
    /* 1) TCP/IP stack + default event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 2) netif for Ethernet */
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    /* 3) SPI bus */
    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(cfg->spi_host, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .mode = 0,                                        /* W5500 = SPI mode 0 */
        .clock_speed_hz = cfg->spi_clock_mhz * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = cfg->pin_cs,
    };

    /* 4) W5500 MAC + PHY. Exactly one of interrupt/polling:
     *    int  => int_gpio_num >= 0 AND poll_period_ms == 0
     *    poll => int_gpio_num == -1 AND poll_period_ms  > 0 */
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(cfg->spi_host, &devcfg);
    w5500_cfg.int_gpio_num  = cfg->pin_int;
    w5500_cfg.poll_period_ms = (cfg->pin_int < 0) ? cfg->poll_period_ms : 0;

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = cfg->pin_rst;
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);

    /* 5) install driver */
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth_handle));

    /* 6) W5500 has no built-in MAC address, set one explicitly */
    uint8_t mac_addr[6];
    memcpy(mac_addr, cfg->mac, sizeof(mac_addr));
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));

    /* 7) attach driver to netif */
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));

    /* 8) events + static IP */
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_handler, NULL));
    set_static_ip(cfg);

    /* 9) go */
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}

bool wiznet_net_is_up(void)
{
    return s_eth_connected;
}
