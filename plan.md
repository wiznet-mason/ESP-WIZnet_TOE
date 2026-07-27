# Plan — W5500 TOE behind unchanged BSD sockets on ESP-IDF

Goal: bring the **"one source, two backends"** property of `WIZnet-PICO-LWIP-TOE-C` to this
ESP-IDF / ESP32-S3 project. The `loopback` example (`main/W5500_loopback.c`) must run
**byte-identically** on either backend, selected by a single build switch:

| `WIZNET_TOE` | Backend | Where TCP/IP runs |
|:---:|---|---|
| **`1`** | **W5500 hardwired TCP/IP (TOE)** | Inside the W5500 (ioLibrary hardware sockets). The app's BSD socket calls are transparently routed to the chip. |
| **`0`** *(current)* | **Software LwIP over `esp_eth`** | On the ESP32-S3; W5500 is a SPI MAC in MACRAW mode. This is today's `main/W5500_loopback.c`. |

> **Status: ✅ IMPLEMENTED & BUILDS.** `idf.py -DWIZNET_TOE=1 build` and `-DWIZNET_TOE=0 build`
> both succeed on ESP-IDF 6.0.2 / ESP32-S3. The ELF confirms the interception is linked
> (`__wrap_lwip_socket`, `wiztoe_socket`, renamed `wiz_close`). `main/W5500_loopback.c` is
> byte-identical across both backends. On-device echo test pending real hardware.

### Implementation outcome & deviations from this plan (discovered while building)
1. **ioLibrary kept as a vendored component, not a git submodule.** `components/ioLibrary_Driver/`
   already existed in the tree (plain files, W6300-variant sources). Per "don't destroy files I
   didn't create," it was kept and given a component `CMakeLists.txt` rather than replaced with a
   submodule. (Revisit if you still want it as a submodule.)
2. **`WIZNET_TOE` is bridged through an environment variable.** ESP-IDF's early
   requirement-expansion pass does NOT see top-level CMake **cache** vars, so `PRIV_REQUIRES`/
   component registration got the wrong branch. The top-level CMakeLists now does
   `set(ENV{WIZNET_TOE} …)` and each component reads `$ENV{WIZNET_TOE}`. `idf.py -DWIZNET_TOE=…`
   still works.
3. **Only `close` is renamed** (`-Dclose=wiz_close` on `socket.c` + `wiznet_toe.c`) — it is the
   sole ioLibrary global that collides with newlib's POSIX `close`. The vendored socket.h defines
   `connect`/`sendto`/`recvfrom` as **overload macros**, so `-D`-renaming them breaks the macro;
   and `socket`/`send`/`recv`/`listen`/`disconnect` don't collide with anything, so they're left.
4. **`-iquote` fixes a `socket.h` shadow.** ESP-IDF's lwIP port puts `…/lwip/port/esp32xx/include/sys`
   on the include path, and it contains a `socket.h` — so `#include "socket.h"` in `wiznet_toe.c`
   was resolving to lwIP's, not ioLibrary's. `wiznet_toe.c` is compiled with
   `-iquote <ioLibrary/Ethernet>` (searched before `-I` for quote-includes).
5. **Vendored ioLibrary built with `-Wno-error`** (it has `-Werror`-tripping warnings:
   missing-field-initializers, misleading-indentation).

See the checklist in §10 for the mapping to files. The code blocks below are the design sketch;
the actual sources match them modulo the four fixes above.

The reference design is analyzed in [WIZnet-PICO-LWIP-TOE-C_research.md](WIZnet-PICO-LWIP-TOE-C_research.md).
This plan adapts that design to ESP-IDF's very different socket/VFS integration.

---

## 1. The core problem and how ESP-IDF changes the Pico approach

On the Pico, apps call BSD sockets and the project **patches lwIP's `sockets.c`** so
`lwip_socket/bind/recv/...` early-return into a `wiztoe_*` backend (`#if WIZNET_TOE`). We *cannot*
copy that verbatim, because ESP-IDF's lwIP lives in `$IDF_PATH/components/lwip` (shared across all
projects) and exposes sockets differently. Two facts drive the design (verified in the installed
IDF 6.0.2):

1. **ESP-IDF has `LWIP_COMPAT_SOCKETS=0`** (`components/lwip/port/include/lwipopts.h:972`). The BSD
   names are instead provided as **`static inline` wrappers** in
   `components/lwip/include/lwip/sockets.h:37-70`:
   ```c
   static inline int socket(int d,int t,int p){ return lwip_socket(d,t,p); }
   static inline ssize_t recv(int s,void*m,size_t l,int f){ return lwip_recv(s,m,l,f); }
   /* bind, listen, accept, connect, send, recvfrom, sendto, setsockopt, ... */
   ```
   So every `socket()/recv()/...` call in the app compiles down to a call to the external symbol
   `lwip_socket`/`lwip_recv`/… .
