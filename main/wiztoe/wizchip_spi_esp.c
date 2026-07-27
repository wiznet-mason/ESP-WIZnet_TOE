/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ESP32 <-> W5500 SPI transport for WIZnet ioLibrary (hardware TCP/IP).
 * Also provides the neutral toe_port.h helpers (yield/time) so wiznet_toe.c
 * can stay free of FreeRTOS/POSIX headers (see toe_port.h).
 *
 * This TU includes the ESP/FreeRTOS headers AND wizchip_conf.h, but NOT the
 * ioLibrary "socket.h" — so it does not hit the socket()/close() name clash.
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#include "wizchip_conf.h"          /* reg_wizchip_*_cbfunc, wizchip_init, getVERSIONR */

#include "net_config.h"
#include "wizchip_spi_esp.h"
#include "toe_port.h"

static const char *TAG = "wiztoe_spi";

static spi_device_handle_t   s_spi;
static SemaphoreHandle_t     s_wiz_mtx;   /* guards chip access (ioLibrary cris) */

/* ---- neutral port helpers (toe_port.h) ---- */
void toe_yield_1ms(void)   { vTaskDelay(pdMS_TO_TICKS(1)); }
uint32_t toe_time_us(void) { return (uint32_t)esp_timer_get_time(); }

/* ---- ioLibrary critical section ---- */
static void cris_enter(void) { xSemaphoreTakeRecursive(s_wiz_mtx, portMAX_DELAY); }
static void cris_exit(void)  { xSemaphoreGiveRecursive(s_wiz_mtx); }

/* ---- chip select (manual GPIO, active-low) ---- */
static void cs_select(void)   { gpio_set_level(PIN_ETH_CS, 0); }
static void cs_deselect(void) { gpio_set_level(PIN_ETH_CS, 1); }

/* ---- single-byte SPI ---- */
static uint8_t spi_readbyte(void)
{
    uint8_t tx = 0xFF, rx = 0;
    spi_transaction_t t = { .length = 8, .tx_buffer = &tx, .rx_buffer = &rx };
    spi_device_polling_transmit(s_spi, &t);
    return rx;
}
static void spi_writebyte(uint8_t b)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &b };
    spi_device_polling_transmit(s_spi, &t);
}

/* ---- burst SPI (reg_wizchip_spiburst_cbfunc: void(uint8_t*, uint16_t)) ---- */
static uint8_t s_ff_fill[64];   /* small idle-fill source for read bursts */
static void spi_readburst(uint8_t *buf, uint16_t len)
{
    /* Drive MOSI with 0xFF while clocking MISO in. tx_buffer NULL sends zeros on
     * some paths, so feed an explicit 0xFF fill in chunks. */
    uint16_t done = 0;
    while (done < len) {
        uint16_t chunk = (len - done > sizeof(s_ff_fill)) ? sizeof(s_ff_fill) : (len - done);
        spi_transaction_t t = { .length = (size_t)chunk * 8, .rxlength = (size_t)chunk * 8,
                                .tx_buffer = s_ff_fill, .rx_buffer = buf + done };
        spi_device_polling_transmit(s_spi, &t);
        done += chunk;
    }
}
static void spi_writeburst(uint8_t *buf, uint16_t len)
{
    spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = buf };
    spi_device_polling_transmit(s_spi, &t);
}

void wizchip_spi_esp_init(void)
{
    memset(s_ff_fill, 0xFF, sizeof(s_ff_fill));

    /* RST pulse + CS idle-high (GPIO) */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_ETH_RST) | (1ULL << PIN_ETH_CS),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_ETH_CS, 1);
    gpio_set_level(PIN_ETH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(PIN_ETH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* SPI bus + device (manual CS -> spics_io_num = -1) */
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_ETH_MOSI,
        .miso_io_num = PIN_ETH_MISO,
        .sclk_io_num = PIN_ETH_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .mode = 0,                                      /* W5500 = SPI mode 0 */
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .spics_io_num = -1,
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(ETH_SPI_HOST, &dev, &s_spi));

    s_wiz_mtx = xSemaphoreCreateRecursiveMutex();

    /* register ioLibrary HAL callbacks */
    reg_wizchip_cris_cbfunc(cris_enter, cris_exit);
    reg_wizchip_cs_cbfunc(cs_select, cs_deselect);
    reg_wizchip_spi_cbfunc(spi_readbyte, spi_writebyte);
    reg_wizchip_spiburst_cbfunc(spi_readburst, spi_writeburst);

    /* 2 KB TX/RX per socket (W5500 has 16 KB each direction, 8 sockets) */
    uint8_t txsize[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    uint8_t rxsize[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    if (wizchip_init(txsize, rxsize) != 0) {
        ESP_LOGE(TAG, "wizchip_init failed");
        return;
    }

    uint8_t ver = getVERSIONR();
    if (ver != 0x04) {
        ESP_LOGE(TAG, "W5500 VERSIONR mismatch: 0x%02x (expected 0x04) — check wiring/SPI", ver);
    } else {
        ESP_LOGI(TAG, "W5500 detected (VERSIONR=0x04)");
    }
}
