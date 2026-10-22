cmake_minimum_required(VERSION 3.16)

# Shared board and platform components live at the repository root.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Version from the latest vX.Y.Z tag only. ESP-IDF's default "git describe --tags"
# would also pick up other tags, such as "apps" used for app publishing.
execute_process(COMMAND git describe --always --tags --dirty --match "v[0-9]*"
                WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
                OUTPUT_VARIABLE PROJECT_VER OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result ERROR_QUIET)
if(NOT git_result EQUAL 0)
    unset(PROJECT_VER)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cyd_launcher)
