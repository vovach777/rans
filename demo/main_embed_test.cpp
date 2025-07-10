#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <vector>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <cstdarg>
#include "../include/rans.hpp"

// Helper to print an error and exit
static void panic(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    fputs("Error: ", stderr);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fputs("\n", stderr);
    exit(1);
}

// Helper to read a file into a buffer
static uint8_t* read_file(char const* filename, size_t* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = new uint8_t[size];
    if (fread(buf, size, 1, f) != 1) {
        fclose(f);
        delete[] buf;
        return 0;
    }
    fclose(f);
    if (out_size) *out_size = size;
    return buf;
}

int main() {
    size_t in_size;
    uint8_t* in_bytes = read_file("book1", &in_size);
    if (!in_bytes) {
        fprintf(stderr, "Error: could not read book1\n");
        return 1;
    }

    printf("--- Test: Self-checking rANS stream ---\n");

    // 1. Setup statistics
    static const uint32_t prob_bits = 16;
    rANS::SymbolStats<prob_bits, 8> stats;
    stats.count_freqs(in_bytes, in_bytes + in_size);

    stats.normalize_freqs();
    stats.make_alias_table();

    // 2. Allocate buffers (out_max_size needs to be larger as we embed raw bytes)
    static size_t out_max_size = 128 << 20; // 128MB
    uint8_t* out_buf = new uint8_t[out_max_size];
    uint8_t* dec_bytes = new uint8_t[in_size];
    memset(dec_bytes, 0xcc, in_size);
    uint8_t* rans_begin;

    // 3. Encode with self-checking mechanism
    printf("Encoding...\n");
    auto start_enc = std::chrono::high_resolution_clock::now();
    {
        rANS::State<prob_bits> rans;
        uint8_t* ptr = out_buf + out_max_size;
        auto put_byte = [&](uint8_t byte) { *--ptr = byte; };

        // Work backwards through the input file
        for (size_t i = in_size; i > 0; --i) {
            uint8_t s = in_bytes[i - 1];

            // IMPORTANT: If 's' has a zero frequency in 'stats', this will crash!
            // For this test, we assume 'book1' does not contain such symbols,
            // or that 'CMD_INSERT_RAW_BYTE' is the only symbol we need to force.
            // A real rANS encoder would use escape codes for zero-frequency symbols.

            // Order matters for LIFO stack:
            // 1. Put the raw byte onto the stream (will be read second by decoder)
            put_byte(s);
            // 2. Put the rANS-encoded byte on top (will be read first by decoder)
            rans.RansEncPutAlias(put_byte, stats, s);
        }
        rans.RansEncFlush(put_byte);
        rans_begin = ptr;
    }
    auto end_enc = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_enc = end_enc - start_enc;
    printf("Encode finished in %.2f ms. Compressed size: %lld bytes\n", elapsed_enc.count(), (long long)(out_buf + out_max_size - rans_begin));


    // 4. Decode and self-check
    printf("Decoding...\n");
    auto start_dec = std::chrono::high_resolution_clock::now();
    {
        rANS::State<prob_bits> rans;
        uint8_t* ptr = rans_begin;
        uint8_t* end_ptr = out_buf + out_max_size;
        auto get_byte = [&]() -> uint8_t {
            if (ptr >= end_ptr) {
                panic("Read past end of buffer during decode!");
            }
            return *ptr++;
        };
        rans.RansDecInit(get_byte);

        // Work forwards through the output buffer
        for (size_t i = 0; i < in_size; ++i) {
            // 1. Decode the rANS symbol
            uint32_t decoded_s = rans.RansDecGetAlias(stats);
            rans.RansDecRenorm(get_byte);

            // 2. Read the raw byte that was embedded right after it
            uint8_t raw_check_byte = get_byte();

            // 3. Compare them
            if (decoded_s != raw_check_byte) {
                panic("Mismatch at index %zu: decoded %u, raw %u\n", i, decoded_s, raw_check_byte);
            }
            dec_bytes[i] = (uint8_t)decoded_s;
        }
    }
    auto end_dec = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed_dec = end_dec - start_dec;
    printf("Decode finished in %.2f ms.\n", elapsed_dec.count());

    // 5. Final check (against original input)
    printf("Verifying...\n");
    if (memcmp(in_bytes, dec_bytes, in_size) == 0) {
        printf("SUCCESS: Decoded data matches original.\n");
    } else {
        printf("FAILURE: Decoded data does not match original!\n");
    }

    delete[] out_buf;
    delete[] dec_bytes;
    delete[] in_bytes;

    return 0;
}