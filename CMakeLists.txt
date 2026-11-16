cmake_minimum_required(VERSION 4.0)

find_package(cmake-fetch REQUIRED PATHS node_modules/cmake-fetch)
find_package(cmake-fuzz REQUIRED PATHS node_modules/cmake-fuzz)
find_package(cmake-prelude REQUIRED PATHS node_modules/cmake-prelude)
find_package(cmake-test REQUIRED PATHS node_modules/cmake-test)
find_package(cmake-ucd REQUIRED PATHS node_modules/cmake-ucd)

project(idna LANGUAGES C VERSION 0.0.0)

fetch_package("github:holepunchto/libutf")
fetch_package("github:holepunchto/libpunycode")
fetch_package("github:holepunchto/libnormalize")

include(cmake/tables.cmake)

add_library_targets(
  idna
  HEADERS
    include/idna.h
  FEATURES
    c_std_11
  SOURCES
    src/idna.c
    "${idna_tables_header}"
    "${idna_tables_source}"
  LINK
    utf::<TYPE>
    punycode::<TYPE>
    normalize::<TYPE>
)

add_dependencies(idna idna_tables)

target_include_directories(
  idna
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)

add_test_subdirectory(test)

if(PROJECT_IS_TOP_LEVEL)
  set(BENCH OFF CACHE BOOL "Build the benchmarks")

  if(BENCH)
    add_subdirectory(bench)
  endif()
endif()
