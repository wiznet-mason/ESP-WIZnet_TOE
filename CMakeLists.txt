# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Networking backend switch (idf.py -DWIZNET_TOE=1|0). It drives CMake source
# selection + the --wrap link flags, so it is a CMake cache variable (not a
# CMAKE_C_FLAGS -D). Default 1 = W5500 hardwired TCP/IP (TOE); 0 = software LwIP
# over esp_eth. Kconfig migration is a later step (see plan.md).
if(NOT DEFINED WIZNET_TOE)
    set(WIZNET_TOE 1 CACHE STRING "1 = hardwired TCP/IP (TOE), 0 = software LwIP over esp_eth")
endif()
message(STATUS "WIZNET_TOE = ${WIZNET_TOE}")
# Bridge WIZNET_TOE through the environment so it is visible in EVERY CMake
# context, including ESP-IDF's early requirement-expansion pass (where top-level
# cache variables are NOT available). Component CMakeLists read $ENV{WIZNET_TOE}.
set(ENV{WIZNET_TOE} "${WIZNET_TOE}")

# Component layout:
#   port/            shared "port" component (W5500 + Wi-Fi bring-up), used by all examples
#   examples/<name>/ one component per example; the chosen one provides app_main
# port/ has its own CMakeLists.txt, so listing it in EXTRA_COMPONENT_DIRS registers
# the directory itself as a component; examples/ has none, so its subdirectories are
# scanned as components instead.
set(EXTRA_COMPONENT_DIRS "examples" "port")
if(NOT DEFINED EXAMPLE)
    set(EXAMPLE loopback CACHE STRING "Which examples/<name> to build")
endif()
message(STATUS "EXAMPLE = ${EXAMPLE}")

# Setting COMPONENTS to the selected example (+ its transitive requirements)
# trims the build exactly like MINIMAL_BUILD did -- and is what lets the app
# component be named after the example instead of the special `main`. With
# COMPONENTS defined, ESP-IDF disregards MINIMAL_BUILD, so we no longer set it.
# (For TOE this still pulls in only ioLibrary_Driver; for esp_eth only w5500 --
#  the two W5500 drivers never coexist, via the example's PRIV_REQUIRES.)
set(COMPONENTS ${EXAMPLE})

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hello_world)
