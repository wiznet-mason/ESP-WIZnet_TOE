# ESP32-S3 + W5500 Loopback (Ethernet **TOE** or **esp_eth**) with concurrent Wi-Fi

A loopback (echo) example for the **WIZnet W5500** on an **ESP32-S3**, built around a
"**one source, two backends**" design: the *same* application code runs on either of two
interchangeable networking stacks, selected at build time by a single CMake switch.

On top of that, the demo brings up **Wi-Fi STA at the same time** and runs a *second*
echo server on it — so you can exercise the W5500 and Wi-Fi loopbacks **concurrently**.

| `WIZNET_TOE` | Backend | Where TCP/IP runs |
|:---:|---|---|
| **1** (default) | **W5500 hardwired TCP/IP (TOE)** via WIZnet `ioLibrary` hardware sockets | **inside the W5500 chip** |
| **0** | software **LwIP over `esp_eth`** (W5500 as a MACRAW SPI MAC) | on the ESP32-S3 |

> **TOE** = *TCP/IP Offload Engine.* In TOE mode the TCP/IP stack lives in the W5500's
> hardware; the ESP32-S3 only talks to hardware sockets over SPI. In `esp_eth` mode the
> W5500 is used as a plain SPI MAC and the ESP32-S3 runs the software LwIP stack.

---

