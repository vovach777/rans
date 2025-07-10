#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <cstdint>
#include <functional>
#include <array>
#include <algorithm>
#include <numeric>
#include <execution>
#include <filesystem>
namespace fs = std::filesystem;
#include <immintrin.h>
#include "mio.hpp"
#include "myargs.hpp"
#include "profiling.hpp"
#include "rans.hpp"

inline void createEmptyFile(const char * fileName) {
    FILE *file = fopen(fileName, "wb");
    if (!file) {
        std::cerr << "Error creating file: " << fileName << std::endl;
        throw std::runtime_error("Failed to create file");
    }
    fclose(file);
}

inline void extendFile(const char * fileName, int64_t fileSize) {
    fs::resize_file(fileName, fileSize);
}

void simd_reverse(uint8_t* begin, uint8_t* end) {

    const __m128i reverse_mask = _mm_setr_epi8(
        15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
    );

    while (end - begin >= 32) {
        end -= 16;

        __m128i lo = _mm_loadu_si128((__m128i*)(begin));
        __m128i hi = _mm_loadu_si128((__m128i*)(end));

        lo = _mm_shuffle_epi8(lo, reverse_mask);
        hi = _mm_shuffle_epi8(hi, reverse_mask);

        _mm_storeu_si128((__m128i*)(begin), hi);
        _mm_storeu_si128((__m128i*)(end), lo);

        begin += 16;
    }

    // Хвост добиваем скалярно
    std::reverse(begin,end);
}


