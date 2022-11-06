cmake_minimum_required (VERSION 3.1)
set (EXE_NAME ch347-nor-prog)
project (${EXE_NAME} C CXX)
find_package(PkgConfig)
pkg_check_modules(libusb-1.0 REQUIRED libusb-1.0)

add_executable(${EXE_NAME} ch347.c spi-op.cpp main.cpp misc.cpp spi_flash.cpp spi_ids.cpp stdafx.cpp)
set_property(TARGET ${EXE_NAME} PROPERTY C_STANDARD 99)
target_link_libraries(${EXE_NAME} ${libusb-1.0_LIBRARIES})