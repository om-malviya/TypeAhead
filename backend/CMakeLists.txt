cmake_minimum_required(VERSION 3.20)
project(typeahead CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Homebrew (Apple Silicon) dependency prefixes -------------------------
# These let find_package locate the brew-installed CMake configs.
list(APPEND CMAKE_PREFIX_PATH
  /opt/homebrew/opt/libpqxx
  /opt/homebrew/opt/nlohmann-json
  /opt/homebrew/opt/postgresql@16
)

find_package(libpqxx CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Threads REQUIRED)

# hiredis ships no CMake config in Homebrew -> locate it manually.
set(HIREDIS_PREFIX /opt/homebrew/opt/hiredis)
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h HINTS ${HIREDIS_PREFIX}/include)
find_library(HIREDIS_LIB NAMES hiredis HINTS ${HIREDIS_PREFIX}/lib)
if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIB)
  message(FATAL_ERROR "hiredis not found. Install with: brew install hiredis")
endif()

# --- Core library (everything except the HTTP entrypoint) -----------------
add_library(typeahead_core STATIC
  src/core/Trie.cpp
  src/core/ConsistentHashRing.cpp
  src/cache/RedisCache.cpp
  src/store/PostgresStore.cpp
  src/batch/BatchWriter.cpp
  src/Engine.cpp
)
target_include_directories(typeahead_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${HIREDIS_INCLUDE_DIR}
)
target_link_libraries(typeahead_core PUBLIC
  libpqxx::pqxx
  nlohmann_json::nlohmann_json
  Threads::Threads
  ${HIREDIS_LIB}
)

# --- Server executable -----------------------------------------------------
add_executable(typeahead src/main.cpp)
target_link_libraries(typeahead PRIVATE typeahead_core)

# --- Dataset loader tool ---------------------------------------------------
add_executable(load_dataset tools/load_dataset.cpp)
target_link_libraries(load_dataset PRIVATE typeahead_core)
