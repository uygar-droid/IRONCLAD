# NeuralAmpModelerCore as a static library, built with IRONCLAD's flags.
# The upstream CMakeLists only builds command-line tools, so the library target
# is defined here. NAM Core sources are used unmodified.

set(NAM_CORE_DIR "${IRONCLAD_THIRDPARTY_DIR}/NeuralAmpModelerCore")

file(GLOB _nam_sources CONFIGURE_DEPENDS
  "${NAM_CORE_DIR}/NAM/*.cpp"
  "${NAM_CORE_DIR}/NAM/*/*.cpp")

add_library(ironclad_nam STATIC ${_nam_sources})
target_compile_features(ironclad_nam PUBLIC cxx_std_20)
target_include_directories(ironclad_nam
  PUBLIC "${NAM_CORE_DIR}"
         "${NAM_CORE_DIR}/NAM"
  SYSTEM PUBLIC
         "${NAM_CORE_DIR}/Dependencies/eigen"
         "${NAM_CORE_DIR}/Dependencies/nlohmann")

# Definitions that affect headers must be identical in every translation unit -> PUBLIC.
#  EIGEN_MPL2_ONLY  : guarantees no LGPL-only Eigen code is compiled in (see LICENSES.md)
#  NAM_ENABLE_A2_FAST: same default as upstream NAM Core
target_compile_definitions(ironclad_nam PUBLIC EIGEN_MPL2_ONLY NAM_ENABLE_A2_FAST)

if(MSVC)
  target_compile_definitions(ironclad_nam PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
  target_compile_options(ironclad_nam PRIVATE /W1)
  target_compile_options(ironclad_nam PUBLIC /bigobj)
else()
  target_compile_options(ironclad_nam PRIVATE -w)
endif()

set_target_properties(ironclad_nam PROPERTIES FOLDER "ThirdParty")

# NAM architectures (WaveNet, LSTM, ...) register themselves through static
# objects in their own .cpp files. Nothing references those objects directly, so a
# normal static-library link silently drops them and every .nam load fails with
# "No config parser registered for architecture". Consumers therefore link the
# archive whole. Always use `ironclad::nam` rather than `ironclad_nam`.
add_library(ironclad_nam_whole INTERFACE)
target_link_libraries(ironclad_nam_whole INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,ironclad_nam>")
add_library(ironclad::nam ALIAS ironclad_nam_whole)
