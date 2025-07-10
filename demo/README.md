# rANS Project Enhancements

This repository contains a C++ port of the ryg_rans library, focused on exploring and optimizing Asymmetric Numeral Systems (ANS) for data compression. This README summarizes recent enhancements and experiments conducted within this project.

## Key Achievements and Improvements

### 1. High-Performance Interleaved rANS Implementation

The `gemini-help/main_port.cpp` file has been significantly enhanced with a 4-way interleaved rANS implementation. This technique leverages instruction-level parallelism to achieve substantial performance gains.

*   **Performance:** Benchmarks show a notable increase in speed, particularly in the decoding phase. Our 4-way interleaved decoder consistently achieves speeds around **~200-215 MiB/s**, which is a significant improvement over the original sequential rANS implementation and even surpasses the original `ryg_rans` interleaved version.
*   **Modern Benchmarking:** All performance measurements now utilize `std::chrono::high_resolution_clock`, providing more accurate, portable, and idiomatic C++ timing results.

### 2. Concept Demonstration: Embedding Data in rANS Stream

A new experimental file, `gemini-help/main_embed_test.cpp`, was created to demonstrate a powerful concept: embedding arbitrary raw data directly into the rANS compressed stream.

*   **Mechanism:** This is achieved by understanding the LIFO (Last-In, First-Out) nature of the rANS stream. The encoder, working in reverse, can place raw bytes onto the stream "stack" immediately followed by their rANS-encoded counterparts. The decoder, working forward, then reads the rANS-encoded symbol, and knowing its meaning, can then extract the raw byte that was placed directly after it.
*   **Self-Checking:** The `main_embed_test.cpp` specifically implements a "self-checking" mechanism where each original byte is both rANS-encoded and then embedded raw. The decoder verifies that the rANS-decoded byte matches the embedded raw byte, confirming the integrity of the stream.
*   **Applications:** This technique opens doors for various advanced functionalities, such as:
    *   Embedding metadata (file size, checksums, version info).
    *   Implementing control signals (e.g., switching compression models, resetting state).
    *   Creating extensible compression formats.

## How to Compile and Run

To compile all experimental executables (`main_port.exe`, `main_alias.exe`, `main_embed_test.exe`) located in the `gemini-help/` directory, you can use the following commands from the project root:

```bash
g++ -std=c++17 -O3 -o gemini-help/main_port.exe gemini-help/main_port.cpp
g++ -std=c++17 -O3 -o gemini-help/main_alias.exe gemini-help/main_alias.cpp
g++ -std=c++17 -O3 -o gemini-help/main_embed_test.exe gemini-help/main_embed_test.cpp
```

After compilation, you can run the executables from the `gemini-help/` directory:

```bash
# Run the enhanced port
gemini-help/main_port.exe

# Run the original ryg_rans version
gemini-help/main_alias.exe

# Run the data embedding demonstration
gemini-help/main_embed_test.exe
```

## Future Work

*   Integrating the `interleaved rANS` implementation from `gemini-help/main_port.cpp` into the main project structure (`src/main.cpp`).
*   Further optimization of the rANS core.
*   Exploring adaptive rANS implementations.

Contributions and feedback are welcome!
