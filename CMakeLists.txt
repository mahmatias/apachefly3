cmake_minimum_required(VERSION 3.25)
project(comanche3 VERSION 1.0.0 LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Multi-config generator (Visual Studio)
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_CXX_FLAGS_DEBUG "/MDd /Zi /Ob0 /Od /RTC1")
  set(CMAKE_CXX_FLAGS_RELEASE "/MD /O2 /Ob2 /DNDEBUG")
endif()

# ISO paths (optional, for tests)
option(C3_ISO_PT "Path to Portuguese ISO" "")
option(C3_ISO_EN "Path to English ISO" "")

# SDL3
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SDL3 REQUIRED sdl3)
else()
  find_path(SDL3_INCLUDE_DIR SDL3/SDL.h)
  find_library(SDL3_LIBRARY SDL3)
  if(SDL3_INCLUDE_DIR AND SDL3_LIBRARY)
    set(SDL3_FOUND TRUE)
    set(SDL3_INCLUDE_DIRS ${SDL3_INCLUDE_DIR})
    set(SDL3_LIBRARIES ${SDL3_LIBRARY})
  endif()
endif()

# doctest
include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.11
)
FetchContent_MakeAvailable(doctest)

# Directories
add_subdirectory(src)
add_subdirectory(tests)

# Main executable
add_executable(comanche3
  src/app/main.cpp
  src/app/application.cpp
)
target_link_libraries(comanche3 PRIVATE c3_app c3_data c3_platform c3_audio c3_present)
if(SDL3_FOUND)
  target_link_libraries(comanche3 PRIVATE ${SDL3_LIBRARIES})
  target_include_directories(comanche3 PRIVATE ${SDL3_INCLUDE_DIRS})
endif()

# Install
install(TARGETS comanche3 RUNTIME DESTINATION bin)