# IRONCLAD VST3 (Windows x64, macOS universal). VST3 is the only format built.

iplug_add_plugin(IRONCLAD
  SOURCES
    IRONCLAD.cpp
    IRONCLAD.h
    config.h
  RESOURCES
    resources/fonts/Roboto-Regular.ttf
  FORMATS VST3
  LINK ironclad_engine)

if(WIN32)
  # iPlug2's CMake only attaches main.rc to the standalone app; the VST3 DLL also
  # needs it, otherwise fonts and images cannot be found at runtime on Windows.
  set(_rc "${CMAKE_CURRENT_SOURCE_DIR}/resources/main.rc")
  target_sources(IRONCLAD-vst3 PRIVATE "${_rc}")
  set_source_files_properties("${_rc}" PROPERTIES
    COMPILE_FLAGS "/I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources/img\" /I\"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
endif()

