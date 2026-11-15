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
â”œâ”€â”€ CMakeLists.txt              # CMake build configuration (C++20)
â”œâ”€â”€ Dockerfile                  # Multi-stage Docker container build
â”œâ”€â”€ docker-compose.yml          # Containerized simulation runner
â”œâ”€â”€ include/
â”‚   â””â”€â”€ coldchain/              # Header definitions
â”‚       â”œâ”€â”€ causal_merge_engine.hpp
â”‚       â”œâ”€â”€ config.hpp
â”‚       â”œâ”€â”€ events.hpp
â”‚       â”œâ”€â”€ hashing.hpp
â”‚       â”œâ”€â”€ iot_device.hpp
â”‚       â”œâ”€â”€ ledger_block.hpp
â”‚       â”œâ”€â”€ ledger_node.hpp
â”‚       â”œâ”€â”€ matrix_clock.hpp
â”‚       â”œâ”€â”€ metrics.hpp
â”‚       â”œâ”€â”€ network_simulator.hpp
â”‚       â”œâ”€â”€ physical_clock.hpp
â”‚       â”œâ”€â”€ pruning_engine.hpp
â”‚       â””â”€â”€ vector_clock.hpp
â”œâ”€â”€ src/                        # Core implementation source files
â”œâ”€â”€ tests/                      # Unit and integration tests (doctest)
â”œâ”€â”€ experiments/                # Parameter sweep benchmarks & plotting scripts
â””â”€â”€ deps/                       # Header-only dependencies (doctest, picosha2)
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


