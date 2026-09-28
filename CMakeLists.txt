cmake_minimum_required(VERSION 3.16)

# The production tarot firmware is fully offline. Build only the application
# and its transitive dependencies instead of scanning/linking every ESP-IDF
# component (Wi-Fi, MQTT, OpenThread, etc.).
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(FoloToy-AI-Passport)
