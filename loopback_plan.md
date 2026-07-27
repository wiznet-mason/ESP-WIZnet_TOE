# W5500 Loopback — Implementation Plan (esp_eth + LwIP)

Plan to convert this ESP-IDF `hello_world` project into a **W5500 Ethernet loopback** program for
the ESP32-S3. A single build-time `#define` selects one of three modes: **TCP server**,
**TCP client**, or **UDP**.

> **Status: ✅ IMPLEMENTED & BUILDS.** All steps below are done. `idf.py build` succeeds for all
> three modes (`-DLOOPBACK_MODE=0/1/2`) with no warnings/errors. Not yet flashed to hardware
> (wiring is still placeholder — §4/§6).

Environment (verified from the repo): ESP-IDF **6.0.2**, target **esp32s3**.

> ⚠️ **IDF 6.0 reality (differs from the original draft of this plan):** the W5500 SPI-Ethernet
> driver is **no longer built into the core `esp_eth` component**. It now ships as a Component
> Registry package **`espressif/w5500`** (pulled via `main/idf_component.yml`). Consequently there
> is **no `CONFIG_ETH_SPI_ETHERNET_W5500`** Kconfig option, and **no `sdkconfig.defaults` is
> needed** (`CONFIG_ETH_USE_SPI_ETHERNET` already defaults to `y`). See §2a/§3.

---

## 1. Approach — ESP-IDF `esp_eth` W5500 driver + LwIP (Approach B) ✅

The W5500 is used as a **SPI Ethernet MAC**; the TCP/IP stack (LwIP) runs on the ESP32-S3. This is
supported by ESP-IDF's `esp_eth` framework plus the registry driver **`espressif/w5500`** —
**no third-party TCP/IP library (ioLibrary_Driver) is needed**. We use:

- `espressif/w5500` (+ `esp_eth`) — W5500 SPI MAC/PHY driver (`esp_eth_mac_new_w5500`,
  `esp_eth_phy_new_w5500`; headers `esp_eth_mac_w5500.h` / `esp_eth_phy_w5500.h`)
- `esp_netif` — binds the Ethernet driver to a network interface
- LwIP **BSD sockets** — the loopback (echo) logic is our own code using `socket/bind/recv/send`

```
   PC  <--Ethernet-->  W5500  <--SPI-->  ESP32-S3
                                          └─ esp_eth driver ─ esp_netif ─ LwIP ─ app sockets (echo)
```

The three requested modes are implemented by us as small socket echo routines, selected at compile
time by `LOOPBACK_MODE`.

---

## 2. Configuration layers — what goes where

Two distinct config layers (answers "which settings are `#define` vs `menuconfig`"):

### 2a. Driver layer — **component dependency (not a `#define`)** ✅
On IDF 6.0 the W5500 driver is a registry component, added via `main/idf_component.yml`:
```yaml
dependencies:
  espressif/w5500: "^1.0.1"
```
`idf.py reconfigure`/`build` downloads it into `managed_components/`. No `sdkconfig.defaults` is
needed — `CONFIG_ETH_USE_SPI_ETHERNET` defaults to `y`, and the old
`CONFIG_ETH_SPI_ETHERNET_W5500` option no longer exists in IDF 6.0.

### 2b. Application layer — **plain `#define` (chosen for first pass)** ✅
Loopback mode, port, static IP, MAC, and SPI pin/host assignments live as `#define`s at the top of
`W5500_loopback.c` (see §6). Simple and fast to iterate.

> Optional later: move these to `main/Kconfig.projbuild` so they appear in `menuconfig` and can be
> changed without editing source. Not done in the first pass.

---

## 3. File / structure changes

```
ESP-WIZnet_TOE/
├── CMakeLists.txt            # unchanged (project name still "hello_world" — cosmetic)
├── main/
│   ├── CMakeLists.txt        # EDIT ✅: src = W5500_loopback.c + PRIV_REQUIRES
│   ├── idf_component.yml     # NEW ✅: espressif/w5500 dependency
│   └── W5500_loopback.c      # RENAME ✅ of hello_world_main.c, rewritten
└── managed_components/       # AUTO-GENERATED ✅: espressif__w5500 (downloaded)
```
> No `components/` and no `sdkconfig.defaults` needed — the driver is a managed component (§2a).

`main/CMakeLists.txt` (edited):
```cmake
idf_component_register(SRCS "W5500_loopback.c"
                       PRIV_REQUIRES esp_eth esp_netif esp_event esp_driver_spi driver
                       INCLUDE_DIRS "")
```
> Root `CMakeLists.txt` keeps `MINIMAL_BUILD ON`; the required components come in transitively via
> `main`'s `PRIV_REQUIRES`, so no root change is needed.