## Table of contents
- [Features](#features)
- [Hardware](#hardware)
  - [W5500 ↔ ESP32-S3 pinout](#w5500--esp32-s3-pinout)
- [Development environment](#development-environment)
- [Configuration](#configuration)
- [Build / Flash / Monitor](#build--flash--monitor)
- [Testing the loopback](#testing-the-loopback)
- [Expected serial output](#expected-serial-output)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Features

- **Two networking backends from one app source**, chosen with `-DWIZNET_TOE=1|0`.
- **Concurrent Ethernet + Wi-Fi loopback**, each as its own sibling task.
- **Three echo modes** (compile-time `LOOPBACK_MODE`): TCP server, TCP client, UDP —
  applied to *both* interfaces.
- W5500 driven over SPI (hardware TCP/IP via `ioLibrary`, or SPI-MAC via `esp_eth`).
- Static IP for Ethernet; DHCP for Wi-Fi STA.

---

## Hardware

- **MCU board:** any ESP32-S3 dev board.
- **Ethernet:** a WIZnet **W5500** module (e.g. W5500 Ethernet HAT / WIZ850io / custom board).
- Common ground and **3.3 V** power to the W5500.
- An Ethernet cable to a switch/PC, and a Wi-Fi AP for the STA.

### W5500 ↔ ESP32-S3 pinout

Defined in [`main/net_config.h`](main/net_config.h). Change the `PIN_ETH_*` macros to match
your wiring.

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
| **ESP-IDF** | **v6.0.2** (v6.0.x). The W5500 SPI driver was moved out of the `esp_eth` core into the Component Registry in IDF 6.0, so it is pulled in as a managed component — see [`main/idf_component.yml`](main/idf_component.yml) (`espressif/w5500: "^1.0.1"`). |
| **Target** | `esp32s3` (`CONFIG_IDF_TARGET="esp32s3"`) |
| **Toolchain** | `xtensa-esp32s3-elf` (installed by ESP-IDF) |
| **FreeRTOS tick** | 1000 Hz (`CONFIG_FREERTOS_HZ=1000`) — TOE polling uses 1 ms `vTaskDelay` |
| **Host OS** | Windows / Linux / macOS. Examples below use PowerShell paths; adapt as needed. |
| **Editor (optional)** | VS Code + the Espressif **ESP-IDF** extension |

Set up the ESP-IDF environment before building:

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

All wiring, IP, Wi-Fi and loopback settings live in [`main/net_config.h`](main/net_config.h).

**1. Wi-Fi credentials — required for the Wi-Fi loopback:**

```c
#define WIFI_SSID   "your-ssid"
#define WIFI_PASS   "your-password"
```

**2. Ethernet static IP** (the W5500 side):

```c
#define STATIC_IP        "192.168.11.2"
#define STATIC_NETMASK   "255.255.255.0"
#define STATIC_GATEWAY   "192.168.11.1"
```

**3. Ports and TCP-client target:**

```c
#define LOOPBACK_PORT         5000              /* Ethernet (W5500) echo port      */
#define WIFI_LOOPBACK_PORT    5001              /* Wi-Fi echo port (kept != Eth)   */
#define LOOPBACK_TARGET_IP    "192.168.11.100"  /* TCP-client mode destination     */
#define LOOPBACK_TARGET_PORT  5000
```

> The Wi-Fi port differs from the Ethernet port on purpose: in `WIZNET_TOE=0` both
> interfaces share **one** LwIP stack, so identical ports would clash on `bind()`.

**4. Echo mode** — `LOOPBACK_MODE` in [`main/loopback.c`](main/loopback.c):

```c
#define LOOPBACK_MODE   LOOPBACK_TCP_SERVER   /* 0 = TCP server (default) */
//      LOOPBACK_MODE   LOOPBACK_TCP_CLIENT   /* 1 = TCP client           */
//      LOOPBACK_MODE   LOOPBACK_UDP          /* 2 = UDP echo             */
```

The selected mode applies to **both** the Ethernet and Wi-Fi loopbacks. Edit the `#define`
(simplest), or override at build time — see below.

---

## Build / Flash / Monitor

```bash
# TOE backend (W5500 hardware TCP/IP) — default
idf.py -DWIZNET_TOE=1 build

# esp_eth backend (software LwIP over the W5500 SPI MAC)
idf.py -DWIZNET_TOE=0 build

# flash + open the serial monitor (exit monitor with Ctrl+])
idf.py -p PORT flash monitor
```

- Switching `WIZNET_TOE` does **not** require `idf.py fullclean`.
- To override the echo mode from the command line instead of editing the `#define`:
  ```bash
  idf.py fullclean
  idf.py -DWIZNET_TOE=1 -DCMAKE_C_FLAGS="-DLOOPBACK_MODE=1" build
  ```
  `fullclean` first, because ESP-IDF *appends* `CMAKE_C_FLAGS` across reconfigures (otherwise
  you get a `LOOPBACK_MODE redefined` `-Werror`).

---

## Testing the loopback

The PC and the W5500 must share a subnet. For the default Ethernet IP `192.168.11.2`,
give your PC a static address on `192.168.11.x` (e.g. **192.168.11.100**), netmask
`255.255.255.0`. The **Wi-Fi** interface gets its IP from your AP via DHCP — read it from
the serial monitor (`wifi: got IP …`).

`ncat` (from **nmap**) is used below; `nc`/`socat`/PuTTY work too.

### Default mode — TCP server (`LOOPBACK_MODE = 0`)

The ESP listens; you connect and type, and every line is echoed back.

```bash
# Ethernet (W5500) — port 5000
ncat 192.168.11.2 5000

# Wi-Fi — port 5001, IP from the serial log
ncat <wifi-ip-from-log> 5001
```
Type any text and press Enter — it comes straight back. Open **both** at once to see the
two interfaces echoing concurrently.

### TCP client mode (`LOOPBACK_MODE = 1`)

The ESP connects *out* to `LOOPBACK_TARGET_IP:LOOPBACK_TARGET_PORT` and echoes whatever the
peer sends. Run a listener on the PC at that address first:

```bash
ncat -l 0.0.0.0 5000     # then type; the ESP echoes it back
```

### UDP mode (`LOOPBACK_MODE = 2`)

```bash
ncat -u 192.168.11.2 5000          # Ethernet
ncat -u <wifi-ip-from-log> 5001    # Wi-Fi
```

### Quick throughput check (optional)

```bash
# pipe 1 MB and time the echo round-trip (TCP server mode)
head -c 1000000 /dev/urandom | ncat 192.168.11.2 5000 | wc -c
```

---

## Expected serial output

Approximate log at boot (TOE mode, default TCP-server), abbreviated:

```
I (…) wiztoe_spi: W5500 detected (VERSIONR=0x04)
I (…) wiztoe_net: TOE up: 192.168.11.2 (W5500 hardware TCP/IP)
I (…) wifi: Wi-Fi STA started, connecting to "your-ssid"
I (…) loopback: [eth] waiting for link...
I (…) loopback: [wifi] waiting for link...
I (…) wifi: got IP 192.168.0.42
I (…) loopback: [eth] loopback: TCP SERVER on port 5000
I (…) loopback: [eth] TCP server listening on port 5000
I (…) loopback: [wifi] loopback: TCP SERVER on port 5001
I (…) loopback: [wifi] TCP server listening on port 5001
...
I (…) loopback: [eth] client connected
I (…) loopback: [wifi] client connected
```

- `VERSIONR=0x04` confirms the ESP32-S3 is talking to a real W5500 over SPI.
- The `[eth]` / `[wifi]` tags distinguish the two concurrent loopback tasks.

---

## Project layout

```
main/
├─ W5500_loopback.c     app_main orchestrator: inits both stacks, starts both echo tasks
├─ loopback.c / .h      backend-neutral echo engine + loopback_start() task launcher; holds LOOPBACK_MODE
├─ net_config.h         wiring, static IP, Wi-Fi creds, ports  ← edit this
├─ net_backend.h        Ethernet bring-up API (wiznet_net_init / wiznet_net_is_up)
├─ net_backend_eth.c    WIZNET_TOE=0 bring-up (esp_eth W5500 MAC + software LwIP)
├─ wifi_backend.c / .h  Wi-Fi STA bring-up + Wi-Fi socket vtable
├─ wifi_loopback.c      Wi-Fi socket vtable definition (the only #if WIZNET_TOE in the app)
├─ idf_component.yml    pulls espressif/w5500 (used by WIZNET_TOE=0)
└─ wiztoe/              WIZNET_TOE=1 only:
   ├─ wiznet_toe.c/.h   hardware-socket layer (ported from WIZnet-PICO-LWIP-TOE-C)
   ├─ wizchip_spi_esp.* ESP32 SPI ↔ ioLibrary callbacks
   ├─ net_backend_toe.c TOE bring-up (shadow esp_netif + static IP + chip config)
   └─ wiztoe_wrap.c     __wrap_lwip_* glue (routes BSD sockets to the W5500)
components/
└─ ioLibrary_Driver/    vendored WIZnet driver (W5500 hardware TCP/IP); built only in TOE mode
```

See [`CLAUDE.md`](CLAUDE.md) for the full architecture notes and build gotchas.

---

## How it works

The echo logic is a single backend-neutral engine ([`loopback.c`](main/loopback.c)) driven by
an injected socket vtable (`loopback_ops_t`). Each interface supplies its own vtable, and both
are started identically:

```c
loopback_start("eth",  &loopback_lwip_ops,  LOOPBACK_PORT,      ..., wiznet_net_is_up);
loopback_start("wifi", &wifi_loopback_ops,  WIFI_LOOPBACK_PORT, ..., wifi_net_is_up);
```

- **Ethernet** uses the standard lwIP BSD entry points (`lwip_socket`, …). In `WIZNET_TOE=1`
  these are redirected to the W5500 hardware sockets **at link time** via `-Wl,--wrap=lwip_*`
  (glue in `wiztoe/wiztoe_wrap.c`), so the application code is unchanged. In `WIZNET_TOE=0`
  they are the software LwIP over `esp_eth`.
- **Wi-Fi** always runs on the software LwIP stack. Because `WIZNET_TOE=1` hijacks the plain
  `lwip_*` symbols to the chip, the Wi-Fi vtable binds to the linker's **`__real_lwip_*`**
  (the un-wrapped originals) to bypass the W5500. In `WIZNET_TOE=0` there is no `--wrap`, so
  Wi-Fi and Ethernet share the one LwIP stack (hence the different ports).

---

## Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `W5500 VERSIONR mismatch: 0x.. (expected 0x04)` | SPI wiring or power. Check MOSI/MISO/SCLK/CS/RST, common ground, 3.3 V, and lower `ETH_SPI_CLOCK_MHZ` if the wiring is long. |
| Ethernet not reachable | PC not on the `192.168.11.x` subnet, or wrong static IP/gateway in `net_config.h`. Verify with `ping 192.168.11.2`. |
| Wi-Fi never gets an IP | Wrong `WIFI_SSID`/`WIFI_PASS`, or a non-WPA2 AP. The example assumes WPA2-PSK. |
| `bind` fails on Wi-Fi in `WIZNET_TOE=0` | Port clash on the shared LwIP stack — keep `WIFI_LOOPBACK_PORT` ≠ `LOOPBACK_PORT`. |
| `LOOPBACK_MODE redefined` error | You passed `-DCMAKE_C_FLAGS=...` without `fullclean`. Run `idf.py fullclean` first. |
| `ninja: error: loading 'build.ninja'` | Half-configured build dir. Run `idf.py reconfigure` (or `fullclean`). |

---

## License

Application sources are provided under permissive licenses (see the SPDX headers in each
file: `CC0-1.0` / `BSD-3-Clause`). The vendored WIZnet `ioLibrary_Driver` and the
`espressif/w5500` managed component retain their own licenses.

## References

- WIZnet **ioLibrary_Driver** — W5500 hardware TCP/IP driver
- **WIZnet-PICO-LWIP-TOE-C** — the reference project this "one source, two backends" TOE
  port is based on (see `WIZnet-PICO-LWIP-TOE-C_research.md` and `plan.md`)
- **espressif/w5500** ESP-IDF component (Component Registry) — used by the `esp_eth` backend
