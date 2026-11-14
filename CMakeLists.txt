cmake_minimum_required(VERSION 3.16.0)
# Stamp the firmware with the GridCheck version from platformio.ini (EH_VERSION),
# so the update code can tell which version a firmware slot holds.
file(READ ${CMAKE_CURRENT_LIST_DIR}/platformio.ini PIO_INI)
string(REGEX MATCH "EH_VERSION=\\\\\"([0-9.]+)" _ver "${PIO_INI}")
set(PROJECT_VER "${CMAKE_MATCH_1}")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(gridcheck)
