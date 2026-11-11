add_library(jam_engine STATIC
  src/engine.cpp
)
target_include_directories(jam_engine
  PUBLIC include
  PRIVATE src
)
# Every client must compute identical samples, so forbid transformations that
# change floating point results between compilers and CPUs.
target_compile_options(jam_engine PUBLIC -ffp-contract=off -fno-fast-math)
target_compile_options(jam_engine PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)

if(JAM_BUILD_TESTS)
  include(FetchContent)
  # A release archive rather than a git clone: faster, and it cannot hang on a
  # stalled shallow fetch.
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(jam_tests
    tests/test_math.cpp
    tests/test_dsp.cpp
    tests/test_fx.cpp
    tests/test_seq.cpp
    tests/test_engine.cpp
  )
  target_include_directories(jam_tests PRIVATE src tools)
  target_link_libraries(jam_tests PRIVATE jam_engine Catch2::Catch2WithMain)

  add_executable(jam_bench bench/bench_voices.cpp)
  target_link_libraries(jam_bench PRIVATE jam_engine)

  add_executable(jam_render tools/render_demo.cpp)
  target_link_libraries(jam_render PRIVATE jam_engine)

  enable_testing()
  add_test(NAME jam_tests COMMAND jam_tests)
endif()
