cmake_minimum_required(VERSION 3.16)

# Shared board and platform components live at the repository root.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyd_launcher)
