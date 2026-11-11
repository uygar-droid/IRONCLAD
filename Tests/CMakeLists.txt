add_executable(ironclad_tests
  TestMain.cpp
  AllocationTracking.cpp
  AllocationTracking.h
  TestAmp.cpp
  TestEngine.cpp
  TestNamSmoke.cpp
  TestParameters.cpp
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
