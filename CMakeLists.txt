cmake_minimum_required(VERSION 3.16.0)
# Stamp the firmware with the GridCheck version from platformio.ini (EH_VERSION),
# so the update code can tell which version a firmware slot holds.
file(READ ${CMAKE_CURRENT_LIST_DIR}/platformio.ini PIO_INI)
string(REGEX MATCH "EH_VERSION=\\\\\"([0-9.]+)" _ver "${PIO_INI}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
# Which screen this build is for (platformio.ini passes -DGC_BOARD=knob18 for the round one).
# The project name is stamped into the firmware too, and a screen refuses an update
# whose project name differs from its own, so a 7" never installs the round screen's
# firmware or the other way round.
if(NOT DEFINED GC_BOARD)
  set(GC_BOARD "7b" CACHE STRING "screen: 7b or knob18")
endif()
if(GC_BOARD STREQUAL "knob18")
  set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.knob18")
  set(GC_PROJECT gridcheck-knob18)
else()
  set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
  set(GC_PROJECT gridcheck)
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${GC_PROJECT})
