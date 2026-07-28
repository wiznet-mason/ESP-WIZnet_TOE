# loopback example — TCP/UDP echo (Ethernet + Wi-Fi)

A backend-neutral **echo** server/client that runs **concurrently** on the W5500 Ethernet
interface and Wi-Fi STA. Whatever a peer sends is echoed straight back. It's the default example
(`-DEXAMPLE=loopback`).

> For hardware, pinout, ESP-IDF setup, Wi-Fi/IP configuration, and the `WIZNET_TOE` backend
> switch, see the [top-level README](../../README.md). This page covers only what's specific to
> the loopback example.

## Echo modes

The mode is a compile-time switch, `LOOPBACK_MODE`, in
[`src/loopback.c`](src/loopback.c) — it applies to **both** interfaces:

```c
#define LOOPBACK_MODE   LOOPBACK_TCP_SERVER   /* 0 = TCP server (default) */
//      LOOPBACK_MODE   LOOPBACK_TCP_CLIENT   /* 1 = TCP client           */
//      LOOPBACK_MODE   LOOPBACK_UDP          /* 2 = UDP echo             */
```

| Mode | Behavior |
|---|---|
| `0` TCP server (default) | ESP listens; you connect and everything you send is echoed back. |
| `1` TCP client | ESP connects *out* to `LOOPBACK_TARGET_IP:LOOPBACK_TARGET_PORT` and echoes what the peer sends. |
| `2` UDP | ESP binds the port and echoes each datagram back to its sender. |

## Configuration

App settings live in [`inc/net_config.h`](inc/net_config.h) (board/Wi-Fi/IP settings are shared —
see the top-level README):

```c
#define LOOPBACK_PORT         5000              /* Ethernet (W5500) listen port    */
#define WIFI_LOOPBACK_PORT    5001              /* Wi-Fi listen port (kept != Eth) */
#define LOOPBACK_TARGET_IP    "192.168.11.100"  /* TCP-client mode destination     */
#define LOOPBACK_TARGET_PORT  5000
```

The two listen ports differ on purpose: in `WIZNET_TOE=0` both interfaces share one LwIP stack, so
identical ports would clash on `bind()`.

## Build & flash

```bash
idf.py -DEXAMPLE=loopback -DWIZNET_TOE=1 build      # TOE (default). Use =0 for esp_eth.
idf.py -p PORT flash monitor
```

To override the echo mode from the command line instead of editing the `#define`:

```bash
idf.py fullclean
idf.py -DEXAMPLE=loopback -DCMAKE_C_FLAGS="-DLOOPBACK_MODE=1" build
```

`fullclean` first, because ESP-IDF *appends* `CMAKE_C_FLAGS` across reconfigures (otherwise you get
a `LOOPBACK_MODE redefined` `-Werror`).

## Testing

The PC and the W5500 must share a subnet. For the default Ethernet IP `192.168.11.2`, give your PC
a static address on `192.168.11.x` (e.g. **192.168.11.100**), netmask `255.255.255.0`. The **Wi-Fi**
interface gets its IP from your AP via DHCP — read it from the serial monitor (`wifi: got IP …`).

`ncat` (from **nmap**) is used below; `nc`/`socat`/PuTTY work too.

**Default mode — TCP server (`LOOPBACK_MODE = 0`)** — connect and type; each line comes straight back:

```bash
ncat 192.168.11.2 5000            # Ethernet (W5500) — port 5000
ncat <wifi-ip-from-log> 5001      # Wi-Fi — port 5001, IP from the serial log
```

Open **both** at once to see the two interfaces echoing concurrently.

**TCP client mode (`LOOPBACK_MODE = 1`)** — run a listener on the PC first; the ESP connects out and
echoes what you type:

```bash
ncat -l 0.0.0.0 5000
```

**UDP mode (`LOOPBACK_MODE = 2`)**:

```bash
ncat -u 192.168.11.2 5000         # Ethernet
ncat -u <wifi-ip-from-log> 5001   # Wi-Fi
```

**Quick throughput check (optional, TCP server mode):**

```bash
head -c 1000000 /dev/urandom | ncat 192.168.11.2 5000 | wc -c
```

## Expected serial output

Approximate boot log (TOE mode, default TCP-server), abbreviated:

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