2. **`close()` on a socket fd routes through the VFS.** `components/lwip/port/esp32xx/vfs_lwip.c`
   registers an fd range `[LWIP_SOCKET_OFFSET, MAX_FDS)` whose `close/read/write/fcntl/ioctl` ops
   call `lwip_close`/`lwip_read`/`lwip_write` (`vfs_lwip.c:78-141`). `LWIP_SOCKET_OFFSET =
   FD_SETSIZE - CONFIG_LWIP_MAX_SOCKETS` (`lwipopts.h:996`), and the range is registered once at
   tcpip init (`sys_arch.c:428` → `esp_vfs_lwip_sockets_register()`).

### Interception mechanism: linker `--wrap` (not a source patch)

Because the app's calls resolve to the `lwip_*` symbols, we intercept them at **link time** with
GNU ld `--wrap` — the idiomatic ESP-IDF override (used throughout IDF, e.g.
`components/lwip/CMakeLists.txt:238`, `components/heap`, `components/cxx`). For each wrapped symbol
`ld` redirects callers to `__wrap_lwip_*` and exposes the original as `__real_lwip_*`.

```
app: socket()  --(static inline)-->  lwip_socket  --(--wrap)-->  __wrap_lwip_socket  --> wiztoe_socket()
app: close(fd) --(newlib)--> VFS close_p --> lwip_close_r_wrapper --> lwip_close --(--wrap)--> __wrap_lwip_close --> wiztoe_close()
```

Key insight: `close(fd)` reaches `lwip_close` **inside `vfs_lwip.o`** (`lwip_close_r_wrapper`,
`vfs_lwip.c:78-81`). `--wrap` is a final-link flag applied to *all* objects, so that internal
reference is redirected to `__wrap_lwip_close` too — meaning we intercept the VFS close path
**without touching IDF's lwIP**. This is the ESP-IDF equivalent of the Pico's `sockets.c` patch.

Why this is clean:
- **No fork of ESP-IDF's lwIP.** Unlike the Pico's `0001_*.patch`, nothing in `$IDF_PATH` changes.
- **No change to `W5500_loopback.c`.** The app keeps calling `socket()/recv()/close()`.
- **Zero cost when `WIZNET_TOE=0`.** The `--wrap` flags and the backend are only linked in TOE
  builds; the current `esp_eth` + software-LwIP path is untouched.

---

## 2. Architecture overview

```
WIZNET_TOE=1  (new — hardwired TOE)
──────────────────────────────────
  W5500_loopback.c  (UNCHANGED: socket/bind/listen/accept/recv/send/close)
      │  static-inline -> lwip_*  ── ld --wrap ──▶  __wrap_lwip_*      main/wiztoe_wrap.c
      ▼
  wiztoe_*  (fd == W5500 hardware socket, plain-C API)               main/wiztoe/wiznet_toe.c
      │  ioLibrary socket()/listen()/recv()/send() on SOCKETn regs
      ▼
  ioLibrary_Driver (Ethernet/socket.c, W5500/w5500.c)      components/ioLibrary_Driver/
      │  SPI callbacks (esp_driver_spi)                     main/wiztoe/wizchip_spi_esp.c
      ▼
  W5500 chip  ── ARP/IP/ICMP/TCP/UDP in silicon ── Ethernet
  (esp_netif_init() still called: registers the socket VFS so close() dispatches; no esp_eth)

WIZNET_TOE=0  (current — unchanged)
──────────────────────────────────
  W5500_loopback.c  ->  lwip_* (real)  ->  netconn/SW TCP/IP  ->  esp_eth (W5500 MACRAW)  ->  Ethernet
```

---

## 3. What changes vs. what stays byte-identical

### Stays unchanged (the "one source" property)
The **echo routines** in `W5500_loopback.c` — `loopback_tcp_server/loopback_tcp_client/
loopback_udp` — use only `socket/bind/listen/accept/connect/send/recv/recvfrom/sendto/setsockopt/
close`. They compile unchanged in both modes. Nothing in them references `WIZNET_TOE`.

