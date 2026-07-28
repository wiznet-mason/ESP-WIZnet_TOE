/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ESP32 SPI transport + bring-up for the W5500 driven by WIZnet ioLibrary
 * (hardware TCP/IP, TOE mode). Registers the ioLibrary SPI/CS/critical-section
 * callbacks over esp_driver_spi, resets the chip, and runs wizchip_init().
 */
#ifndef WIZCHIP_SPI_ESP_H
#define WIZCHIP_SPI_ESP_H

/* SPI wiring for the W5500 (subset of wiznet_cfg_t the transport layer needs).
 * Kept as its own struct so this low-level TU does not depend on net_backend.h. */
typedef struct {
    int spi_host;          /* SPI host, e.g. SPI2_HOST */
    int spi_clock_mhz;
    int pin_sclk, pin_mosi, pin_miso, pin_cs, pin_rst;
} wizchip_spi_cfg_t;

/* Initialize SPI bus + GPIO from `cfg`, register ioLibrary callbacks, reset +
 * init the W5500 (2 KB TX/RX per socket), and verify VERSIONR. `cfg` is read
 * during the call (the CS pin is latched for the ioLibrary CS callbacks). */
void wizchip_spi_esp_init(const wizchip_spi_cfg_t *cfg);

#endif /* WIZCHIP_SPI_ESP_H */
