add_executable(ironclad_tests
  TestMain.cpp
  AllocationTracking.cpp
  AllocationTracking.h
  TestAmp.cpp
  TestCab.cpp
  TestCabinetDsp.cpp
  WavTestUtils.h
  TestEffects.cpp
  TestEngine.cpp
  TestNamSmoke.cpp
  TestParameters.cpp
  TestPresets.cpp
  TestSpscQueue.cpp)

target_include_directories(ironclad_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${IRONCLAD_THIRDPARTY_DIR}/doctest")

target_compile_definitions(ironclad_tests PRIVATE
  IRONCLAD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  IRONCLAD_NAM_EXAMPLE_MODELS_DIR="${NAM_CORE_DIR}/example_models")

find_package(Threads REQUIRED)
target_link_libraries(ironclad_tests PRIVATE ironclad_engine ironclad::nam ironclad_warnings Threads::Threads)

add_test(NAME ironclad_tests COMMAND ironclad_tests)

# CPU benchmark (not part of ctest): ironclad_bench [sampleRate] [blockSize]
add_executable(ironclad_bench Bench.cpp WavTestUtils.h)
target_include_directories(ironclad_bench PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ironclad_bench PRIVATE IRONCLAD_NAM_EXAMPLE_MODELS_DIR="${NAM_CORE_DIR}/example_models")
target_link_libraries(ironclad_bench PRIVATE ironclad_engine ironclad::nam Threads::Threads)
