# Backend-conditional build (WIZNET_TOE from the top-level CMakeLists).
#   TOE=1: W5500 hardwired TCP/IP via ioLibrary; BSD sockets routed by --wrap.
#   TOE=0: software LwIP over esp_eth (original behavior).
# Read WIZNET_TOE from the environment (see top-level CMakeLists) so PRIV_REQUIRES
# resolves correctly in ESP-IDF's early requirement-expansion pass.
if(DEFINED ENV{WIZNET_TOE})
    set(WIZNET_TOE $ENV{WIZNET_TOE})
endif()

# Common sources: backend-neutral loopback engine + Wi-Fi (STA) coexistence,
# compiled into BOTH backends.
set(SRCS "W5500_loopback.c" "loopback.c" "wifi_backend.c" "wifi_loopback.c")

if(WIZNET_TOE)
    list(APPEND SRCS
        "wiztoe/net_backend_toe.c"
        "wiztoe/wizchip_spi_esp.c"
        "wiztoe/wiznet_toe.c"
        "wiztoe/wiztoe_wrap.c")
    set(REQ spi_flash esp_netif esp_event esp_driver_spi esp_driver_gpio esp_timer
            lwip esp_wifi nvs_flash ioLibrary_Driver)
else()
    list(APPEND SRCS "net_backend_eth.c")
    set(REQ spi_flash esp_eth esp_netif esp_event esp_driver_spi driver
            lwip esp_wifi nvs_flash)
endif()

idf_component_register(SRCS ${SRCS}
                       PRIV_REQUIRES ${REQ}
                       INCLUDE_DIRS "." "wiztoe")

# Expose the backend selection as a C macro. wifi_loopback.c uses it to pick the
# Wi-Fi socket ops: __real_lwip_* (bypass the W5500 --wrap) in TOE=1, plain
# lwip_* in TOE=0. (W5500_loopback.c stays free of #if WIZNET_TOE.)
if(WIZNET_TOE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WIZNET_TOE=1)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WIZNET_TOE=0)
endif()

if(WIZNET_TOE)
    # W5500 chip select for the wiztoe sources (wiznet_toe.c / wizchip_spi_esp.c /
    # net_backend_toe.c all include wizchip_conf.h).
    target_compile_definitions(${COMPONENT_LIB} PRIVATE _WIZCHIP_=W5500)

    # Intercept lwIP's BSD socket entry points at link time -> wiztoe_wrap.c.
    # (The app's socket()/recv()/... are static-inline wrappers around these
    #  lwip_* symbols; close() reaches lwip_close via the socket VFS.)
    set(WRAP_FNS lwip_socket lwip_bind lwip_listen lwip_accept lwip_connect
                 lwip_send lwip_recv lwip_recvfrom lwip_sendto
                 lwip_setsockopt lwip_getsockopt lwip_getsockname lwip_close)
    foreach(fn ${WRAP_FNS})
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=${fn}")
        target_link_libraries(${COMPONENT_LIB} INTERFACE "-u __wrap_${fn}")
    endforeach()

    # wiznet_toe.c is the only main TU that includes the ioLibrary "socket.h".
    #  - -iquote: make `#include "socket.h"` resolve to ioLibrary's Ethernet/socket.h
    #    instead of lwIP's port sys/socket.h (which is on the -I path via lwip).
    #    -iquote dirs are searched before -I dirs for quote-form includes.
    #  - close=wiz_close: match the rename applied to ioLibrary's socket.c.
    set_source_files_properties("wiztoe/wiznet_toe.c" PROPERTIES
        COMPILE_DEFINITIONS "close=wiz_close"
        COMPILE_OPTIONS "-iquote;${CMAKE_SOURCE_DIR}/components/ioLibrary_Driver/Ethernet")
endif()
