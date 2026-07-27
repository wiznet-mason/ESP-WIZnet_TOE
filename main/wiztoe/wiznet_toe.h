/*
 * Copyright (c) 2024 WIZnet Co.,Ltd
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * WIZnet TOE (TCP Offload Engine / hardwired TCP/IP) backend — neutral API.
 * Ported from WIZnet-PICO-LWIP-TOE-C (port/lwip/wiznet_toe.h).
 *
 * IMPORTANT: plain C types ONLY (no ioLibrary, no lwIP headers) so this can be
 * included by the __wrap_lwip_* glue without colliding with the ioLibrary
 * socket()/recv()/... names. wiznet_toe.c is the only TU that includes the
 * ioLibrary headers.
 *
 * File descriptors map 1:1 to W5500 hardware socket numbers (fd == sn, before
 * LWIP_SOCKET_OFFSET is applied by the caller).
 */
#ifndef _WIZNET_TOE_H_
#define _WIZNET_TOE_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Extra error return (besides -1): blocking call hit SO_RCVTIMEO.
 * The wrap layer maps this to errno EWOULDBLOCK. */
#define WIZTOE_ERR_TIMEOUT (-2)

/* Neutral option codes — the wrap layer maps (level, optname) to these. */
typedef enum {
    WIZTOE_OPT_KEEPALIVE,
    WIZTOE_OPT_KEEPIDLE,
    WIZTOE_OPT_NODELAY,
    WIZTOE_OPT_TTL,
    WIZTOE_OPT_TOS,
    WIZTOE_OPT_RCVTIMEO_MS,
    WIZTOE_OPT_SNDTIMEO_MS,
    WIZTOE_OPT_RCVBUF,
    WIZTOE_OPT_SNDBUF,
    WIZTOE_OPT_ERROR,
    WIZTOE_OPT_TYPE
} wiztoe_opt_t;

int  wiztoe_setsockopt(int fd, wiztoe_opt_t opt, const void *val, size_t len);
int  wiztoe_getsockopt(int fd, wiztoe_opt_t opt, void *val, size_t *len);

/* fd allocation / lifetime */
int  wiztoe_socket(int domain, int type, int protocol);   /* type 1=STREAM, 2=DGRAM */
int  wiztoe_close(int fd);

/* TCP */
int  wiztoe_bind(int fd, uint16_t port);
int  wiztoe_listen(int fd, int backlog);
int  wiztoe_accept(int fd);                                /* listener becomes the connection */
int  wiztoe_connect(int fd, const uint8_t ip[4], uint16_t port);
int  wiztoe_send(int fd, const void *buf, size_t len);
int  wiztoe_recv(int fd, void *buf, size_t len);           /* 0 = EOF */

/* UDP */
int  wiztoe_sendto(int fd, const void *buf, size_t len, const uint8_t ip[4], uint16_t port);
int  wiztoe_recvfrom(int fd, void *buf, size_t len, uint8_t ip[4], uint16_t *port);
int  wiztoe_udp_open_multicast(int fd, const uint8_t group[4], uint16_t port);

/* helpers */
int  wiztoe_is_udp(int fd);
void wiztoe_peer(int fd, uint8_t ip[4], uint16_t *port);
void wiztoe_getsockname(int fd, uint8_t ip[4], uint16_t *port);
void wiztoe_local_ip(uint8_t ip[4]);
void wiztoe_local_mac(uint8_t mac[6]);

/* raw hardware-socket reservation (for ioLibrary DHCP_run/DNS_run) */
int  wiztoe_socket_reserve(void);
void wiztoe_socket_release(int sn);

/* Configure the chip's own network identity (TOE: the CHIP owns the IP). */
void wiztoe_network_init(const uint8_t ip[4], const uint8_t mask[4],
                         const uint8_t gw[4], const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* _WIZNET_TOE_H_ */
