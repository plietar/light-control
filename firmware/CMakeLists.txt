cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(light-control)
target_add_binary_data(light-control.elf "main/isrgrootx1.pem" TEXT)