---

## 4. Wiring (actual board wiring) ✅

ESP32-S3 ↔ W5500 GPIO assignments (confirmed):

| Signal   | W5500 pin | ESP32-S3 GPIO |
|----------|-----------|---------------|
| SCLK     | SCLK      | IO12 |
| MOSI     | MOSI      | IO11 |
| MISO     | MISO      | IO13 |
| CS       | SCSn      | IO10 |
| RST      | RSTn      | IO9  |
| INT      | INT       | IO14 (**interrupt mode**) |

- SPI host: `SPI2_HOST`, clock start at **20 MHz** (raise to ~33–40 MHz once stable).
- W5500 = SPI mode 0.
- INT wired to IO14 → `int_gpio_num = 14` (interrupt-driven RX; the driver's own default is also
  interrupt mode). Set `PIN_ETH_INT = -1` to fall back to timer polling.
- ⚠️ The driver enforces **exactly one** mode (XOR): interrupt mode needs `int_gpio_num >= 0` **and**
  `poll_period_ms == 0`; polling needs `int_gpio_num == -1` **and** `poll_period_ms > 0`. Setting
  both makes `esp_eth_mac_new_w5500()` return `NULL`. The source picks the right pairing via
  `#if (PIN_ETH_INT < 0)`.

---

## 5. Ethernet bring-up (esp_eth + esp_netif) ✅

> Implemented in `main/W5500_loopback.c` as `eth_init()`. The W5500 constructors come from the
> registry driver's headers `esp_eth_mac_w5500.h` / `esp_eth_phy_w5500.h` (the sketch below shows
> the generic `esp_eth_*.h` includes — the real file uses the `*_w5500.h` variants).

```c
#include "esp_eth.h"
#include "esp_eth_mac.h"
#include "esp_eth_phy.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t     *s_eth_netif  = NULL;

static void eth_init(void)
{
    // 1) TCP/IP stack + default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2) netif for Ethernet
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_eth_netif = esp_netif_new(&netif_cfg);

    // 3) SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_ETH_MOSI,
        .miso_io_num = PIN_ETH_MISO,
        .sclk_io_num = PIN_ETH_SCLK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(ETH_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .mode = 0,
        .clock_speed_hz = ETH_SPI_CLOCK_MHZ * 1000 * 1000,
        .queue_size = 20,
        .spics_io_num = PIN_ETH_CS,   // hardware CS is fine in MACRAW mode
    };

    // 4) W5500 MAC + PHY
    eth_w5500_config_t w5500_cfg = ETH_W5500_DEFAULT_CONFIG(ETH_SPI_HOST, &devcfg);
    w5500_cfg.int_gpio_num  = PIN_ETH_INT;     // -1 => polling
    w5500_cfg.poll_period_ms = 10;             // used when int_gpio_num == -1

    eth_mac_config_t mac_cfg = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_cfg, &mac_cfg);

    eth_phy_config_t phy_cfg = ETH_PHY_DEFAULT_CONFIG();
    phy_cfg.reset_gpio_num = PIN_ETH_RST;      // -1 if not wired
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_cfg);

    // 5) install driver
    esp_eth_config_t eth_cfg = ETH_DEFAULT_CONFIG(mac, phy);
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_cfg, &s_eth_handle));

    // 6) W5500 has no built-in MAC address — we must set one
    uint8_t mac_addr[6] = ETH_MAC_ADDR;
    ESP_ERROR_CHECK(esp_eth_ioctl(s_eth_handle, ETH_CMD_S_MAC_ADDR, mac_addr));

    // 7) attach driver to netif
    ESP_ERROR_CHECK(esp_netif_attach(s_eth_netif, esp_eth_new_netif_glue(s_eth_handle)));

    // 8) events + static IP (see §7)
    register_eth_events();
    set_static_ip();

    // 9) go
    ESP_ERROR_CHECK(esp_eth_start(s_eth_handle));
}
```

---

## 6. Configuration defines (top of `W5500_loopback.c`) ✅

