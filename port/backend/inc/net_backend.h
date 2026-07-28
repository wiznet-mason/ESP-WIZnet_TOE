/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Backend-neutral network bring-up harness. The example calls only these
 * functions, so its app source is byte-identical across backends:
 *   - WIZNET_TOE=1 -> ioLibrary_Driver/src/net_backend_toe.c  (W5500 hardware TCP/IP)
 *   - WIZNET_TOE=0 -> net_backend_eth.c                       (esp_eth MACRAW + LwIP)
 *
 * The port layer hardcodes NO board config: the example owns its net_config.h and
 * passes wiring/IP in via wiznet_cfg_t, so `port` stays reusable across examples
 * (and boards). Same idea as WIZnet-PICO-C, where the example supplies the net
 * info to the driver.
 */
#ifndef NET_BACKEND_H
#define NET_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

/* Board + network configuration, supplied by the example (from its net_config.h). */
typedef struct {
    /* static IPv4 identity (dotted-decimal strings) */
    const char *ip;
    const char *netmask;
    const char *gateway;
    uint8_t     mac[6];
    /* W5500 SPI wiring */
    int spi_host;          /* SPI host, e.g. SPI2_HOST */
    int spi_clock_mhz;
    int pin_sclk, pin_mosi, pin_miso, pin_cs, pin_rst;
    int pin_int;           /* interrupt pin; < 0 selects polling (TOE=0 only) */
    int poll_period_ms;    /* esp_eth polling period used when pin_int < 0 (TOE=0) */
} wiznet_cfg_t;

/* Bring up the chip + network with the given config (static IP). Blocks only for
 * chip init, not link. `cfg` is read during the call; it need not outlive it. */
void wiznet_net_init(const wiznet_cfg_t *cfg);

/* True once the interface is usable (link up / IP configured). */
bool wiznet_net_is_up(void);

#endif /* NET_BACKEND_H */
