# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# App/resource tuning options live in main/Kconfig.projbuild and
# components/esp32_mquickjs/Kconfig.projbuild.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/main/Kconfig.projbuild"
    "${CMAKE_SOURCE_DIR}/components/esp32_mquickjs/Kconfig.projbuild")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32qjs)
