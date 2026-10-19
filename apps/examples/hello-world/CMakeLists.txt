cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyd_hello_world)

# Build this project as a loadable ELF app instead of a bootable firmware image.
include(elf_loader)
project_elf(cyd_hello_world)
