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
#include "../include/rans.hpp"

static uint8_t* read_file(char const* filename, size_t* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f)
        return 0;

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = new uint8_t[size];
    if (fread(buf, size, 1, f) != 1)
    {
        fclose(f);
        delete[] buf;
        return 0;
    }

    fclose(f);

    if (out_size)
        *out_size = size;

    return buf;
}

int main()
{
    size_t in_size;
    uint8_t* in_bytes = read_file("book1", &in_size);
    if (!in_bytes) {
        fprintf(stderr, "Error: could not read book1\n");
        return 1;
    }

    static const uint32_t prob_bits = 16;
    rANS::SymbolStats<prob_bits, 8> stats;
    stats.count_freqs(in_bytes, in_bytes + in_size);
    stats.normalize_freqs();
    stats.make_alias_table();

    static size_t out_max_size = 32 << 20; // 32MB
    uint8_t* out_buf = new uint8_t[out_max_size];
    uint8_t* dec_bytes = new uint8_t[in_size];

    // try rANS encode
    uint8_t* rans_begin;

    // ---- regular rANS encode/decode. Typical usage.
    memset(dec_bytes, 0xcc, in_size);
    printf("rANS encode:\n");
    for (int run = 0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();

        rANS::State<prob_bits> rans;
        uint8_t* ptr = out_buf + out_max_size; // *end* of output buffer

        auto put_byte = [&](uint8_t byte) {
            *--ptr = byte;
        };

        for (size_t i = in_size; i > 0; i--) { // NB: working in reverse!
            int s = in_bytes[i - 1];
            rans.RansEncPutAlias(put_byte, stats, s);
        }

        rans.RansEncFlush(put_byte);
        rans_begin = ptr;

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    printf("rANS: %d bytes\n", (int)(out_buf + out_max_size - rans_begin));

    // try rANS decode
    for (int run = 0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();

        rANS::State<prob_bits> rans;
        uint8_t* ptr = rans_begin;

        auto get_byte = [&]() -> uint8_t {
            return *ptr++;
        };

        rans.RansDecInit(get_byte);

        for (size_t i = 0; i < in_size; i++) {
            uint32_t s = rans.RansDecGetAlias(stats);
            dec_bytes[i] = (uint8_t)s;
            rans.RansDecRenorm(get_byte);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }

    // check decode results
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");

    // ---- interleaved rANS encode/decode. This is the kind of thing you might do to optimize critical paths.
    memset(dec_bytes, 0xcc, in_size);
    printf("\ninterleaved rANS encode:\n");

    const int NUM_STREAMS = 4;

    for (int run = 0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();

        rANS::State<prob_bits> rans[NUM_STREAMS];
        uint8_t* ptr = out_buf + out_max_size; // *end* of output buffer

        auto put_byte = [&](uint8_t byte) {
            *--ptr = byte;
        };

        size_t i = in_size;

        // Process bytes from the end until the length is a multiple of NUM_STREAMS
        while (i % NUM_STREAMS != 0) {
            --i;
            rans[i % NUM_STREAMS].RansEncPutAlias(put_byte, stats, in_bytes[i]);
        }

        // Main loop processing 4 bytes at a time
        for (; i > 0; i -= NUM_STREAMS) { // NB: working in reverse!
            rans[3].RansEncPutAlias(put_byte, stats, in_bytes[i - 1]);
            rans[2].RansEncPutAlias(put_byte, stats, in_bytes[i - 2]);
            rans[1].RansEncPutAlias(put_byte, stats, in_bytes[i - 3]);
            rans[0].RansEncPutAlias(put_byte, stats, in_bytes[i - 4]);
        }

        for(int j = NUM_STREAMS - 1; j >= 0; --j) {
            rans[j].RansEncFlush(put_byte);
        }
        rans_begin = ptr;

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    printf("interleaved rANS: %d bytes\n", (int)(out_buf + out_max_size - rans_begin));

    // try interleaved rANS decode
    for (int run = 0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();

        rANS::State<prob_bits> rans[NUM_STREAMS];
        uint8_t* ptr = rans_begin;

        auto get_byte = [&]() -> uint8_t {
            return *ptr++;
        };

        for(int j = 0; j < NUM_STREAMS; ++j) {
            rans[j].RansDecInit(get_byte);
        }

        size_t i = 0;
        size_t main_body_size = (in_size / NUM_STREAMS) * NUM_STREAMS;
        for (; i < main_body_size; i += NUM_STREAMS) {
            dec_bytes[i + 0] = (uint8_t)rans[0].RansDecGetAlias(stats);
            dec_bytes[i + 1] = (uint8_t)rans[1].RansDecGetAlias(stats);
            dec_bytes[i + 2] = (uint8_t)rans[2].RansDecGetAlias(stats);
            dec_bytes[i + 3] = (uint8_t)rans[3].RansDecGetAlias(stats);

            rans[0].RansDecRenorm(get_byte);
            rans[1].RansDecRenorm(get_byte);
            rans[2].RansDecRenorm(get_byte);
            rans[3].RansDecRenorm(get_byte);
        }

        // Process remainder
        for (size_t j = 0; j < (in_size % NUM_STREAMS); ++j) {
            dec_bytes[i + j] = (uint8_t)rans[j].RansDecGetAlias(stats);
            rans[j].RansDecRenorm(get_byte);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }

    // check decode results
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");


    delete[] out_buf;
    delete[] dec_bytes;
    delete[] in_bytes;

    return 0;
}
