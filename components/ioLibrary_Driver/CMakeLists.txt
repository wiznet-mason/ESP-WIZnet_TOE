# WIZnet ioLibrary_Driver as an ESP-IDF component (W5500 hardware TCP/IP / TOE).
# Built only when WIZNET_TOE=1 (main REQUIRES it); empty otherwise so the TOE=0
# (esp_eth) build is unaffected.
#
# ioLibrary's Ethernet/socket.c defines a global `close(uint8_t)` that collides
# with newlib's POSIX close() at link (and would silently override it). Rename
# ONLY `close` -> wiz_close on socket.c (and, matching, on main's wiznet_toe.c).
# NOTE: connect()/sendto()/recvfrom() are OVERLOAD MACROS in this (W6300-variant)
# socket.h, so they must NOT be -D-renamed; and socket/send/recv/listen/disconnect
# do not collide with any other global, so they are left as-is.

# Read WIZNET_TOE from the environment (set by the top-level CMakeLists) so this
# registers correctly in ESP-IDF's early requirement-expansion pass, where
# top-level cache variables are not visible.
if(DEFINED ENV{WIZNET_TOE})
    set(WIZNET_TOE $ENV{WIZNET_TOE})
endif()

if(WIZNET_TOE)
    idf_component_register(
        SRCS "Ethernet/socket.c"
             "Ethernet/wizchip_conf.c"
             "Ethernet/W5500/w5500.c"
        INCLUDE_DIRS "Ethernet" "Ethernet/W5500")

    # W5500 chip select (PUBLIC: also reaches consumers that include wizchip_conf.h).
    target_compile_definitions(${COMPONENT_LIB} PUBLIC _WIZCHIP_=W5500)

    # Vendored third-party code: don't let its warnings fail the -Werror build.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error)

    # Only `close` needs renaming (newlib collision); socket.c defines it.
    set_source_files_properties("Ethernet/socket.c" PROPERTIES
        COMPILE_DEFINITIONS "close=wiz_close")
else()
    idf_component_register()
endif()
