cmake_minimum_required(VERSION 3.24.2)

project(
  AliceBob
  VERSION 1.0
  LANGUAGES CXX)

add_executable(AliceBob
  main.cpp
  Transfer.cpp
)

find_package(nlohmann_json 3.6.1 REQUIRED)
target_link_libraries(AliceBob PRIVATE nlohmann_json::nlohmann_json)

find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIR})
target_link_libraries(AliceBob PRIVATE ${CURL_LIBRARIES})

target_compile_features(AliceBob PRIVATE cxx_std_20)