cmake_minimum_required(VERSION 3.20)
project(lob_matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LOB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(lob src/order_book.cpp)
target_include_directories(lob PUBLIC include)
if(MSVC)
  target_compile_options(lob PRIVATE /W4)
else()
  target_compile_options(lob PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
if(LOB_SANITIZE)
  target_compile_options(lob PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob PUBLIC -fsanitize=address,undefined)
endif()

include(CTest)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG 063de7e9578f82b369302001269680b4b1553359  # v1.18.0
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(lob_tests tests/test_order_book.cpp tests/test_randomized.cpp)
  target_link_libraries(lob_tests PRIVATE lob GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(lob_tests)
endif()