```c
/* ---- Loopback mode (override at build time with -DLOOPBACK_MODE=n) ---- */
#define LOOPBACK_TCP_SERVER   0
#define LOOPBACK_TCP_CLIENT   1
#define LOOPBACK_UDP          2

#ifndef LOOPBACK_MODE
#define LOOPBACK_MODE         LOOPBACK_TCP_SERVER
#endif

#define LOOPBACK_PORT         5000        // TCP/UDP port (server & udp listen; client dest)
#define BUF_SIZE              2048

/* client mode target (echo server on the PC) */
#define TARGET_IP             "192.168.11.100"
#define TARGET_PORT           5000

/* ---- static network config ---- */
#define ETH_MAC_ADDR          {0x02, 0x00, 0x00, 0x12, 0x34, 0x56}  // locally-administered
#define STATIC_IP             "192.168.11.2"
#define STATIC_NETMASK        "255.255.255.0"
#define STATIC_GATEWAY        "192.168.11.1"

/* ---- SPI / GPIO wiring (actual board) ---- */
#define ETH_SPI_HOST          SPI2_HOST
#define ETH_SPI_CLOCK_MHZ     20
#define PIN_ETH_SCLK          12
#define PIN_ETH_MOSI          11
#define PIN_ETH_MISO          13
#define PIN_ETH_CS            10
#define PIN_ETH_RST           9
#define PIN_ETH_INT           14     // interrupt mode (-1 to poll)
```

Build a specific mode without editing the file:
```
idf.py fullclean
idf.py build -DCMAKE_C_FLAGS="-DLOOPBACK_MODE=1"    # 1 = TCP client, 2 = UDP
```
> ⚠️ `idf.py fullclean` **before** switching modes this way. ESP-IDF *appends* `CMAKE_C_FLAGS` to
> `build/toolchain/cflags` on each reconfigure, so building mode 1 then mode 2 without a clean
> leaves both `-DLOOPBACK_MODE=1` and `=2` on the command line → `'LOOPBACK_MODE' redefined`
> `-Werror` failure. Simplest alternative: just edit the `LOOPBACK_MODE` `#define` in the source.

---

## 7. Events + static IP ✅

> Implemented. Refinement vs. this sketch: the real code gates the loopback start on
> **link-up** (`ETHERNET_EVENT_CONNECTED` → `s_eth_connected`) rather than on a DHCP `GOT_IP`,
> since the static IP is already configured before `esp_eth_start()`.

```c
#include "esp_log.h"
#include "lwip/ip4_addr.h"

static const char *TAG = "w5500_loopback";
static volatile bool s_have_ip = false;

static void eth_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case ETHERNET_EVENT_CONNECTED:    ESP_LOGI(TAG, "Link Up");   break;
    case ETHERNET_EVENT_DISCONNECTED: ESP_LOGI(TAG, "Link Down"); s_have_ip = false; break;
    default: break;
    }
}
static void got_ip_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
    s_have_ip = true;
}

static void register_eth_events(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_handler, NULL));
}

static void set_static_ip(void)   // for the demo; swap to DHCP by skipping this
{
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_eth_netif));
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr      = esp_ip4addr_aton(STATIC_IP);
    ip.netmask.addr = esp_ip4addr_aton(STATIC_NETMASK);
    ip.gw.addr      = esp_ip4addr_aton(STATIC_GATEWAY);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_eth_netif, &ip));
    s_have_ip = true;   // static: no DHCP wait needed (still wait for link-up)
}
```

---

## 8. Loopback (echo) routines — BSD sockets ✅

Our own code (this is the part ioLibrary would otherwise have provided). Each is a small blocking
echo loop. Implemented with error handling + partial-send loops, and each routine is wrapped in an
`#if (LOOPBACK_MODE == ...)` guard so only the selected mode's routine compiles (no
unused-function warnings).

```c
#include "lwip/sockets.h"
#include <string.h>

static void loopback_tcp_server(uint8_t *buf)
{
    int lsock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(LOOPBACK_PORT),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(lsock, (struct sockaddr *)&addr, sizeof(addr));
    listen(lsock, 1);
    ESP_LOGI(TAG, "TCP server listening on %d", LOOPBACK_PORT);

    while (1) {
        struct sockaddr_in src; socklen_t sl = sizeof(src);
        int c = accept(lsock, (struct sockaddr *)&src, &sl);
        if (c < 0) continue;
        ESP_LOGI(TAG, "client connected");
        for (;;) {
            int n = recv(c, buf, BUF_SIZE, 0);
            if (n <= 0) break;
            send(c, buf, n, 0);          // echo back
        }
        close(c);
    }
}

static void loopback_tcp_client(uint8_t *buf)
{
    while (1) {
        int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in dst = { .sin_family = AF_INET, .sin_port = htons(TARGET_PORT) };
        dst.sin_addr.s_addr = esp_ip4addr_aton(TARGET_IP);
        if (connect(s, (struct sockaddr *)&dst, sizeof(dst)) == 0) {
            ESP_LOGI(TAG, "connected to %s:%d", TARGET_IP, TARGET_PORT);
            for (;;) {
                int n = recv(s, buf, BUF_SIZE, 0);   // echo whatever the peer sends
                if (n <= 0) break;
                send(s, buf, n, 0);
            }
        }
        close(s);
        vTaskDelay(pdMS_TO_TICKS(1000));            // retry
    }
}

static void loopback_udp(uint8_t *buf)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(LOOPBACK_PORT),
                                .sin_addr.s_addr = htonl(INADDR_ANY) };
    bind(s, (struct sockaddr *)&addr, sizeof(addr));
    ESP_LOGI(TAG, "UDP echo on %d", LOOPBACK_PORT);
    while (1) {
        struct sockaddr_in src; socklen_t sl = sizeof(src);
        int n = recvfrom(s, buf, BUF_SIZE, 0, (struct sockaddr *)&src, &sl);
        if (n > 0) sendto(s, buf, n, 0, (struct sockaddr *)&src, sl);  // echo back
    }
}
```

