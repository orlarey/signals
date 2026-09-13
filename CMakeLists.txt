cmake_minimum_required(VERSION 3.12)

project(signals LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wno-unused-parameter")

if (TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,clang-*,performance-*,llvm-*,modernize-*,-modernize-use-trailing-return-type,cert-*")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# The copy unit (signals/) and the vendored dependencies (tlib/, interval/,
# FaustAlgebra/, DirectedGraph/). This master-dev lineage carries no Dependencies/
# (that one belongs to the FIR18 lineage, on branch fir18). DirectedGraph/ is here
# because tlib itself now depends on it -- header-only, nothing to compile.
# CONFIGURE_DEPENDS keeps the source list in sync with the directories (the
# interval standalone project taught us the cost of a stale explicit list).
file(GLOB_RECURSE SIGNALS_SRC CONFIGURE_DEPENDS signals/*.cpp)
file(GLOB TLIB_SRC      CONFIGURE_DEPENDS tlib/*.cpp)
file(GLOB INTERVAL_SRC  CONFIGURE_DEPENDS interval/*.cpp)

# the library
add_library(signals STATIC
    ${SIGNALS_SRC}
    ${TLIB_SRC}
    ${INTERVAL_SRC})

# "." first so that "interval/interval_def.hh"-style prefixed includes
# resolve; the rest is the flat include model shared with the Faust compiler.
target_include_directories(signals PUBLIC
    .
    signals/terms
    signals/types
    signals/traverse
    signals/analysis
    signals/normalize
    signals/forms
    signals/passes
    signals/session
    interval
    FaustAlgebra
    tlib
    DirectedGraph)

# the test executable
add_executable(signals-tests tests/tests.cpp)
target_link_libraries(signals-tests PRIVATE signals)
add_test(NAME signals-tests COMMAND signals-tests)

# The layering rule of the signal library : a layer of signals/ includes only the
# layers listed before it (tests/layers.py holds the order and checks the #includes).
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_Interpreter_FOUND)
    add_test(NAME signals-layers COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/layers.py ${CMAKE_CURRENT_SOURCE_DIR}/signals)
endif()