### Refactor required: split hardware/network bring-up out of the example
Today `W5500_loopback.c` also contains `eth_init()` (esp_eth-specific) and `set_static_ip()`. To
keep the example backend-agnostic (mirroring the Pico's `examples/common/socket_link.c` harness),
move bring-up behind a **common harness API** implemented twice:

```c
/* main/net_backend.h  (backend-neutral; included by W5500_loopback.c) */
void wiznet_net_init(void);   /* SPI+chip+IP (TOE)  OR  esp_eth+netif+IP (SW) */
bool wiznet_net_is_up(void);  /* link/IP ready */
```

`app_main()` then becomes backend-agnostic:
```c
void app_main(void) {
    static uint8_t buf[BUF_SIZE];
    wiznet_net_init();
    while (!wiznet_net_is_up()) vTaskDelay(pdMS_TO_TICKS(100));
#if   (LOOPBACK_MODE == LOOPBACK_TCP_SERVER)
    loopback_tcp_server(buf);
#elif (LOOPBACK_MODE == LOOPBACK_TCP_CLIENT)
    loopback_tcp_client(buf);
#else
    loopback_udp(buf);
#endif
}
```
> The `#if LOOPBACK_MODE` split stays (it selects TCP-server/client/UDP, orthogonal to the
> backend). There is still **no `#if WIZNET_TOE`** anywhere in the example logic.

`wiznet_net_init()` has two implementations, compiled by the switch:
- `net_backend_toe.c` (`WIZNET_TOE=1`): SPI + ioLibrary init + static IP via `ctlnetwork`, and
  `esp_netif_init()` to register the socket VFS (see §7).
- `net_backend_eth.c` (`WIZNET_TOE=0`): today's `eth_init()` + `set_static_ip()` verbatim.

---

## 4. New files & directory layout

```
ESP-WIZnet_TOE/
├── main/
│   ├── W5500_loopback.c        # EDIT: keep echo routines; move bring-up to net_backend
│   ├── net_backend.h           # NEW: backend-neutral harness API
│   ├── net_backend_eth.c       # NEW (WIZNET_TOE=0): current esp_eth bring-up (moved out)
│   ├── idf_component.yml        # EDIT: esp_eth dep only for TOE=0 (or keep; unused when TOE=1)
│   ├── CMakeLists.txt           # EDIT: conditional sources + --wrap link flags (TOE only)
│   │                            #   (Kconfig.projbuild deferred — WIZNET_TOE is a -D CMake var for now)
│   └── wiztoe/                  # NEW: the TOE backend (compiled only when WIZNET_TOE=1)
│       ├── net_backend_toe.c    #   wiznet_net_init(): SPI+chip+static IP+esp_netif_init
│       ├── wizchip_spi_esp.c/.h #   esp_driver_spi <-> ioLibrary callbacks, reset, wizchip_init
│       ├── wiznet_toe.c/.h      #   wiztoe_* hardware-socket API (fd==sn) — ported from Pico
│       └── wiztoe_wrap.c        #   __wrap_lwip_* -> wiztoe_* (neutral C; no ioLibrary headers)
└── components/
    └── ioLibrary_Driver/        # NEW: vendored WIZnet driver (submodule or copy)
        ├── CMakeLists.txt
        └── Ethernet/{socket.c,wizchip_conf.c,W5500/w5500.c}   (+ headers)
```

**Header-hygiene rule (from the Pico design):** ioLibrary's `socket.h` defines `socket()/listen()/
recv()/...` which collide with lwIP's names. Confine ioLibrary includes to `wiznet_toe.c`,
`wizchip_spi_esp.c`, `net_backend_toe.c`. **`wiztoe_wrap.c` must NOT include ioLibrary headers** —
it speaks only the neutral `wiztoe_*` API (`wiznet_toe.h`, plain C types), so it can freely use
lwIP's `LWIP_SOCKET_OFFSET`, `struct sockaddr_in`, etc.

---

## 5. Component: ioLibrary_Driver as a git submodule  *(decision: submodule)*

The W5500 hardware-TCP/IP driver is **not** in the ESP Component Registry (the `espressif/w5500`
package we use for `esp_eth` is a MACRAW MAC driver, *not* the hardware stack). Add WIZnet's
`ioLibrary_Driver` as a **git submodule** under `components/` (same source WIZnet's own Pico
project uses).

> **Prerequisite:** this project is **not currently a git repository** (per the environment). A
> submodule requires one, so first `git init` the project (and commit the existing tree), then:
> ```bash
> git submodule add https://github.com/Wiznet/ioLibrary_Driver.git components/ioLibrary_Driver
> git -C components/ioLibrary_Driver checkout <pinned-commit>   # pin a known-good revision
> ```
> Clone-time restore later: `git clone --recurse-submodules …` (or `git submodule update --init`).

The submodule ships more than we need; the component `CMakeLists.txt` selects the minimal set:
`Ethernet/socket.c`, `Ethernet/wizchip_conf.c`, `Ethernet/W5500/w5500.c` (+ headers). Set the chip
via the build define `-D_WIZCHIP_=W5500` (or in `wizchip_conf.h`).

`components/ioLibrary_Driver/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "Ethernet/socket.c" "Ethernet/wizchip_conf.c" "Ethernet/W5500/w5500.c"
    INCLUDE_DIRS "Ethernet" "Ethernet/W5500"
    REQUIRES driver esp_driver_spi esp_driver_gpio)
