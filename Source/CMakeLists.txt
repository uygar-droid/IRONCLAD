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
  Cabinet/CabStage.cpp
  Cabinet/CabStage.h
  Cabinet/Convolver.cpp
  Cabinet/Convolver.h
  Cabinet/Fft.cpp
  Cabinet/Fft.h
  Cabinet/ImpulseResponse.cpp
  Cabinet/ImpulseResponse.h
  Cabinet/IrLoader.cpp
  Cabinet/IrLoader.h
  Cabinet/Resample.cpp
  Cabinet/Resample.h
  Cabinet/WavReader.cpp
  Cabinet/WavReader.h
  Audio/Engine.h
  Audio/SmoothedValue.h
  Dsp/Adaa.h
  Dsp/Biquad.h
  Dsp/DelayLine.h
  Effects/Boost.h
  Effects/BypassSlot.h
  Effects/Chorus.h
  Effects/Compressor.h
  Effects/Delay.h
  Effects/Drive.h
  Effects/Equalizer.h
  Effects/NoiseGate.h
  Effects/Reverb.cpp
  Effects/Reverb.h
  Effects/TapTempo.h
  Effects/Wah.h
  Parameters/Parameters.h
  Utilities/Denormals.h
  Utilities/Paths.h
  Utilities/SpscQueue.h)

target_include_directories(ironclad_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ironclad_engine PUBLIC cxx_std_20)
target_link_libraries(ironclad_engine
  PUBLIC ironclad::nam
  PRIVATE ironclad_warnings)

set_target_properties(ironclad_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
  # WDL (pulled in by AudioDSPTools' resampler) includes <windows.h>; keep its min/max
  # macros from breaking std::min / std::max.
  target_compile_definitions(ironclad_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
