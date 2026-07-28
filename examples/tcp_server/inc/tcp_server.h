/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Backend-neutral TCP echo server engine, shared by the Ethernet (W5500) and
 * Wi-Fi paths. Socket calls go through a vtable (net_sock_ops_t, provided by
 * `port`) so the same server drives either stack — the example needs no
 * knowledge of the W5500 --wrap (use net_eth_ops / net_wifi_ops, see
 * net_sock_ops.h).
 */
#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>

#include "net_sock_ops.h"   /* net_sock_ops_t (from port) */

/*
 * Spawn a FreeRTOS task that waits until is_up() reports the interface ready,
 * then runs a TCP server on `port` forever: it accepts one client at a time,
 * sends a welcome banner, then echoes back everything the client sends.
 * Ethernet and Wi-Fi are started with identical calls — same level, same shape.
 *   name  - short label; also the task name and log tag (e.g. "eth", "wifi")
 *   ops   - socket vtable for this interface (net_eth_ops / net_wifi_ops)
 *   port  - TCP listen port
 *   is_up - predicate the task polls for link/IP readiness
 */
void tcp_server_start(const char *name, const net_sock_ops_t *ops,
                      uint16_t port, bool (*is_up)(void));

#endif /* TCP_SERVER_H */
