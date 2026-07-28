# tcp_server example — TCP server (Ethernet + Wi-Fi)

A backend-neutral **TCP server** that runs **concurrently** on the W5500 Ethernet interface and
Wi-Fi STA. On each interface it accepts one client at a time, greets it with a **welcome banner**,
then **echoes** everything the client sends (logging the client's address and byte counts). The
banner is what distinguishes it from the pure-echo [loopback](../loopback/README.md) example — the
server actively pushes data on connect.

Build it with `-DEXAMPLE=tcp_server`.

> For hardware, pinout, ESP-IDF setup, Wi-Fi/IP configuration, and the `WIZNET_TOE` backend
> switch, see the [top-level README](../../README.md). This page covers only what's specific to
> the tcp_server example.

## Behavior

On each accepted connection the server:
1. logs the client's IP/port,
2. sends `Welcome to the ESP32-S3 + W5500 TCP server [<iface>]\r\n` (`<iface>` = `eth` or `wifi`),
3. echoes back every chunk it receives until the client disconnects.

One client at a time per interface; when a client disconnects the server goes back to accepting.

## Configuration

App settings live in [`inc/net_config.h`](inc/net_config.h) (board/Wi-Fi/IP settings are shared —
see the top-level README):

```c
#define TCP_SERVER_PORT       5000    /* Ethernet (W5500) listen port    */
#define WIFI_TCP_SERVER_PORT  5001    /* Wi-Fi listen port (kept != Eth) */
#define TCP_SERVER_BUF_SIZE   2048    /* per-connection RX/echo buffer   */
```

The two listen ports differ on purpose: in `WIZNET_TOE=0` both interfaces share one LwIP stack, so
identical ports would clash on `bind()`.

## Build & flash

```bash
idf.py fullclean                                     # if switching from another example
idf.py -DEXAMPLE=tcp_server -DWIZNET_TOE=1 build      # TOE (default). Use =0 for esp_eth.
idf.py -p PORT flash monitor
```

## Testing

The PC and the W5500 must share a subnet. For the default Ethernet IP `192.168.11.2`, give your PC
a static address on `192.168.11.x` (e.g. **192.168.11.100**), netmask `255.255.255.0`. The **Wi-Fi**
interface gets its IP from your AP via DHCP — read it from the serial monitor (`wifi: got IP …`).

Connect with `ncat` (from **nmap**; `nc`/`socat`/PuTTY work too). You should see the welcome banner
immediately, then whatever you type is echoed back:

```bash
ncat 192.168.11.2 5000            # Ethernet (W5500) — port 5000
ncat <wifi-ip-from-log> 5001      # Wi-Fi — port 5001, IP from the serial log
```

Example session:

```
$ ncat 192.168.11.2 5000
Welcome to the ESP32-S3 + W5500 TCP server [eth]
hello            <- you type this
hello            <- echoed back
```

Open **both** at once to see the two interfaces serving concurrently.

## Expected serial output

Approximate boot log (TOE mode), abbreviated:

```
I (…) wiztoe_spi: W5500 detected (VERSIONR=0x04)
I (…) wiztoe_net: TOE up: 192.168.11.2 (W5500 hardware TCP/IP)
I (…) wifi: Wi-Fi STA started, connecting to "your-ssid"
I (…) tcp_server: [eth] waiting for link...
I (…) tcp_server: [wifi] waiting for link...
I (…) wifi: got IP 192.168.0.42
I (…) tcp_server: [eth] TCP server listening on port 5000
I (…) tcp_server: [wifi] TCP server listening on port 5001
...
I (…) tcp_server: [eth] client connected from 192.168.11.100:54321
I (…) tcp_server: [eth] echo 6 bytes
I (…) tcp_server: [eth] client disconnected
```

- `VERSIONR=0x04` confirms the ESP32-S3 is talking to a real W5500 over SPI.
- The `[eth]` / `[wifi]` tags distinguish the two concurrent server tasks.
