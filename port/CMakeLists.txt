# "port" component: W5500 + Wi-Fi network bring-up shared by ALL examples.
# The backend selection (WIZNET_TOE) and the --wrap link glue live here so the
# examples themselves stay backend-agnostic (they just call wiznet_net_init() /
# wifi_net_init() and inject socket vtables).
#   TOE=1: W5500 hardwired TCP/IP via ioLibrary; BSD sockets routed by --wrap.
#   TOE=0: software LwIP over esp_eth.
#
# Layout (mirrors WIZnet-PICO-C's port/): the generic WIZnet hardware-TCP/IP
# driver stays in components/ioLibrary_Driver; all W5500 chip driver-port code
# lives in the ioLibrary_Driver/ subfolder here:
#   TOE=1: the ioLibrary port glue (SPI/GPIO callbacks, hardware-socket TOE
#          layer, the --wrap shim).
#   TOE=0: the vendored esp_eth W5500 MAC/PHY driver (esp_eth_mac_w5500.c /
#          esp_eth_phy_w5500.c + its register header w5500.h) — moved in-tree
#          from the espressif/w5500 registry component so the project is
#          self-contained (no managed_components download).
# Sources are grouped into two subfolders, each with its own src/ + inc/:
#   backend/         backend-neutral bring-up harness (public API net_backend.h /
#                    wifi_backend.h; Wi-Fi STA + the esp_eth TOE=0 bring-up).
#   ioLibrary_Driver/ all W5500 chip driver-port code (see above), private headers.
# Board config (net_config.h) is NOT here — each example owns it and passes it in
# via wiznet_cfg_t / wifi_net_init() args, so port hardcodes no pins/IP/creds.
# Read WIZNET_TOE from the environment (see top-level CMakeLists) so PRIV_REQUIRES
# resolves correctly in ESP-IDF's early requirement-expansion pass.
if(DEFINED ENV{WIZNET_TOE})
    set(WIZNET_TOE $ENV{WIZNET_TOE})
endif()

# Common to BOTH backends: Wi-Fi STA bring-up + the shared socket vtables
# (Ethernet / Wi-Fi) that examples inject into their server engines.
set(SRCS "backend/src/wifi_backend.c"
         "backend/src/net_sock_ops.c"
         "backend/src/net_wifi_ops.c")
# Private include dirs (own sources only). The driver-port headers are added
# unconditionally so the -I path is stable; the TUs are backend-conditional.
set(PRIV_INC "ioLibrary_Driver/inc")

if(WIZNET_TOE)
    list(APPEND SRCS
        "backend/src/net_backend_toe.c"
        "ioLibrary_Driver/src/wizchip_spi_esp.c"
        "ioLibrary_Driver/src/wiznet_toe.c"
        "ioLibrary_Driver/src/wiztoe_wrap.c")
    set(REQ spi_flash esp_netif esp_event esp_driver_spi esp_driver_gpio esp_timer
            lwip esp_wifi nvs_flash ioLibrary_Driver)
else()
    # esp_eth path + the vendored W5500 MAC/PHY driver (was espressif/w5500).
    list(APPEND SRCS
        "backend/src/net_backend_eth.c"
        "ioLibrary_Driver/src/esp_eth_mac_w5500.c"
        "ioLibrary_Driver/src/esp_eth_phy_w5500.c")
    # esp_driver_gpio + esp_timer are what the vendored driver needs (matching
    # the old espressif/w5500 component's PRIV_REQUIRES).
    set(REQ spi_flash esp_eth esp_netif esp_event esp_driver_spi esp_driver_gpio
            esp_timer driver lwip esp_wifi nvs_flash)
endif()

idf_component_register(SRCS ${SRCS}
                       PRIV_REQUIRES ${REQ}
                       INCLUDE_DIRS "backend/inc"
                       PRIV_INCLUDE_DIRS ${PRIV_INC})

# net_wifi_ops.c needs the backend as a C macro: __real_lwip_* (bypass the W5500
# --wrap) in TOE=1, plain lwip_* in TOE=0. Examples no longer see this macro.
if(WIZNET_TOE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WIZNET_TOE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WIZNET_TOE=0)
endif()

if(WIZNET_TOE)
    # W5500 chip select for the wiztoe sources (they include wizchip_conf.h).
    target_compile_definitions(${COMPONENT_LIB} PRIVATE _WIZCHIP_=W5500)

    # Intercept lwIP's BSD socket entry points at link time -> wiztoe_wrap.c.
    # INTERFACE so the flags propagate to whoever links this component (the
    # selected example) and on to the final executable. (The app's socket()/
    # recv()/... are static-inline wrappers around these lwip_* symbols;
    # close() reaches lwip_close via the socket VFS.)
    set(WRAP_FNS lwip_socket lwip_bind lwip_listen lwip_accept lwip_connect
                 lwip_send lwip_recv lwip_recvfrom lwip_sendto
                 lwip_setsockopt lwip_getsockopt lwip_getsockname lwip_close)
    foreach(fn ${WRAP_FNS})
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}")
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u __wrap_${fn}")
    endforeach()

    # wiznet_toe.c is the only TU that includes the ioLibrary "socket.h".
    #  - -iquote: make `#include "socket.h"` resolve to ioLibrary's Ethernet/socket.h
    #    instead of lwIP's port sys/socket.h (which is on the -I path via lwip).
    #    -iquote dirs are searched before -I dirs for quote-form includes.
    #  - close=wiz_close: match the rename applied to ioLibrary's socket.c.
    set_source_files_properties("ioLibrary_Driver/src/wiznet_toe.c" PROPERTIES
        COMPILE_DEFINITIONS "close=wiz_close"
        COMPILE_OPTIONS "-iquote;${CMAKE_SOURCE_DIR}/components/ioLibrary_Driver/Ethernet")
endif()
