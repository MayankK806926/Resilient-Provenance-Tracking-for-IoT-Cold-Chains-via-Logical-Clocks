# Resilient Provenance Tracking for IoT Cold Chains via Logical Clocks

A C++20 framework implementing resilient provenance tracking and causal event ordering for distributed IoT cold chain logistics networks experiencing network blackouts, clock drift, and intermittent connectivity.

## Overview

In pharmaceutical and perishable cold chain monitoring, IoT sensors track environmental conditions (e.g., temperature excursions) across multiple custodian nodes:
- **Node 0**: Manufacturer
- **Node 1**: Distributor
- **Node 2**: Cold Storage
- **Node 3**: Pharmacy

During transport, extended network blackouts and unsynchronized physical RTC drift can cause naive physical timestamps to misorder causal sequences or obscure regulatory provenance. 

This repository implements:
1. **Vector Clocks & Matrix Clocks**: Partial and causal ordering of distributed events across custodian transitions.
2. **Causal Merge Engine**: Deterministic reconciliation of branching event histories without central coordinator.
3. **Cryptographic Ledger Integrity**: SHA-256 block-chained provenance log preventing retroactive tampering.
4. **Matrix Clock Pruning Engine**: Dynamic causal horizon calculation for safe log pruning on memory-constrained edge devices.
5. **Dual-Pipeline Simulation Framework**: Quantitative side-by-side comparison between logical clock causal ordering and naive physical timestamping.

## Project Structure

```
.
|-- CMakeLists.txt              # CMake build configuration (C++20)
|-- Dockerfile                  # Multi-stage Docker container build
|-- docker-compose.yml          # Containerized simulation runner
|-- .dockerignore               # Docker build ignore rules
|-- .gitignore                  # Git ignore patterns
|-- README.md                   # Project documentation
|-- include/
|   `-- coldchain/              # Header definitions
|       |-- causal_merge_engine.hpp
|       |-- config.hpp
|       |-- events.hpp
|       |-- hashing.hpp
|       |-- iot_device.hpp
|       |-- ledger_block.hpp
|       |-- ledger_node.hpp
|       |-- matrix_clock.hpp
|       |-- metrics.hpp
|       |-- network_simulator.hpp
|       |-- physical_clock.hpp
|       |-- pruning_engine.hpp
|       `-- vector_clock.hpp
|-- src/                        # Core implementation source files
|   |-- causal_merge_engine.cpp
|   |-- events.cpp
|   |-- hashing.cpp
|   |-- iot_device.cpp
|   |-- ledger_block.cpp
|   |-- ledger_node.cpp
|   |-- main_sim.cpp
|   |-- matrix_clock.cpp
|   |-- metrics.cpp
|   |-- network_simulator.cpp
|   |-- physical_clock.cpp
|   |-- pruning_engine.cpp
|   `-- vector_clock.cpp
|-- tests/                      # Unit and integration tests (doctest)
|   |-- test_causal_merge.cpp
|   |-- test_ledger_integrity.cpp
|   |-- test_matrix_clock.cpp
|   |-- test_network_simulator.cpp
|   |-- test_pruning_safety.cpp
|   `-- test_vector_clock.cpp
|-- experiments/                # Parameter sweep benchmarks & plotting scripts
|   |-- plot_results.py
|   |-- run_all_sweeps.sh
|   `-- sweep_runner.cpp
`-- deps/                       # Header-only dependencies
    |-- doctest.h
    `-- picosha2.h
```

## Prerequisites

- **C++ Compiler**: GCC 11+, Clang 13+, or MSVC supporting C++20
- **CMake**: Version 3.20 or newer
- **Ninja** or **Make**
- **Python 3** (optional, with `matplotlib` and `pandas` for benchmark plots)
- **Docker** (optional)

## Building from Source

### Native Build (CMake)

```bash
# Configure build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build core simulation and sweep runner
cmake --build build --config Release
```

### Running Tests

```bash
ctest --test-dir build --output-on-failure
```

### Running the Simulation

```bash
# Run comparative simulation
./build/sim --shipments 5 --duration-hours 24 --seed 42
```

## Running Parameter Sweeps & Experiments

```bash
# Execute automated sweep suite
chmod +x experiments/run_all_sweeps.sh
./experiments/run_all_sweeps.sh build

# Generate plots from output CSVs
python3 experiments/plot_results.py --input data/sweeps/results.csv --output-dir data/sweeps/
```

## Docker Usage

Build and run using Docker Compose:

```bash
docker compose up --build
```