---

## 9. `app_main` flow ✅

> Implemented. The real code waits on `s_eth_connected` (link-up) instead of `s_have_ip`, and adds
> an `#else #error` guard for an invalid `LOOPBACK_MODE`.

```c
void app_main(void)
{
    static uint8_t buf[BUF_SIZE];

    eth_init();                                  // §5 + §7

    // wait for link-up (static IP means no DHCP wait, but the cable/PHY must be up)
    while (!s_have_ip) vTaskDelay(pdMS_TO_TICKS(100));

#if   (LOOPBACK_MODE == LOOPBACK_TCP_SERVER)
    loopback_tcp_server(buf);
#elif (LOOPBACK_MODE == LOOPBACK_TCP_CLIENT)
    loopback_tcp_client(buf);
#else
    loopback_udp(buf);
#endif
}
```

> `loopback_*` here are **blocking** (unlike WIZnet's non-blocking state machines), so they own the
> task. That's fine — `app_main` runs in its own FreeRTOS task. Optionally spawn them with
> `xTaskCreate` if `app_main` needs to keep doing other work.

---

## 10. Build / config checklist

1. ✅ Add `main/idf_component.yml` with `espressif/w5500` (§2a). *(No `sdkconfig.defaults` needed —
   see the IDF 6.0 note at the top.)*
2. ✅ Rename `hello_world_main.c` → `W5500_loopback.c` and implement per §5–§9.
3. ✅ `main/CMakeLists.txt`: SRC `W5500_loopback.c` + `PRIV_REQUIRES spi_flash esp_eth esp_netif
   esp_event esp_driver_spi driver` (§3).
4. ✅ `idf.py build` — succeeds for `LOOPBACK_MODE` 0, 1, and 2 (no warnings/errors). The component
   manager auto-downloads `espressif/w5500` into `managed_components/` on first build/reconfigure.
5. ⬜ (cosmetic, skipped) Root project name in `CMakeLists.txt` left as `hello_world`; output binary
   is `build/hello_world.bin`. Rename to `w5500_loopback` later if desired.

---

## 11. Test / bring-up plan ⬜ (pending real hardware/wiring)

> Firmware builds; on-device verification is deferred until the real GPIO wiring is set (§4/§6).
> Log strings in the implementation: `Ethernet link up`, `Got IP: <ip>`, and per-mode banners.

1. **Link + IP**: flash & monitor; expect `Ethernet link up` then `Got IP: 192.168.11.2`. No link → check
   wiring, `int_gpio_num`/`poll_period_ms`, SPI clock (drop to 5 MHz to bisect), MAC address set.
2. **TCP server** (default): from a PC on the same subnet — `ncat 192.168.11.2 5000`, type text,
   see it echoed.
3. **TCP client** (`-DLOOPBACK_MODE=1`): run `ncat -l 5000` on the PC (IP = `TARGET_IP` =
   192.168.11.100), device connects; whatever the PC sends is echoed back.
4. **UDP** (`-DLOOPBACK_MODE=2`): `ncat -u 192.168.11.2 5000`, datagrams echoed.
5. PC and device share `192.168.11.0/24` (per §6); set the PC to e.g. 192.168.11.100/24. Adjust
   `STATIC_*` if your LAN differs, or use a direct cable with a static PC IP.

---

## 12. Optional follow-ups (not first pass)

- **Wiring finalization**: replace placeholder GPIOs (§4/§6) with real board pins; switch to
  interrupt mode by setting `PIN_ETH_INT` to a real GPIO.
- **DHCP** instead of static IP: skip `set_static_ip()` and wait for `IP_EVENT_ETH_GOT_IP`.
- **Kconfig.projbuild**: move the §6 app defines into `menuconfig`.
- **Run modes as tasks**: `xTaskCreate` per mode if concurrent work is needed.
- **Raise SPI clock** to 33–40 MHz after basic bring-up is stable.
```
