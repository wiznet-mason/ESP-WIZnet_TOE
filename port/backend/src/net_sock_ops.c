/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Ethernet (W5500) socket vtable: the plain lwIP BSD entry points. In
 * WIZNET_TOE=1 these lwip_* symbols are --wrap-redirected to the W5500 hardware
 * sockets; in WIZNET_TOE=0 they are software LwIP over esp_eth. (The Wi-Fi
 * vtable, which must bypass the --wrap, is in net_wifi_ops.c.)
 */
#include "lwip/sockets.h"

#include "net_sock_ops.h"

const net_sock_ops_t net_eth_ops = {
    .socket = lwip_socket,   .bind = lwip_bind,
    .listen = lwip_listen,   .accept = lwip_accept,
    .connect = lwip_connect, .recv = lwip_recv,
    .send = lwip_send,       .recvfrom = lwip_recvfrom,
    .sendto = lwip_sendto,   .setsockopt = lwip_setsockopt,
    .close = lwip_close,
};
