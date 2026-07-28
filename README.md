# ESP32-S3 + W5500 examples (Ethernet **TOE** or **esp_eth**) with concurrent Wi-Fi

A set of example apps for the **WIZnet W5500** on an **ESP32-S3**, built around a
"**one source, two backends**" design: the *same* application code runs on either of two
interchangeable networking stacks, selected at build time by a single CMake switch. On top of
that, every example brings up **Wi-Fi STA at the same time** and runs a *second* server on it —
so you can exercise the W5500 and Wi-Fi paths **concurrently**.

| `WIZNET_TOE` | Backend | Where TCP/IP runs |
|:---:|---|---|
| **1** (default) | **W5500 hardwired TCP/IP (TOE)** via WIZnet `ioLibrary` hardware sockets | **inside the W5500 chip** |
| **0** | software **LwIP over `esp_eth`** (W5500 as a MACRAW SPI MAC) | on the ESP32-S3 |

> **TOE** = *TCP/IP Offload Engine.* In TOE mode the TCP/IP stack lives in the W5500's
> hardware; the ESP32-S3 only talks to hardware sockets over SPI. In `esp_eth` mode the
> W5500 is used as a plain SPI MAC and the ESP32-S3 runs the software LwIP stack.

## Examples

Each example is selectable at build time with `-DEXAMPLE=<name>` and has its own README with
wiring-independent details (ports, test steps, expected output):

| Example | What it does | README |
|---|---|---|
| **`loopback`** (default) | TCP/UDP **echo** (server / client / UDP modes) on Ethernet + Wi-Fi | [examples/loopback/README.md](examples/loopback/README.md) |
| **`tcp_server`** | **TCP server** that greets each client with a banner, then echoes | [examples/tcp_server/README.md](examples/tcp_server/README.md) |

This top-level README covers everything common to all examples: hardware, pinout, environment,
and how to build. For what a given example *does* and how to test it, open its README above.

---

