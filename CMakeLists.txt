cmake_minimum_required(VERSION 3.20)
project(NumericalSanitizer LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_library(nsan_runtime STATIC
  runtime/numerical_sanitizer_runtime.c
)
target_include_directories(nsan_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(nsan_runtime PROPERTIES
  OUTPUT_NAME "nsan_runtime"
)

add_llvm_pass_plugin(NumericalSanitizerPass
  lib/NumericalSanitizerPass.cpp
)
target_include_directories(NumericalSanitizerPass PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(NSAN_PLUGIN_PATH "$<TARGET_FILE:NumericalSanitizerPass>")
set(NSAN_RUNTIME_PATH "$<TARGET_FILE:nsan_runtime>")
set(NSAN_WRAPPER_IN "${CMAKE_CURRENT_SOURCE_DIR}/tools/numerical-clang.in")
set(NSAN_WRAPPER_OUT "${CMAKE_CURRENT_BINARY_DIR}/numerical-clang")

configure_file(${NSAN_WRAPPER_IN} ${NSAN_WRAPPER_OUT} @ONLY)
file(CHMOD ${NSAN_WRAPPER_OUT}
  PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

enable_testing()
add_test(
  NAME nsan_examples
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py --clang ${NSAN_WRAPPER_OUT}
)
