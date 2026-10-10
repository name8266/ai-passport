cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../../../components/bsp")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Use the shared verified firmware/archive artifact naming convention.
set(PROJECT_VER "notification-hub-public-1.0.0-rc1")
project(FoloToy-AI-Passport)