```

---

## 6. SPI transport glue (`wizchip_spi_esp.c`)

ioLibrary is HAL-agnostic; register callbacks that drive the W5500 over `esp_driver_spi`. This is
the ESP32 analog of the Pico's `port/ioLibrary_Driver/src/wizchip_spi.c`, and reuses the SPI wiring
already defined in `W5500_loopback.c` (SCLK 12, MOSI 11, MISO 13, CS 10, RST 9, INT 14). CS is
**manual GPIO** (held across multi-byte ioLibrary frames — hardware CS would toggle per byte).

```c
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "wizchip_conf.h"

static spi_device_handle_t s_spi;

static void cs_sel(void)   { gpio_set_level(PIN_W5500_CS, 0); }
static void cs_desel(void) { gpio_set_level(PIN_W5500_CS, 1); }

static uint8_t spi_rb(void) {
    uint8_t tx = 0xFF, rx = 0;
    spi_transaction_t t = { .length = 8, .tx_buffer = &tx, .rx_buffer = &rx };
    spi_device_polling_transmit(s_spi, &t);
    return rx;
}
static void spi_wb(uint8_t b) {
    spi_transaction_t t = { .length = 8, .tx_buffer = &b };
    spi_device_polling_transmit(s_spi, &t);
}
static void spi_rb_burst(uint8_t *buf, uint16_t len) {
    spi_transaction_t t = { .length = len*8, .rxlength = len*8, .rx_buffer = buf };
    spi_device_polling_transmit(s_spi, &t);   /* NOTE: confirm 0xFF idle fill on read */
}
static void spi_wb_burst(uint8_t *buf, uint16_t len) {
    spi_transaction_t t = { .length = len*8, .tx_buffer = buf };
    spi_device_polling_transmit(s_spi, &t);
}

/* ioLibrary access is single-threaded per our design (§8); a mutex still guards
 * against the (rare) case of two tasks touching the chip. */
static SemaphoreHandle_t s_wiz_mtx;
static void cris_en(void) { xSemaphoreTakeRecursive(s_wiz_mtx, portMAX_DELAY); }
static void cris_ex(void) { xSemaphoreGiveRecursive(s_wiz_mtx); }

