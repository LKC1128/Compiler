cmake_minimum_required(VERSION 3.20)
project(sysy_compiler LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_compile_options($<$<CONFIG:Release>:-O2>)

include_directories(include)

# Collect sources
file(GLOB_RECURSE FRONTEND_SRCS "src/frontend/*.cpp")
file(GLOB_RECURSE IR_SRCS "src/ir/*.cpp")
file(GLOB_RECURSE ANALYSIS_SRCS "src/analysis/*.cpp")
file(GLOB_RECURSE PASSES_SRCS "src/passes/*.cpp")
file(GLOB_RECURSE BACKEND_SRCS "src/backend/*.cpp")

set(ALL_LIB_SRCS
    ${FRONTEND_SRCS}
    ${IR_SRCS}
    ${ANALYSIS_SRCS}
    ${PASSES_SRCS}
    ${BACKEND_SRCS}
)

add_library(sysy_core STATIC ${ALL_LIB_SRCS})

add_executable(compiler src/main.cpp)
target_link_libraries(compiler PRIVATE sysy_core)

# Correctness instrumentation and native IR regression coverage.
option(SYSY_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(SYSY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sysy_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_compile_options(compiler PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
    target_link_options(compiler PRIVATE -fsanitize=address,undefined)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(audit_regression tests/unit/audit_regression.cpp)
    target_link_libraries(audit_regression PRIVATE sysy_core)
    if(SYSY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(audit_regression PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(audit_regression PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME audit_regression COMMAND audit_regression)
    add_executable(middle_end_regression tests/unit/middle_end_regression.cpp)
    target_link_libraries(middle_end_regression PRIVATE sysy_core)
    if(SYSY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(middle_end_regression PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(middle_end_regression PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME middle_end_regression COMMAND middle_end_regression)
endif()

if(BUILD_TESTING)
    add_executable(compliance_regression tests/unit/compliance_regression.cpp)
    target_link_libraries(compliance_regression PRIVATE sysy_core)
    if(SYSY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(compliance_regression PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(compliance_regression PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME compliance_regression COMMAND compliance_regression)
endif()
