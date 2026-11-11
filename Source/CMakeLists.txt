# Framework-independent audio engine. Must never include iPlug2 headers:
# no iPlug2 include paths are given to this target, so such an include fails to compile.

add_library(ironclad_engine STATIC
  Amp/AmpModel.cpp
  Amp/AmpModel.h
  Amp/AmpStage.cpp
  Amp/AmpStage.h
  Amp/CaptureInfo.h
  Amp/CaptureLoader.cpp
  Amp/CaptureLoader.h
  Amp/ResamplingModel.h
  Audio/Engine.cpp
  Audio/Engine.h
  Audio/SmoothedValue.h
  Parameters/Parameters.h
  Utilities/Denormals.h
  Utilities/SpscQueue.h)

target_include_directories(ironclad_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ironclad_engine PUBLIC cxx_std_20)
target_link_libraries(ironclad_engine
  PUBLIC ironclad::nam
  PRIVATE ironclad_warnings)

set_target_properties(ironclad_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