void wizchip_spi_esp_init(void) {
    /* RST pulse + CS idle-high */
    gpio_set_direction(PIN_W5500_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_W5500_RST, 0); vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(PIN_W5500_RST, 1); vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_direction(PIN_W5500_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_W5500_CS, 1);

    spi_bus_config_t bus = { .mosi_io_num=PIN_W5500_MOSI, .miso_io_num=PIN_W5500_MISO,
                             .sclk_io_num=PIN_W5500_SCLK, .quadwp_io_num=-1, .quadhd_io_num=-1,
                             .max_transfer_sz = 4096 };
    ESP_ERROR_CHECK(spi_bus_initialize(W5500_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = { .clock_speed_hz = 20*1000*1000, .mode = 0,
                                          .spics_io_num = -1, .queue_size = 4 };
    ESP_ERROR_CHECK(spi_bus_add_device(W5500_SPI_HOST, &dev, &s_spi));

    s_wiz_mtx = xSemaphoreCreateRecursiveMutex();
    reg_wizchip_cris_cbfunc(cris_en, cris_ex);
    reg_wizchip_cs_cbfunc(cs_sel, cs_desel);
    reg_wizchip_spi_cbfunc(spi_rb, spi_wb);
    reg_wizchip_spiburst_cbfunc(spi_rb_burst, spi_wb_burst);

    uint8_t sizes[8] = {2,2,2,2,2,2,2,2};      /* 2 KB TX/RX per socket (W5500 = 16 KB each) */
    wizchip_init(sizes, sizes);
    /* sanity: getVERSIONR() must be 0x04 */
}
```

---

## 7. TOE network bring-up (`net_backend_toe.c`)  *(decision: keep a shadow `esp_netif`)*

Two jobs: (a) push the static IP into the chip's hardware stack via ioLibrary `ctlnetwork` (data-
plane identity), and (b) stand up lwIP + a **shadow `esp_netif`** — the ESP-IDF analog of the
Pico's `wizchip_lwip.c` shadow netif. The shadow netif:
- makes `esp_netif_init()` run, which registers the lwIP socket VFS fd-range (so `close(fd)`
  dispatches to `__wrap_lwip_close`; see §1),
- holds the same IPv4 identity the chip has, so control-plane APIs (`esp_netif_get_ip_info`, IP
  events, future DHCP) behave as apps expect,
- carries **no data** (no `esp_eth` glue attached; the W5500 terminates TCP/IP itself).

```c
#include "esp_netif.h"
#include "wizchip_conf.h"
#include "wizchip_spi_esp.h"

static esp_netif_t *s_shadow;
static bool s_net_up;

void wiznet_net_init(void)   /* WIZNET_TOE build of the harness */
{
    /* 1) TCP/IP core up -> registers the lwIP socket VFS (sys_arch.c:428). */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 2) shadow netif holding the IP identity (no driver attached; carries no data).
     *    Use the default ETH inherent config but never call esp_eth_new_netif_glue(). */
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    esp_netif_config_t cfg = { .base = &base, .driver = NULL, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH };
    s_shadow = esp_netif_new(&cfg);
    esp_netif_action_start(s_shadow, NULL, 0, NULL);   /* bring the netif up (no PHY) */
    esp_netif_dhcpc_stop(s_shadow);
    esp_netif_ip_info_t ip = {0};
    ip.ip.addr = esp_ip4addr_aton("192.168.11.2");
    ip.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    ip.gw.addr = esp_ip4addr_aton("192.168.11.1");
    esp_netif_set_ip_info(s_shadow, &ip);

    /* 3) W5500 over SPI + ioLibrary */
    wizchip_spi_esp_init();

    /* 4) mirror the same identity into the chip's hardware stack (data plane). */
    wiz_NetInfo ni = { .mac = ETH_MAC_ADDR, .ip = {192,168,11,2}, .sn = {255,255,255,0},
                       .gw = {192,168,11,1}, .dns = {8,8,8,8}, .dhcp = NETINFO_STATIC };
    ctlnetwork(CN_SET_NETINFO, (void *)&ni);
    s_net_up = true;   /* optionally gate on getPHYCFGR() link bit first */
}
bool wiznet_net_is_up(void) { return s_net_up; }
```

> The shadow-netif `esp_netif_new(...driver=NULL...)` pattern needs on-target validation — some IDF
> versions expect a driver handle before `action_start`. Fallback if it fights the API: create the
> netif and just `esp_netif_set_ip_info()` without `action_start` (the socket VFS is already
> registered by `esp_netif_init()`, which is the hard requirement for `close()`).
> **Future DHCP (Pico-style):** run the chip's ioLibrary DHCP client, then push the lease into the
> shadow netif with `esp_netif_set_ip_info()` and re-`ctlnetwork` — keeping identity in sync.

---

## 8. The `wiztoe_*` hardware-socket layer (`wiznet_toe.c/.h`)

Port the Pico's `port/lwip/wiznet_toe.c` almost verbatim — it is already plain-C and HAL-neutral.
`fd == W5500 hardware socket number`; a fixed descriptor array tracks per-socket state.

```c
/* wiznet_toe.h — neutral, plain C (NO lwIP, NO ioLibrary types) */
#define WIZTOE_MAX_SOCK   8            /* W5500; must be <= CONFIG_LWIP_MAX_SOCKETS (=10) */
#define WIZTOE_ERR_TIMEOUT (-2)

int wiztoe_socket(int domain, int type, int protocol);
int wiztoe_bind(int fd, uint16_t port);
int wiztoe_listen(int fd, int backlog);
int wiztoe_accept(int fd);
int wiztoe_connect(int fd, const uint8_t ip[4], uint16_t port);
int wiztoe_send(int fd, const void *buf, size_t len);
int wiztoe_recv(int fd, void *buf, size_t len);
int wiztoe_recvfrom(int fd, void *buf, size_t len, uint8_t ip[4], uint16_t *port);
int wiztoe_sendto(int fd, const void *buf, size_t len, const uint8_t ip[4], uint16_t port);
int wiztoe_close(int fd);
int wiztoe_is_udp(int fd);
void wiztoe_peer(int fd, uint8_t ip[4], uint16_t *port);
/* setsockopt/getsockopt via neutral enum, as in the Pico design */
```

Behavior to preserve from the reference (see research report §6):
- `accept()`: W5500 does not spawn a child socket — the **listening socket itself becomes the
  connection**; yield-poll `getSn_SR(fd)` until `SOCK_ESTABLISHED`, return the same fd; re-arm on
  aborted handshake; honor `rcv_timeout_ms` → `WIZTOE_ERR_TIMEOUT`.
- `connect()`: randomized ephemeral local port to avoid TIME_WAIT 4-tuple reuse after reset.
- `close()`: a server connection fd re-arms the listener; a client/UDP fd fully closes the slot.
- `recv()`: return 0 on EOF (peer FIN with empty RX buffer).

**ESP32 adaptation:** the Pico polls with `sleep_ms(1)` (which yields under its FreeRTOS interop).
On ESP-IDF replace the poll sleep with `vTaskDelay(pdMS_TO_TICKS(1))` so other tasks and the idle
task (watchdog) run:
```c
/* wiztoe poll idiom, ESP-IDF */
while (getSn_RX_RSR(fd) == 0) {
    if (state_left_established) return 0;                 /* EOF */
    if (timeout_expired)        return WIZTOE_ERR_TIMEOUT;
    vTaskDelay(pdMS_TO_TICKS(1));
}
```

---

## 9. The `--wrap` glue (`wiztoe_wrap.c`)

Neutral C. Maps each wrapped `lwip_*` to `wiztoe_*`, translating `struct sockaddr_in` ↔ octet
arrays exactly as the Pico's `sockets.c` patch does. **fd mapping:** `wiztoe_*` fds are `0..7`; to
land inside the VFS-routed range `[LWIP_SOCKET_OFFSET, MAX_FDS)` so `close()/read()/write()`
dispatch, add `LWIP_SOCKET_OFFSET`:

```c
#include "lwip/sockets.h"      /* LWIP_SOCKET_OFFSET, struct sockaddr_in, htons/htonl */
#include "wiznet_toe.h"        /* neutral wiztoe_* API */

/* real lwIP entry points, exposed by --wrap */
extern int  __real_lwip_socket(int, int, int);
extern int  __real_lwip_close(int);
/* ... one extern per wrapped symbol ... */

int __wrap_lwip_socket(int domain, int type, int protocol) {
    int fd = wiztoe_socket(domain, type, protocol);
    if (fd < 0) { errno = ENFILE; return -1; }
    return fd + LWIP_SOCKET_OFFSET;
}

int __wrap_lwip_bind(int s, const struct sockaddr *name, socklen_t namelen) {
    const struct sockaddr_in *sin = (const void *)name;
    return (wiztoe_bind(s - LWIP_SOCKET_OFFSET, lwip_ntohs(sin->sin_port)) < 0)
           ? (errno = EADDRINUSE, -1) : 0;
}

int __wrap_lwip_accept(int s, struct sockaddr *addr, socklen_t *addrlen) {
    int fd = wiztoe_accept(s - LWIP_SOCKET_OFFSET);
    if (fd == WIZTOE_ERR_TIMEOUT) { errno = EWOULDBLOCK; return -1; }
    if (fd < 0)                   { errno = EINVAL;      return -1; }
    /* fill *addr from wiztoe_peer() ... */
    return fd + LWIP_SOCKET_OFFSET;
}

ssize_t __wrap_lwip_recv(int s, void *m, size_t l, int f) {
    int n = wiztoe_recv(s - LWIP_SOCKET_OFFSET, m, l);
    if (n == WIZTOE_ERR_TIMEOUT) { errno = EWOULDBLOCK; return -1; }
    return (n < 0) ? (errno = EIO, -1) : n;
}

int __wrap_lwip_close(int s) {            /* also reached via VFS close(fd) */
    return (wiztoe_close(s - LWIP_SOCKET_OFFSET) < 0) ? (errno = EBADF, -1) : 0;
}
/* ... listen, connect, send, recvfrom, sendto, setsockopt, getsockopt,
       getsockname, read, write likewise ... */
```

Wrapped symbol set (must match exactly what the app + VFS reach):
`lwip_socket, lwip_bind, lwip_listen, lwip_accept, lwip_connect, lwip_send, lwip_recv,
lwip_recvfrom, lwip_sendto, lwip_setsockopt, lwip_getsockopt, lwip_getsockname, lwip_close,
lwip_read, lwip_write` (add `lwip_fcntl`/`lwip_ioctl` only if the app uses non-blocking I/O).

> `read`/`write` on socket fds route through VFS to `lwip_read`/`lwip_write`; our loopback doesn't
> use them, but wrapping them keeps the fd space consistent if future examples do.

---

## 10. Build switch & wiring — `-DWIZNET_TOE=` CMake variable  *(decision: `-D`, Kconfig later)*

`WIZNET_TOE` must drive **CMake** decisions (which sources compile, whether the `--wrap` link flags
are added), not just the C preprocessor — so it is a **CMake cache variable** set on the command
line, exactly like WIZnet's own Pico project uses a `WIZNET_TOE` CMake variable:

```bash
idf.py -DWIZNET_TOE=1 build      # 1 = hardwired TOE (default)
idf.py -DWIZNET_TOE=0 build      # 0 = software LwIP over esp_eth (current behavior)
```

> **Distinct from `LOOPBACK_MODE`.** `LOOPBACK_MODE` is a *compiler* define passed through
> `CMAKE_C_FLAGS` (which ESP-IDF *appends* across reconfigures — the fullclean gotcha in
> [loopback_plan.md](loopback_plan.md) §6). `WIZNET_TOE` is a proper **CMake cache variable**
> (`idf.py -DWIZNET_TOE=…`): it is *replaced*, not appended, so switching backends needs no
> `fullclean`. A full rebuild is still wise since it changes the linked source set.
> Kconfig migration is a later step (see §14).

Top-level `CMakeLists.txt` — default the variable and expose it to C once, project-wide:
```cmake
if(NOT DEFINED WIZNET_TOE)
    set(WIZNET_TOE 1 CACHE STRING "1=hardwired TOE, 0=software LwIP over esp_eth")
endif()
add_compile_definitions(WIZNET_TOE=${WIZNET_TOE})   # so any TU can #if WIZNET_TOE
message(STATUS "WIZNET_TOE = ${WIZNET_TOE}")
```

`main/CMakeLists.txt` — conditional sources + `--wrap` **only** in TOE builds:
```cmake
set(SRCS "W5500_loopback.c")
if(WIZNET_TOE)
    list(APPEND SRCS
        "wiztoe/net_backend_toe.c" "wiztoe/wizchip_spi_esp.c"
        "wiztoe/wiznet_toe.c"      "wiztoe/wiztoe_wrap.c")
    set(PRIV_REQ spi_flash esp_netif esp_event esp_driver_spi esp_driver_gpio lwip ioLibrary_Driver)
else()
    list(APPEND SRCS "net_backend_eth.c")
    set(PRIV_REQ spi_flash esp_eth esp_netif esp_event esp_driver_spi driver)
endif()

idf_component_register(SRCS ${SRCS} PRIV_REQUIRES ${PRIV_REQ} INCLUDE_DIRS "" "wiztoe")

if(WIZNET_TOE)
    foreach(fn lwip_socket lwip_bind lwip_listen lwip_accept lwip_connect
               lwip_send lwip_recv lwip_recvfrom lwip_sendto
               lwip_setsockopt lwip_getsockopt lwip_getsockname
               lwip_close lwip_read lwip_write)
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}")
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u __wrap_${fn}")  # force-keep wrapper
    endforeach()
