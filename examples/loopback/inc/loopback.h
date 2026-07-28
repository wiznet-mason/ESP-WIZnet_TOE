/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Backend-neutral loopback (echo) engine, shared by the Ethernet (W5500) and
 * Wi-Fi paths. The socket entry points are injected via a vtable (net_sock_ops_t,
 * provided by `port`) so the exact same echo logic drives either stack — the
 * example doesn't need to know about the W5500 --wrap (that lives in `port`; use
 * net_eth_ops for Ethernet and net_wifi_ops for Wi-Fi, see net_sock_ops.h).
 *
 * The compile-time LOOPBACK_MODE (TCP server / client / UDP) selects which echo
 * routine loopback_run() dispatches to (override with -DLOOPBACK_MODE=n).
 */
#ifndef LOOPBACK_H
#define LOOPBACK_H

#include <stdint.h>
#include <stdbool.h>

#include "net_sock_ops.h"   /* net_sock_ops_t (from port) */

/*
 * Spawn a FreeRTOS task that waits until is_up() reports the interface ready,
 * then runs the LOOPBACK_MODE echo loop (server / client / UDP) forever.
 * Ethernet and Wi-Fi are started with identical calls — same level, same shape.
 *   name          - short label; also the task name and log tag (e.g. "eth", "wifi")
 *   ops           - socket vtable for this interface (net_eth_ops / net_wifi_ops)
 *   listen_port   - TCP/UDP server listen port (server & UDP modes)
 *   target_ip/port- destination (TCP client mode only)
 *   is_up         - predicate the task polls for link/IP readiness
 */
void loopback_start(const char *name, const net_sock_ops_t *ops,
                    uint16_t listen_port, const char *target_ip, uint16_t target_port,
                    bool (*is_up)(void));

#endif /* LOOPBACK_H */
