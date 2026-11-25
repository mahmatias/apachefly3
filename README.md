# Comanche 3 - Modern C++ Port

A modern C++20 reimplementation of the classic **Comanche 3** helicopter simulation game engine.

## Overview

This project aims to rebuild Comanche 3 from the ground up using modern C++ practices, targeting Windows with DirectX 11 and SDL3 for cross-platform window/input handling.

## Features

- **Data Layer** (`c3_data`): ISO9660 parsing, resource extraction, LZP1 decompression, PCX/3DO/MIDI/WAV loaders
- **Platform Layer** (`c3_platform`): SDL3 window management, D3D11 rendering backend
- **Audio Layer** (`c3_audio`): Audio mixer and playback
- **Presentation Layer** (`c3_present`): Entity rendering and model handling
- **Application Layer** (`c3_app`): Game logic and main loop

## Building

### Requirements
- CMake 3.25+
- C++20 compiler (MSVC 19.30+, GCC 11+, Clang 13+)
- SDL3 development libraries
- DirectX 11 SDK (Windows)

### Windows (Visual Studio)
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Linux/macOS
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Running

```bash
comanche3 --iso <path_to_comanche3.iso> [--run]
```

- `--iso`: Path to Comanche 3 ISO file or mounted directory
- `--run`: Launch the game after loading resources

## Project Structure

```
src/
├── app/           # Application layer
├── audio/         # Audio engine
├── data/          # Data formats & extraction (ISO9660, LZP1, PCX, 3DO, MIDI, WAV)
├── platform/      # Platform abstraction (SDL3, D3D11)
├── present/       # Rendering & models
└── tools/         # Command-line utilities
```

## Tools

- `c3_extract` - Extract resources from ISO
- `c3_decompress` - Decompress LZP1 files
- `c3_test_lzp1` - Test LZP1 decompression
- `c3_analyze_bitstream` - Analyze compressed bitstreams

## License

Research/educational project. Original Comanche 3 assets are property of NovaLogic.