using myargs::args;
int main(int argc, char** argv) {

    srand(static_cast<unsigned int>(std::time(nullptr)));
    args.parse(argc, argv);
    if (args.size() < 2) {
         std::cerr << "Usage: " << args[0] << " <filename>" << std::endl;
         return 1;
    }
    auto is_decoder = args.has('d') || args.has("decode");
    auto filename = args.size() >= 3 ? args[2] : args[1] + ( is_decoder ? ".orig" : ".rans" );
try {
    std::error_code error;

    profiling::StopWatch sw;
    profiling::StopWatch cherry_pick_sw;
    sw.start();

    auto mmap = mio::make_mmap<mio::ummap_source>(args[1], 0, 0, error);

    if (error)
    {
        std::cerr << error.message() << std::endl;
        return 1;
    }

    if (mmap.size()==0) {
        return 0;
    }

    auto is_benchmark = args.has('b') || args.has("benchmark");

    constexpr int64_t PgSize = 4 << 20;
    createEmptyFile(filename.data());
    extendFile(filename.data(),PgSize);
    int64_t base = 0;
    auto rw_mmap = mio::make_mmap<mio::ummap_sink>(filename, base, PgSize, error);
    if (error)
    {
        throw std::runtime_error(error.message());
    }
    auto dest_begin = rw_mmap.begin();
    auto dest = dest_begin;
    auto dest_end = rw_mmap.end();
    auto src_begin = mmap.begin();
    auto src_end = mmap.end();
    auto src = src_begin;
    auto src_size = src_end - src_begin;
    auto print_progress = [&](){
        auto src_size = is_decoder ? std::distance(src_begin, src) : std::distance(src, src_end);
        auto dest_size = base + (dest - dest_begin);
        std::cout << "\r                \r" << std::dec << src_size << " -> " << dest_size
                  << std::fixed << std::setprecision(2) << " (" << (dest_size * 100. / src_size) << "%)"
                  << " @ " << std::fixed << std::setprecision(2) << src_size / (1024. * 1024.)   / sw.elapsed() << " Mb/sec"
                << std::flush;
    };

    auto check_expand = [&](int64_t chk_size, int64_t backwindow, int64_t PgSize) {
        if (dest + chk_size > dest_end) {
            if (PgSize < 1)
                throw std::logic_error("PgSize must be greater than 0");
            base += (dest-dest_begin); //seek base to dest_end of current data
            backwindow = base >= backwindow ? backwindow : backwindow - base;
            base -= backwindow; //move base to the beginning of the file to keep backwindow
            auto size = (chk_size + backwindow + PgSize-1) & ~(PgSize - 1);
            rw_mmap.unmap();
            extendFile(filename.data(), base + size);
            rw_mmap = mio::make_mmap<mio::ummap_sink>(filename, base, size, error);
            if (error)
            {
                throw std::runtime_error(error.message());
            }
            dest_begin = rw_mmap.begin();
            dest = dest_begin  + backwindow;
            dest_end = rw_mmap.end();
            print_progress();
        }
    };
    constexpr uint32_t prob_bits = 12;

    if (is_decoder) {
    // ----- Декодер rANS с alias-таблицей -----

        int64_t in_bytes = reinterpret_cast<const uint64_t*>(src)[0];
        src += 8;
        check_expand(std::min(PgSize,in_bytes), 0, 1);

        // Считать cum_freqs (256 * 2 байта)
        rANS::SymbolStats<prob_bits, 8> stats;
        stats.load_freqs(reinterpret_cast<const uint16_t*>(src), reinterpret_cast<const uint16_t*>(src)+256);
        src += 256 * 2;
        stats.make_alias_table();

        // Инициализируем декодер
        rANS::State<prob_bits> state;
        auto getbyte = [&]() -> uint8_t {
            if (src == src_end) {
                throw std::logic_error("EOF");
            }
            return *src++;
        };
        state.RansDecInit(getbyte);
        cherry_pick_sw.start();
        for (size_t i = 0; i < in_bytes; i++) {
            uint8_t sym = state.RansDecGetAlias(stats);
            if (dest == dest_end) {
                cherry_pick_sw.stop();
                //размер выходного файла известен заранее, поэтому расширяем его только на PgSize байт
                if (in_bytes-i <= PgSize) {
                    check_expand(in_bytes-i, 0, 1);
                } else {
                    check_expand(1, 0, PgSize);
                }
                cherry_pick_sw.start();
            }
            *dest++ = sym;
            state.RansDecRenorm(getbyte);
        }
        cherry_pick_sw.stop();
    } else {
        static_assert(prob_bits <= 16, "prob_bits must be <= 16");
        static_assert(prob_bits >= 8, "prob_bits must be >= 8");
        rANS::SymbolStats<prob_bits,8> stats;
        rANS::State<prob_bits> state;
        stats.count_freqs(src, src_end);
        stats.normalize_freqs();
        stats.make_alias_table();

        check_expand(8,0,PgSize);
        reinterpret_cast<uint64_t*>(dest)[0] = src_end-src;
        dest += 8;
        //freq table to reconstruct on decoder
        check_expand(256*2,0,PgSize);
        for (int i=0; i < 256; i++) {
            reinterpret_cast<uint16_t*>(dest)[i] = stats.freqs[i];
            std::cout << stats.freqs[i] << " ";
        }
        std::cout << std::endl;
        dest += 256*2;
        auto rans_begin = dest;

        auto putbyte = [&](uint8_t byte) {
            if (dest == dest_end) {
                cherry_pick_sw.stop();
                check_expand(1,0,PgSize);
                cherry_pick_sw.start();
            }
            *dest++ = byte;
        };
        auto last = src_end;
        cherry_pick_sw.start();
        for (src = src_end; src != src_begin;) {
            state.RansEncPutAlias(putbyte, stats, *--src);
        }
        state.RansEncFlush(putbyte);
        cherry_pick_sw.stop();

    }
    base += (dest - dest_begin);
    rw_mmap.unmap();
    mmap.unmap();
    extendFile(filename.data(), base);
    if (!is_decoder) {
        print_progress();
        rw_mmap = mio::make_mmap<mio::ummap_sink>(filename, 0, base, error);
        if (error)
        {
            throw std::runtime_error(error.message());
        }
        simd_reverse(rw_mmap.data() + 8 + 256*2, rw_mmap.data() + base);
        rw_mmap.unmap();
    }
    sw.stop();
    std::cout << "\r                         \r";
    std::cout << std::dec << (is_decoder ? "Decompression" : "Compression") <<  " time (speed): " << std::fixed << std::setprecision(3) << sw.elapsed()  << " (" << std::fixed << std::setprecision(2) << src_size / (1024. * 1024.)   / sw.elapsed() << " Mb/sec)" << std::endl;
    std::cout << std::dec << "Cherry pick time (speed): " << std::fixed << std::setprecision(3) << cherry_pick_sw.elapsed()  << " (" << std::fixed << std::setprecision(2) << src_size / (1024. * 1024.)   / cherry_pick_sw.elapsed() << " Mb/sec)" << std::endl;
    std::cout << std::dec <<  src_size << " -> " << base << std::fixed << std::setprecision(2) << " (" << (base * 100. / src_size) << "%)" << std::endl;
}
catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    remove(filename.c_str());
    return 1;
}
    return 0;

}