cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyd_clock)

# Build as a loadable ELF module, not as a bootable firmware image.
include(elf_loader)
project_elf(cyd_clock)