endif()
```
> `-u __wrap_<fn>` forces the linker to pull each wrapper from the static lib even though nothing
> references `__wrap_*` by name — the same trick IDF uses at `components/lwip/CMakeLists.txt:239`.
> `_WIZCHIP_=W5500` is likewise added via `add_compile_definitions` (needed by ioLibrary headers).

---

## 11. fd space & `close()` routing (correctness detail)

- `CONFIG_LWIP_MAX_SOCKETS = 10`, so `LWIP_SOCKET_OFFSET = FD_SETSIZE - 10`, and the VFS routes
  `[LWIP_SOCKET_OFFSET, MAX_FDS)`.
- W5500 has 8 hardware sockets; `WIZTOE_MAX_SOCK = 8 <= 10`, so `wiztoe fd (0..7) +
  LWIP_SOCKET_OFFSET` always lands in the VFS range. Add a `_Static_assert(WIZTOE_MAX_SOCK <=
  CONFIG_LWIP_MAX_SOCKETS)`.
- Because the whole range is VFS-registered at init (not per-fd), a TOE fd we hand out is
  immediately valid for `close()/read()/write()` — those dispatch to `lwip_close_r_wrapper` etc.,
  whose `lwip_*` calls are `--wrap`-redirected to our handlers. No per-fd registration needed.
- `select()` is **not** covered (our loopback doesn't use it). If a future example needs it, the
  VFS `select` path calls `lwip_select` on real netconns and would not see TOE fds — document as a
  limitation (matches the Pico, which also has no select over TOE).

---

## 12. Concurrency model

- TOE I/O is **yield-polling** (no ISR data path), exactly like the Pico. Each blocking `wiztoe_*`
  call polls chip status/RX registers with `vTaskDelay(1 tick)` between polls.
- **One task per hardware socket** (the loopback runs a single echo task in `app_main`, so this is
  naturally satisfied). The recursive mutex in the SPI cris callbacks guards the (rare) concurrent
  chip access.
- No lwIP tcpip data path is exercised in TOE mode; the tcpip thread started by `esp_netif_init()`
  is idle.
- **Poll latency: 1 ms** *(decision)*. Set `CONFIG_FREERTOS_HZ=1000` so `vTaskDelay(pdMS_TO_TICKS(1))`
  is a true 1 ms tick (at the default 100 Hz it would round to 10 ms). Add to `sdkconfig.defaults`:
  ```
  CONFIG_FREERTOS_HZ=1000
  ```
  Then `idf.py reconfigure`. (Applies to both backends; harmless for `WIZNET_TOE=0`.)

---

## 13. Test / bring-up plan

1. **Both modes still build.** `WIZNET_TOE=0` (current esp_eth) unchanged; `WIZNET_TOE=1` links the
   wrap set + backend + ioLibrary. `idf.py fullclean build` for each (recall the `CMAKE_C_FLAGS`
   accumulation gotcha in [loopback_plan.md](loopback_plan.md) §6 / [CLAUDE.md](CLAUDE.md)).
2. **SPI/chip sanity (TOE):** after `wizchip_init`, `getVERSIONR()` must be `0x04`.
3. **Same PC-side test for both backends** (this is the whole point):
   - TCP server (default): `ncat 192.168.11.2 5000`, type text → echoed.
   - TCP client (`-DLOOPBACK_MODE=1`): `ncat -l 5000` on the PC (`192.168.11.100`) → board echoes.
   - UDP (`-DLOOPBACK_MODE=2`): `ncat -u 192.168.11.2 5000` → echoed.
   Byte-identical `W5500_loopback.c` must pass in both `WIZNET_TOE=0` and `=1`.
4. **`close()` routing check (TOE):** after a client disconnects, the server loop must re-arm and
   accept a second connection (verifies VFS→`__wrap_lwip_close`→`wiztoe_close` listener re-arm).

---

## 14. Risks, limitations & open questions

**Risks / things to verify during implementation**
- **`--wrap` reaches the VFS close path.** The plan relies on `--wrap lwip_close` redirecting the
  reference *inside* `vfs_lwip.o`. This holds because `--wrap` is a final-link transform over all
  objects, but must be verified on-target (test 4). If a future IDF calls `lwip_close` via a
  function pointer captured before wrap resolution, revisit. (Current IDF assigns
  `&lwip_close_r_wrapper`, which itself calls `lwip_close` by symbol — wrappable.)
- **Burst read idle fill.** `spi_device_polling_transmit` with `tx_buffer` shorter than `rxlength`
  — confirm the W5500 sees `0xFF` (or supply a dummy 0xFF TX buffer), same caveat as the esp_eth
  plan.
- **ioLibrary licensing/vendoring.** Copying `ioLibrary_Driver` into `components/` — confirm this
  is acceptable (BSD-like WIZnet license) and pin a known-good commit.

**Limitations (inherited from the TOE model)**
- Max 8 concurrent connections (W5500 hardware sockets).
- No `select()`/`poll()` over TOE fds; no non-blocking `O_NONBLOCK` unless `wiztoe_*` grows it.
- `esp_eth`, DHCP-client, and the software lwIP data path are dormant/absent in TOE mode.

**Resolved decisions (from review)**
1. **Config surface:** `-DWIZNET_TOE=` **CMake cache variable** now (`idf.py -DWIZNET_TOE=1`);
   migrate to Kconfig later (§10).
2. **ioLibrary:** add as a **git submodule** under `components/` (requires `git init` first, since
   the project isn't a repo yet) (§5).
3. **esp_netif:** **keep a shadow `esp_netif`** for IP identity (and future DHCP), Pico-style (§7).
4. **Poll latency:** **1 ms** — set `CONFIG_FREERTOS_HZ=1000` in `sdkconfig.defaults` (§12).

**Environment**
- ESP-IDF path: `D:\Project\ESP32\esp-idf` (IDF 6.0.2), target esp32s3.

---

## 15. Why this differs from the Pico project (summary)

| Concern | Pico (`WIZnet-PICO-LWIP-TOE-C`) | This ESP-IDF plan |
|---|---|---|
| Intercept point | **Patch** lwIP `sockets.c` (`#if WIZNET_TOE` early-return) | **Linker `--wrap`** on `lwip_*` (no IDF fork) |
| BSD names | lwIP `LWIP_COMPAT_SOCKETS`-style, patched in tree | IDF `static inline` wrappers → `lwip_*` symbols |
| `close()` | lwIP `lwip_close` (patched) | VFS → `lwip_close` → `__wrap_lwip_close` |
| Chip driver | ioLibrary_Driver (submodule) | ioLibrary_Driver (vendored component) — same |
| SPI HAL | RP2040 `hardware_spi`/PIO | ESP32 `esp_driver_spi` |
| `wiztoe_*` core | `port/lwip/wiznet_toe.c` | ported ~verbatim (sleep→vTaskDelay) |
| Backend-neutral harness | `examples/common/socket_link.c` | `net_backend.h` + `net_backend_{toe,eth}.c` |
| App source | unchanged across backends | unchanged across backends (same property) |

The end result matches the Pico's headline property — **the loopback `.c` is byte-identical in both
backends** — achieved with ESP-IDF's idiomatic link-time interception instead of a source patch.
