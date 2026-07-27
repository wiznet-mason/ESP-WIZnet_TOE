/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * ESP32 SPI transport + bring-up for the W5500 driven by WIZnet ioLibrary
 * (hardware TCP/IP, TOE mode). Registers the ioLibrary SPI/CS/critical-section
 * callbacks over esp_driver_spi, resets the chip, and runs wizchip_init().
 */
#ifndef WIZCHIP_SPI_ESP_H
#define WIZCHIP_SPI_ESP_H

/* Initialize SPI bus + GPIO, register ioLibrary callbacks, reset + init the
 * W5500 (2 KB TX/RX per socket), and verify VERSIONR. Aborts on chip mismatch. */
void wizchip_spi_esp_init(void);

#endif /* WIZCHIP_SPI_ESP_H */
