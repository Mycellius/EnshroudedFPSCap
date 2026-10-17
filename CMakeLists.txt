cmake_minimum_required(VERSION 3.20)
project(EnshroudedBaseCap VERSION 1.1.0 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "EnshroudedBaseCap only builds as a 64-bit Windows DLL.")
endif()

option(BASECAP_BUILD_TESTS "Build the pacing test" ON)

# Everything lands in one folder so the test finds the DLLs next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/bin>")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(EnshroudedBaseCap SHARED src/basecap.cpp src/basecap.rc)
set_target_properties(EnshroudedBaseCap PROPERTIES PREFIX "")
target_compile_features(EnshroudedBaseCap PRIVATE cxx_std_17)
target_link_libraries(EnshroudedBaseCap PRIVATE minhook user32 gdi32)

if(MSVC)
  target_compile_definitions(EnshroudedBaseCap PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(EnshroudedBaseCap PRIVATE /W4 /permissive-)
else()
  target_compile_options(EnshroudedBaseCap PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)
  target_link_options(EnshroudedBaseCap PRIVATE -static -s)
endif()

if(BASECAP_BUILD_TESTS)
  enable_testing()
  add_library(fake_interposer SHARED tests/fake_interposer.c)
  set_target_properties(fake_interposer PROPERTIES OUTPUT_NAME "sl.interposer" PREFIX "")
  add_executable(pacing_test tests/pacing_test.c)
  if(MSVC)
    target_compile_definitions(pacing_test PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  add_dependencies(pacing_test EnshroudedBaseCap fake_interposer)
  add_test(NAME pacing COMMAND pacing_test WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
