cmake_minimum_required(VERSION 3.20)
project(coldchain_vclock CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# --- Core library -----------------------------------------------------------
add_library(coldchain_core
    src/vector_clock.cpp
    src/matrix_clock.cpp
    src/events.cpp
    src/hashing.cpp
    src/ledger_block.cpp
    src/physical_clock.cpp
    src/causal_merge_engine.cpp
    src/iot_device.cpp
    src/ledger_node.cpp
    src/pruning_engine.cpp
    src/network_simulator.cpp
    src/metrics.cpp
)
target_include_directories(coldchain_core PUBLIC include)
target_include_directories(coldchain_core PRIVATE deps)

# --- Simulation executable ---------------------------------------------------
add_executable(sim src/main_sim.cpp)
target_link_libraries(sim coldchain_core)
target_include_directories(sim PRIVATE deps)

# --- Sweep runner ------------------------------------------------------------
add_executable(sweep_runner experiments/sweep_runner.cpp)
target_link_libraries(sweep_runner coldchain_core)
target_include_directories(sweep_runner PRIVATE deps)

# --- Tests -------------------------------------------------------------------
enable_testing()

macro(add_coldchain_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} coldchain_core)
    target_include_directories(${test_name} PRIVATE deps)
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_coldchain_test(test_ledger_integrity tests/test_ledger_integrity.cpp)
add_coldchain_test(test_vector_clock tests/test_vector_clock.cpp)
add_coldchain_test(test_causal_merge tests/test_causal_merge.cpp)
add_coldchain_test(test_matrix_clock tests/test_matrix_clock.cpp)
add_coldchain_test(test_network_simulator tests/test_network_simulator.cpp)
add_coldchain_test(test_pruning_safety tests/test_pruning_safety.cpp)
add_coldchain_test(test_replicated_ledger tests/test_replicated_ledger.cpp)
add_coldchain_test(test_baselines_metrics tests/test_baselines_metrics.cpp)