## Table of contents
- [Features](#features)
- [Hardware](#hardware)
  - [W5500 ↔ ESP32-S3 pinout](#w5500--esp32-s3-pinout)
- [Development environment](#development-environment)
- [Configuration](#configuration)
- [Build / Flash / Monitor](#build--flash--monitor)
  - [Choosing the backend (`WIZNET_TOE`)](#choosing-the-backend-wiznet_toe)
  - [Choosing the example (`EXAMPLE`)](#choosing-the-example-example)
  - [Build every example at once](#build-every-example-at-once)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Features

- **Two networking backends from one app source**, chosen with `-DWIZNET_TOE=1|0`.
- **Concurrent Ethernet (W5500) + Wi-Fi STA**, each running the example as its own sibling task.
- W5500 driven over SPI (hardware TCP/IP via `ioLibrary`, or SPI-MAC via `esp_eth`).
- Static IP for Ethernet; DHCP for Wi-Fi STA.
- **Self-contained**: no `managed_components/` download — the esp_eth W5500 driver is vendored in-tree.
- App code has **zero `#if WIZNET_TOE`**: the backend/`--wrap` details live entirely in the `port/` component.

---

## Hardware

- **MCU board:** any ESP32-S3 dev board.
- **Ethernet:** a WIZnet **W5500** module (e.g. W5500 Ethernet HAT / WIZ850io / custom board).
- Common ground and **3.3 V** power to the W5500.
- An Ethernet cable to a switch/PC, and a Wi-Fi AP for the STA.

### W5500 ↔ ESP32-S3 pinout

Defined in each example's `inc/net_config.h` (same board defaults across examples). Change the
`PIN_ETH_*` macros to match your wiring.

| W5500 signal | ESP32-S3 GPIO | Notes |
|---|:---:|---|
| SCLK | **GPIO12** | SPI clock |
| MOSI | **GPIO11** | SPI master-out |
| MISO | **GPIO13** | SPI master-in |
| SCSn (CS) | **GPIO10** | Chip select (driven manually as GPIO in TOE mode) |
| RSTn | **GPIO9**  | Active-low reset (pulsed at init) |
| INT  | **GPIO14** | Link/socket interrupt — used by `esp_eth` in `WIZNET_TOE=0`; set `PIN_ETH_INT` to `-1` to fall back to polling |
| 3V3  | 3.3 V | **Do not power from 5 V** |
| GND  | GND | Common ground with the ESP32-S3 |

- **SPI host:** `SPI2_HOST`, **clock:** 20 MHz (`ETH_SPI_HOST` / `ETH_SPI_CLOCK_MHZ`).
- In **TOE mode** the CS pin is toggled manually (the SPI driver is configured with
  `spics_io_num = -1`); in **`esp_eth` mode** the CS is handled by the driver.

---

## Development environment

| Item | Version / value |
|---|---|
| **ESP-IDF** | **v6.0.2** (v6.0.x). In IDF 6.0 the W5500 SPI driver was moved out of the `esp_eth` core into the Component Registry (`espressif/w5500`). This project **vendors** that driver in-tree under [`port/ioLibrary_Driver/`](port/ioLibrary_Driver/) so it is fully self-contained (no `managed_components/` download); Espressif's Apache-2.0 license is kept alongside it. |
| **Target** | `esp32s3` (`CONFIG_IDF_TARGET="esp32s3"`) |
| **Toolchain** | `xtensa-esp32s3-elf` (installed by ESP-IDF) |
| **FreeRTOS tick** | 1000 Hz (`CONFIG_FREERTOS_HZ=1000`) — TOE polling uses 1 ms `vTaskDelay` |
| **Host OS** | Windows / Linux / macOS. Examples below use PowerShell paths; adapt as needed. |
| **Editor (optional)** | VS Code + the Espressif **ESP-IDF** extension |

Set up the ESP-IDF environment before building (or use the VS Code extension's *ESP-IDF Terminal*):

```bash
# Linux/macOS
. $HOME/esp/esp-idf/export.sh
```
```powershell
# Windows PowerShell (adjust the path to your install)
. D:\Project\ESP32\esp-idf\export.ps1
```

Then set the target once (regenerates `sdkconfig`) if it isn't already `esp32s3`:

```bash
idf.py set-target esp32s3
```

---

## Configuration

Every example owns its own `inc/net_config.h`. The **board / network** settings there are the
same across examples; the **app-specific** settings (listen ports, echo mode, …) are documented
in each example's README.

**Wi-Fi credentials** — required for the Wi-Fi side of every example:

```c
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
```

**Ethernet static IP** (the W5500 side):

```c
#define STATIC_IP        "192.168.11.2"
#define STATIC_NETMASK   "255.255.255.0"
#define STATIC_GATEWAY   "192.168.11.1"
```

Each example uses **two listen ports** — one for Ethernet, one for Wi-Fi — deliberately different,
because in `WIZNET_TOE=0` both interfaces share **one** LwIP stack and identical ports would clash
on `bind()`. The exact numbers are in the example's README.

---

## Build / Flash / Monitor

### Choosing the backend (`WIZNET_TOE`)

```bash
idf.py -DWIZNET_TOE=1 build      # TOE — W5500 hardware TCP/IP (default)
idf.py -DWIZNET_TOE=0 build      # esp_eth — software LwIP over the W5500 SPI MAC

idf.py -p PORT flash monitor     # flash + serial monitor (exit: Ctrl+])
```

Switching `WIZNET_TOE` does **not** require `idf.py fullclean`.

### Choosing the example (`EXAMPLE`)

Pick which example is built with `-DEXAMPLE=<name>` (default `loopback`). ESP32 flashes one app at
a time, so each build produces a single image:

```bash
idf.py -DEXAMPLE=loopback   build
idf.py -DEXAMPLE=tcp_server build
```

`EXAMPLE` is a CMake **cache** variable, so the value sticks in the build directory. Changing it
alters the set of built components, so **`fullclean` when switching example**:

```bash
idf.py fullclean
idf.py -DEXAMPLE=tcp_server -DWIZNET_TOE=1 build
```

> **VS Code ESP-IDF extension:** the *Build* button runs `idf.py build` and follows whatever
> `EXAMPLE`/`WIZNET_TOE` are cached in `build/` (defaults on first build). To pick a different
> example, run the `-DEXAMPLE=…` command once in the *ESP-IDF Terminal* (`fullclean` first); the
> Build / Flash / Monitor buttons then all target it.

### Build every example at once

`build_all.ps1` / `build_all.sh` build each example × backend into its own `builds/<name>_toe<n>/`
directory (so one command produces every binary — one `app_main` per image). Run it from the
**ESP-IDF terminal** or after sourcing `export.ps1`/`export.sh`; the script checks that `idf.py` is
on PATH and stops with a hint if the environment isn't active.

```bash
./build_all.ps1        # Windows PowerShell   (or:  ./build_all.sh  on Linux/macOS)
./build_all.ps1 -Toe 1                 # one backend only
./build_all.ps1 -Examples loopback     # one example only
```

Binaries land in `builds/<example>_toe<toe>/hello_world.bin`. Flash a specific one with
`idf.py -B builds/tcp_server_toe1 -p PORT flash monitor`.

---

## Project layout

Every source group keeps `.c` under `src/` and `.h` under `inc/`. Code shared by all examples
(W5500 + Wi-Fi bring-up, backend selection, the socket vtables, the `--wrap` glue) is the
top-level **`port/`** component; each example holds only its own app logic + config and depends on
`port`. The port layer hardcodes no board config — each example owns its `net_config.h` and passes
the values in (see *How it works*). Inside `port/`, the bring-up harness is in `port/backend/` and
**all W5500 chip driver-port code** is in `port/ioLibrary_Driver/` (same idea as WIZnet-PICO-C's
`port/`). The project is self-contained: the esp_eth W5500 driver is vendored in-tree (no
`managed_components/`); the *generic* ioLibrary hardware-TCP/IP driver is in `components/ioLibrary_Driver`.

```
port/                          shared component: network bring-up for all examples (no board config)
├─ CMakeLists.txt              backend (WIZNET_TOE) source selection + --wrap link glue
├─ backend/                    backend-neutral bring-up harness + shared socket vtables
│  ├─ inc/                     public API (visible to examples)
│  │  ├─ net_backend.h         Ethernet bring-up API — wiznet_net_init(cfg) + wiznet_cfg_t
│  │  ├─ wifi_backend.h        Wi-Fi STA bring-up API — wifi_net_init(ssid, pass)
│  │  └─ net_sock_ops.h        net_sock_ops_t + net_eth_ops / net_wifi_ops (dual-interface vtables)
│  └─ src/
│     ├─ wifi_backend.c        Wi-Fi STA bring-up (both backends)
│     ├─ net_sock_ops.c        net_eth_ops (plain lwip_* → W5500 via --wrap in TOE=1)
│     ├─ net_wifi_ops.c        net_wifi_ops (__real_lwip_* wrap-bypass; the only #if WIZNET_TOE)
│     ├─ net_backend_eth.c     WIZNET_TOE=0 bring-up (esp_eth W5500 MAC + software LwIP)
│     └─ net_backend_toe.c     WIZNET_TOE=1 bring-up (shadow esp_netif + static IP + chip config)
└─ ioLibrary_Driver/           pure W5500 chip driver-port code (private headers)
   ├─ LICENSE.esp_eth_w5500    Apache-2.0 for the vendored esp_eth driver
   ├─ inc/  esp_eth_mac_w5500.h · esp_eth_phy_w5500.h · w5500.h   (TOE=0, vendored esp_eth driver)
   │        wizchip_spi_esp.h · wiznet_toe.h · toe_port.h         (TOE=1, ioLibrary glue)
   └─ src/
      ├─ esp_eth_mac_w5500.c   TOE=0: vendored W5500 esp_eth MAC (was espressif/w5500)
      ├─ esp_eth_phy_w5500.c   TOE=0: vendored W5500 esp_eth PHY
      ├─ wizchip_spi_esp.c     TOE=1: ESP32 SPI/GPIO ↔ ioLibrary callbacks (reg_wizchip_*_cbfunc)
      ├─ wiznet_toe.c          TOE=1: hardware-socket layer (ported from WIZnet-PICO-LWIP-TOE-C)
      └─ wiztoe_wrap.c         TOE=1: __wrap_lwip_* glue (routes BSD sockets to the W5500)

examples/                      each subfolder is a selectable example component (-DEXAMPLE=<name>)
├─ loopback/                   TCP/UDP echo (the default -DEXAMPLE)   → examples/loopback/README.md
└─ tcp_server/                 TCP server (greets + echoes)           → examples/tcp_server/README.md

components/
└─ ioLibrary_Driver/           generic (vendored) WIZnet driver (W5500 hardware TCP/IP); TOE only
```

See [`CLAUDE.md`](CLAUDE.md) for the full architecture notes and build gotchas.

---

## How it works

Each example's engine is backend-neutral and drives its BSD socket calls through an injected
vtable (`net_sock_ops_t`). `port` provides two ready vtables — `net_eth_ops` and `net_wifi_ops` —
so the same engine runs on both interfaces, started identically (example app code has **no**
`#if WIZNET_TOE`):

```c
/* every example starts its engine on both interfaces this way */
<engine>_start("eth",  &net_eth_ops,  ETH_PORT,  ..., wiznet_net_is_up);
<engine>_start("wifi", &net_wifi_ops, WIFI_PORT, ..., wifi_net_is_up);
```

- **`net_eth_ops`** uses the standard lwIP BSD entry points (`lwip_socket`, …). In `WIZNET_TOE=1`
  these are redirected to the W5500 hardware sockets **at link time** via `-Wl,--wrap=lwip_*`
  (glue in `port/ioLibrary_Driver/src/wiztoe_wrap.c`), so the application code is unchanged. In
  `WIZNET_TOE=0` they are the software LwIP over `esp_eth`.
- **`net_wifi_ops`** always runs on the software LwIP stack. Because `WIZNET_TOE=1` hijacks the
  plain `lwip_*` symbols to the chip, it binds to the linker's **`__real_lwip_*`** (the un-wrapped
  originals) to bypass the W5500. In `WIZNET_TOE=0` there is no `--wrap`, so Wi-Fi and Ethernet
  share the one LwIP stack (hence the different ports).

Both vtables live in `port` (which owns the `--wrap`), in `net_sock_ops.c` / `net_wifi_ops.c` — so
the wrap awareness is in exactly one place and every example stays `#if`-free.

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `W5500 VERSIONR mismatch: 0x.. (expected 0x04)` | SPI wiring or power. Check MOSI/MISO/SCLK/CS/RST, common ground, 3.3 V, and lower `ETH_SPI_CLOCK_MHZ` if the wiring is long. |
| Ethernet not reachable | PC not on the `192.168.11.x` subnet, or wrong static IP/gateway in `net_config.h`. Verify with `ping 192.168.11.2`. |
| Wi-Fi never gets an IP | Wrong `WIFI_SSID`/`WIFI_PASS`, or a non-WPA2 AP. The examples assume WPA2-PSK. |
| `bind` fails on Wi-Fi in `WIZNET_TOE=0` | Port clash on the shared LwIP stack — keep the Wi-Fi and Ethernet listen ports different. |
| `idf.py` not recognized | ESP-IDF environment not active. Use the *ESP-IDF Terminal* or source `export.ps1`/`export.sh`. |
| `ninja: error: loading 'build.ninja'` | Half-configured build dir. Run `idf.py reconfigure` (or `fullclean`). |

---

## License

Application sources are provided under permissive licenses (see the SPDX headers in each
file: `CC0-1.0` / `BSD-3-Clause`). The vendored WIZnet `ioLibrary_Driver` and the vendored
esp_eth W5500 driver (`port/ioLibrary_Driver/`, Apache-2.0 — see `LICENSE.esp_eth_w5500`)
retain their own licenses.

## References

- WIZnet **ioLibrary_Driver** — W5500 hardware TCP/IP driver
- **WIZnet-PICO-LWIP-TOE-C** — the reference project this "one source, two backends" TOE
  port is based on (see `WIZnet-PICO-LWIP-TOE-C_research.md` and `plan.md`)
- **espressif/w5500** ESP-IDF component (Component Registry) — the esp_eth backend's W5500
  MAC/PHY driver, vendored in-tree here under `port/ioLibrary_Driver/`
