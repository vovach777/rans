# rANS C++ Port

This repository hosts a C++ port of the highly efficient Asymmetric Numeral Systems (ANS) compression algorithm, originally developed by Fabian Giesen (ryg_rans: [https://github.com/rygorous/ryg_rans](https://github.com/rygorous/ryg_rans)). This project aims to provide a robust and performant C++ implementation of rANS, with a focus on modern C++ practices and advanced optimization techniques.

## Project Overview

The core of this project is a C++ implementation of the rANS algorithm, designed for high-speed data compression and decompression. It leverages the mathematical principles of ANS to achieve excellent compression ratios while maintaining fast execution times.

## Key Features and Enhancements

*   **High-Performance Interleaved rANS:** The implementation includes a highly optimized 4-way interleaved rANS encoder and decoder. This technique significantly boosts performance by exploiting instruction-level parallelism on modern CPUs, leading to faster compression and decompression speeds.
*   **Modern C++ Benchmarking:** Performance measurements are conducted using `std::chrono::high_resolution_clock`, providing more accurate, portable, and idiomatic C++ timing results. This provides reliable benchmarks for evaluating the algorithm's efficiency.
*   **Extensible Stream Design:** The underlying rANS mechanism allows for advanced stream manipulation, such as embedding arbitrary metadata or control signals directly within the compressed data stream. This opens possibilities for creating flexible and extensible compression formats.

## How to Compile and Run

This project uses CMake for its build system. Ensure you have CMake and a C++17 compatible compiler (like g++ or Clang) installed.

```bash
# Create a build directory and configure CMake
cmake -B build

# Build the project
cmake --build build
```

After successful compilation, you can run the application. `src/main.cpp` acts as a full compressor/decompressor, utilizing memory-mapped files (`mio.hpp`) for efficient I/O and dynamically resizing output files as needed. It also employs SIMD instructions (`simd_reverse`) for optimized byte manipulation.

### Usage:

To compress a file:

```bash
./build/rans <input_filename>
```

This will create a compressed file named `<input_filename>.rans` by default.

To decompress a file:

```bash
./build/rans -d <compressed_filename>
# OR
./build/rans --decode <compressed_filename>
```

This will decompress `<compressed_filename>` and create an output file named `<compressed_filename>.orig` by default.

YouYou can also specify a custom output filename:

```bash
./build/rans <input_filename> <output_filename>
./build/rans -d <compressed_filename> <output_filename>
```

## Origin

This project is a direct port and enhancement of Fabian Giesen's public domain `ryg_rans` implementation. We extend his foundational work with modern C++ features and explore further performance optimizations and advanced usage patterns.

## Future Work

*   Integrating advanced experimental features into the main codebase.
*   Implementing adaptive frequency modeling for improved compression ratios.
*   Adding support for external input files and outputting compressed data.
*   Further performance profiling and optimization.

Contributions and feedback are welcome!
