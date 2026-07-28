# loopback example. Backend-agnostic: it only calls the "port" component's
# bring-up API and injects port's socket vtables (net_eth_ops / net_wifi_ops)
# into its own echo engine. The port component (PRIV_REQUIRES) carries the
# WIZNET_TOE source selection, the --wrap link glue, and the vtables — so this
# example has zero #if WIZNET_TOE.
idf_component_register(SRCS "src/W5500_loopback.c"
                            "src/loopback.c"
                       PRIV_REQUIRES port lwip esp_netif esp_driver_spi
                       INCLUDE_DIRS "inc")
