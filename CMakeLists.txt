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

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
# (With MINIMAL_BUILD, only the backend actually required by `main` is compiled: ioLibrary_Driver
#  for TOE=1, espressif/w5500(esp_eth) for TOE=0 — so the two W5500 drivers never coexist.)
idf_build_set_property(MINIMAL_BUILD ON)
project(hello_world)
