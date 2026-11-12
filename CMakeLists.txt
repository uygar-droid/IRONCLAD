cmake_minimum_required(VERSION 3.25)

# ---- Product scope ----------------------------------------------------------
# IRONCLAD ships as a VST3 for Windows x64 and macOS (universal) only.
# Linux is not a target; on other systems only the engine unit tests can be
# configured, for internal development convenience.

if(APPLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
  endif()
endif()

set(IPLUG2_CXX_STANDARD 20 CACHE STRING "C++ standard used for iPlug2 and IRONCLAD" FORCE)

project(IRONCLAD VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Must match iPlug2's choice (static CRT) so every library links into one plugin DLL.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(WIN32 OR APPLE)
  set(_ironclad_plugin_default ON)
else()
  set(_ironclad_plugin_default OFF)
endif()
option(IRONCLAD_BUILD_PLUGIN "Build the IRONCLAD VST3 (Windows/macOS only)" ${_ironclad_plugin_default})
option(IRONCLAD_BUILD_TESTS "Build the engine unit tests" ON)

if(IRONCLAD_BUILD_PLUGIN AND NOT (WIN32 OR APPLE))
  message(FATAL_ERROR "IRONCLAD's VST3 is built on Windows and macOS only.")
endif()

set(IRONCLAD_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty")

foreach(_dep iPlug2/iPlug2.cmake NeuralAmpModelerCore/NAM/dsp.h NeuralAmpModelerCore/Dependencies/eigen/Eigen/Dense)
  if(NOT EXISTS "${IRONCLAD_THIRDPARTY_DIR}/${_dep}")
    message(FATAL_ERROR "Missing ThirdParty/${_dep}. Run Scripts/bootstrap (git submodule update --init --recursive).")
  endif()
endforeach()

# Warnings for IRONCLAD's own code (third-party targets keep their defaults).
add_library(ironclad_warnings INTERFACE)
if(MSVC)
  target_compile_options(ironclad_warnings INTERFACE /W4 /permissive-)
else()
  target_compile_options(ironclad_warnings INTERFACE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

include(cmake/NamCore.cmake)  # -> ironclad_nam
add_subdirectory(Source)      # -> ironclad_engine

if(IRONCLAD_BUILD_PLUGIN)
  set(IPLUG2_DIR "${IRONCLAD_THIRDPARTY_DIR}/iPlug2" CACHE PATH "iPlug2 root directory")
  # iPlug2 silently skips VST3 when the SDK is absent; for IRONCLAD that is a hard error.
  if(NOT EXISTS "${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK/pluginterfaces/vst/ivstaudioprocessor.h")
    message(FATAL_ERROR "VST3 SDK not found in ThirdParty/iPlug2/Dependencies/IPlug/VST3_SDK. Run Scripts/bootstrap.sh first.")
  endif()
  include(${IPLUG2_DIR}/iPlug2.cmake)
  find_package(iPlug2 REQUIRED)
  add_subdirectory(Source/Plugin)
endif()

if(IRONCLAD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(Tests)
endif()
