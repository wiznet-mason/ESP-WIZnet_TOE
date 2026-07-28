/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * Backend-neutral TCP echo server. Accepts one client at a time, greets it with
 * a welcome banner, then echoes back whatever it receives. BSD socket calls go
 * through a vtable (net_sock_ops_t) so the Ethernet (W5500) and Wi-Fi paths run
 * the same code — the vtable (net_eth_ops / net_wifi_ops) comes from `port`.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "tcp_server.h"
#include "net_config.h"         /* TCP_SERVER_BUF_SIZE */

static const char *TAG = "tcp_server";

/* Send the whole buffer, tolerating partial sends. Returns false on error. */
static bool send_all(const net_sock_ops_t *ops, int fd, const void *data, int len)
{
    const uint8_t *p = (const uint8_t *)data;
    int off = 0;
    while (off < len) {
        int w = ops->send(fd, p + off, len - off, 0);
        if (w < 0) {
            return false;
        }
        off += w;
    }
    return true;
}

static void tcp_server_run(const char *tag, const net_sock_ops_t *ops,
                           uint16_t port, uint8_t *buf, int buf_size)
{
    int lsock = ops->socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (lsock < 0) {
        ESP_LOGE(TAG, "[%s] socket() failed: errno %d", tag, errno);
        return;
    }
    int opt = 1;
    ops->setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (ops->bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        ops->listen(lsock, 1) < 0) {
        ESP_LOGE(TAG, "[%s] bind/listen failed: errno %d", tag, errno);
        ops->close(lsock);
        return;
    }
    ESP_LOGI(TAG, "[%s] TCP server listening on port %d", tag, port);

    while (1) {
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int c = ops->accept(lsock, (struct sockaddr *)&src, &sl);
        if (c < 0) {
            ESP_LOGE(TAG, "[%s] accept failed: errno %d", tag, errno);
            continue;
        }
        /* s_addr is network byte order, so byte 0 is the first dotted octet. */
        uint32_t ip = src.sin_addr.s_addr;
        ESP_LOGI(TAG, "[%s] client connected from %u.%u.%u.%u:%u", tag,
                 (unsigned)(ip & 0xff), (unsigned)((ip >> 8) & 0xff),
                 (unsigned)((ip >> 16) & 0xff), (unsigned)((ip >> 24) & 0xff),
                 (unsigned)ntohs(src.sin_port));

        /* Greet the client (this is what makes it a "server", not just an echo). */
        char banner[96];
        int blen = snprintf(banner, sizeof(banner),
                            "Welcome to the ESP32-S3 + W5500 TCP server [%s]\r\n", tag);
        if (!send_all(ops, c, banner, blen)) {
            ESP_LOGW(TAG, "[%s] failed to send banner: errno %d", tag, errno);
        }

        /* Echo everything the client sends until it closes the connection. */
        while (1) {
            int n = ops->recv(c, buf, buf_size, 0);
            if (n <= 0) {
                break;
            }
            ESP_LOGI(TAG, "[%s] echo %d bytes", tag, n);
            if (!send_all(ops, c, buf, n)) {
                break;
            }
        }
        ESP_LOGI(TAG, "[%s] client disconnected", tag);
        ops->close(c);
    }
}

/* --------------------------------------------------------------------------
 * Task launcher: Ethernet and Wi-Fi are both started this way (same level).
 * ------------------------------------------------------------------------ */
typedef struct {
    const char           *name;
    const net_sock_ops_t *ops;
    uint16_t              port;
    bool                (*is_up)(void);
} tcp_server_ctx_t;

static void tcp_server_task(void *arg)
{
    tcp_server_ctx_t *c = (tcp_server_ctx_t *)arg;

    uint8_t *buf = malloc(TCP_SERVER_BUF_SIZE);
    if (buf == NULL) {
        ESP_LOGE(TAG, "[%s] out of memory for %d-byte buffer", c->name, TCP_SERVER_BUF_SIZE);
        free(c);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "[%s] waiting for link...", c->name);
    while (!c->is_up()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    tcp_server_run(c->name, c->ops, c->port, buf, TCP_SERVER_BUF_SIZE);

    free(buf);       /* tcp_server_run only returns on a fatal setup error */
    free(c);
    vTaskDelete(NULL);
}

void tcp_server_start(const char *name, const net_sock_ops_t *ops,
                      uint16_t port, bool (*is_up)(void))
{
    tcp_server_ctx_t *c = malloc(sizeof(*c));
    if (c == NULL) {
        ESP_LOGE(TAG, "[%s] out of memory", name);
        return;
    }
    c->name = name;
    c->ops = ops;
    c->port = port;
    c->is_up = is_up;

    if (xTaskCreate(tcp_server_task, name, 4096, c, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "[%s] xTaskCreate failed", name);
        free(c);
    }
}
