cmake_minimum_required(VERSION 3.16)
project(minisql VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MINISQL_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(MINISQL_WERROR "Treat warnings as errors" OFF)
option(MINISQL_BUILD_TESTS "Build unit/integration tests" ON)

set(MINISQL_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
if(MINISQL_WERROR)
  list(APPEND MINISQL_WARNINGS -Werror)
endif()

if(MINISQL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- core engine library (no I/O to the terminal, no CLI concerns) ----
add_library(minisql_core
  src/value.cpp
  src/catalog.cpp
  src/csv.cpp
  src/lexer.cpp
  src/parser.cpp
  src/validator.cpp
  src/planner.cpp
  src/executor.cpp
  src/engine.cpp
  src/datagen.cpp
)
target_include_directories(minisql_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(minisql_core PRIVATE ${MINISQL_WARNINGS})

# ---- applications ----
add_executable(minisql apps/minisql_cli.cpp)
target_link_libraries(minisql PRIVATE minisql_core)
target_compile_options(minisql PRIVATE ${MINISQL_WARNINGS})

add_executable(minisql_bench apps/minisql_bench.cpp)
target_link_libraries(minisql_bench PRIVATE minisql_core)
target_compile_options(minisql_bench PRIVATE ${MINISQL_WARNINGS})

# ---- tests ----
if(MINISQL_BUILD_TESTS)
  enable_testing()
  foreach(t parser_tests csv_tests execution_tests index_tests)
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE minisql_core)
    target_compile_options(${t} PRIVATE ${MINISQL_WARNINGS})
    target_compile_definitions(${t} PRIVATE MINISQL_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ${t} COMMAND ${t})
  endforeach()

  add_test(NAME cli_smoke
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_smoke_test.sh $<TARGET_FILE:minisql>
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
