#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <chrono>
#include "rans_byte.h"

static void panic(const char *fmt, ...) {
    va_list arg;
    va_start(arg, fmt);
    fputs("Error: ", stderr);
    vfprintf(stderr, fmt, arg);
    va_end(arg);
    fputs("\n", stderr);
    exit(1);
}

static uint8_t* read_file(char const* filename, size_t* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f)
        panic("file not found: %s\n", filename);
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t* buf = new uint8_t[size];
    if (fread(buf, size, 1, f) != 1)
        panic("read failed\n");
    
    fclose(f);
    
    if (out_size)
        *out_size = size;
    
    return buf;
}

// ---- Stats
struct SymbolStats {
    static const int LOG2NSYMS = 8;
    static const int NSYMS = 1 << LOG2NSYMS;
    
    uint32_t freqs[NSYMS];
    uint32_t cum_freqs[NSYMS + 1];
    
    // alias table
    uint32_t divider[NSYMS];
    uint32_t slot_adjust[NSYMS*2];
    uint32_t slot_freqs[NSYMS*2];
    uint8_t sym_id[NSYMS*2];
    
    // for encoder
    uint32_t* alias_remap;
    
    SymbolStats() : alias_remap(0) {}
    ~SymbolStats() { delete[] alias_remap; }
    
    void count_freqs(uint8_t const* in, size_t nbytes);
    void calc_cum_freqs();
    void normalize_freqs(uint32_t target_total);
    void make_alias_table();
};

void SymbolStats::count_freqs(uint8_t const* in, size_t nbytes) {
    for (int i=0; i < NSYMS; i++)
        freqs[i] = 0;
    
    for (size_t i=0; i < nbytes; i++)
        freqs[in[i]]++;
}

void SymbolStats::calc_cum_freqs() {
    cum_freqs[0] = 0;
    for (int i=0; i < NSYMS; i++)
        cum_freqs[i+1] = cum_freqs[i] + freqs[i];
}

void SymbolStats::normalize_freqs(uint32_t target_total) {
    assert(target_total >= NSYMS);
    
    calc_cum_freqs();
    uint32_t cur_total = cum_freqs[NSYMS];
    
    // resample distribution based on cumulative freqs
    for (int i = 1; i <= NSYMS; i++)
        cum_freqs[i] = ((uint64_t)target_total * cum_freqs[i])/cur_total;
    
    for (int i=0; i < NSYMS; i++) {
        if (freqs[i] && cum_freqs[i+1] == cum_freqs[i]) {
            uint32_t best_freq = ~0u;
            int best_steal = -1;
            
            for (int j=0; j < NSYMS; j++) {
                uint32_t freq = cum_freqs[j+1] - cum_freqs[j];
                if (freq > 1 && freq < best_freq) {
                    best_freq = freq;
                    best_steal = j;
                }
            }
            assert(best_steal != -1);
            
            if (best_steal < i) {
                for (int j = best_steal + 1; j <= i; j++)
                    cum_freqs[j]--;
            } else {
                assert(best_steal > i);
                for (int j = i + 1; j <= best_steal; j++)
                    cum_freqs[j]++;
            }
        }
    }
    
    assert(cum_freqs[0] == 0 && cum_freqs[NSYMS] == target_total);
    for (int i=0; i < NSYMS; i++) {
        if (freqs[i] == 0)
            assert(cum_freqs[i+1] == cum_freqs[i]);
        else
            assert(cum_freqs[i+1] > cum_freqs[i]);
        
        freqs[i] = cum_freqs[i+1] - cum_freqs[i];
    }
}

void SymbolStats::make_alias_table() {
    uint32_t sum = cum_freqs[NSYMS];
    assert(sum != 0 && (sum % NSYMS) == 0);
    assert(sum >= NSYMS);
    
    uint32_t tgt_sum = sum / NSYMS;
    
    uint32_t remaining[NSYMS];
    for (int i=0; i < NSYMS; i++) {
        remaining[i] = freqs[i];
        divider[i] = tgt_sum;
        sym_id[i*2 + 0] = i;
        sym_id[i*2 + 1] = i;
    }
    
    int cur_large = 0;
    int cur_small = 0;
    while (cur_large < NSYMS && remaining[cur_large] < tgt_sum)
        cur_large++;
    while (cur_small < NSYMS && remaining[cur_small] >= tgt_sum)
        cur_small++;
    
    int next_small = cur_small + 1;
    
    while (cur_large < NSYMS && cur_small < NSYMS) {
        sym_id[cur_small*2 + 0] = cur_large;
        divider[cur_small] = remaining[cur_small];
        
        remaining[cur_large] -= tgt_sum - divider[cur_small];
        
        if (remaining[cur_large] >= tgt_sum || next_small <= cur_large) {
            cur_small = next_small;
            while (cur_small < NSYMS && remaining[cur_small] >= tgt_sum)
                cur_small++;
            next_small = cur_small + 1;
        } else
            cur_small = cur_large;
        
        while (cur_large < NSYMS && remaining[cur_large] < tgt_sum)
            cur_large++;
    }
    
    uint32_t assigned[NSYMS] = { 0 };
    alias_remap = new uint32_t[sum];
    
    for (int i=0; i < NSYMS; i++) {
        int j = sym_id[i*2 + 0];
        uint32_t sym0_height = divider[i];
        uint32_t sym1_height = tgt_sum - divider[i];
        
        uint32_t base0 = assigned[i];
        uint32_t base1 = assigned[j];
        
        uint32_t cbase0 = cum_freqs[i] + base0;
        uint32_t cbase1 = cum_freqs[j] + base1;
        
        divider[i] = i*tgt_sum + sym0_height;
        
        slot_freqs[i*2 + 1] = freqs[i];
        slot_freqs[i*2 + 0] = freqs[j];
        
        slot_adjust[i*2 + 1] = i*tgt_sum - base0;
        slot_adjust[i*2 + 0] = i*tgt_sum - (base1 - sym0_height);
        
        for (uint32_t k=0; k < sym0_height; k++)
            alias_remap[cbase0 + k] = k + i*tgt_sum;
        
        for (uint32_t k=0; k < sym1_height; k++)
            alias_remap[cbase1 + k] = (k + sym0_height) + i*tgt_sum;
        
        assigned[i] += sym0_height;
        assigned[j] += sym1_height;
    }
    
    for (int i=0; i < NSYMS; i++)
        assert(assigned[i] == freqs[i]);
}

static inline void RansEncPutAlias(RansState* r, uint8_t** pptr, SymbolStats* const syms, int s, uint32_t scale_bits) {
    uint32_t freq = syms->freqs[s];
    RansState x = RansEncRenorm(*r, pptr, freq, scale_bits);
    *r = ((x / freq) << scale_bits) + syms->alias_remap[(x % freq) + syms->cum_freqs[s]];
}

static inline uint32_t RansDecGetAlias(RansState* r, SymbolStats* const syms, uint32_t scale_bits) {
    RansState x = *r;
    uint32_t mask = (1u << scale_bits) - 1;
    uint32_t xm = x & mask;
    uint32_t bucket_id = xm >> (scale_bits - SymbolStats::LOG2NSYMS);
    uint32_t bucket2 = bucket_id * 2;
    if (xm < syms->divider[bucket_id])
        bucket2++;
    
    *r = syms->slot_freqs[bucket2] * (x >> scale_bits) + xm - syms->slot_adjust[bucket2];
    return syms->sym_id[bucket2];
}

int main() {
    size_t in_size;
    uint8_t* in_bytes = read_file("book1", &in_size);
    
    static const uint32_t prob_bits = 16;
    static const uint32_t prob_scale = 1 << prob_bits;
    
    SymbolStats stats;
    stats.count_freqs(in_bytes, in_size);
    stats.normalize_freqs(prob_scale);
    stats.make_alias_table();
    
    static size_t out_max_size = 32<<20; // 32MB
    uint8_t* out_buf = new uint8_t[out_max_size];
    uint8_t* dec_bytes = new uint8_t[in_size];
    
    uint8_t *rans_begin;
    
    // ---- regular rANS encode/decode. Typical usage.
    memset(dec_bytes, 0xcc, in_size);
    printf("rANS encode:\n");
    for (int run=0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        RansState rans;
        RansEncInit(&rans);
        
        uint8_t* ptr = out_buf + out_max_size;
        for (size_t i=in_size; i > 0; i--) {
            int s = in_bytes[i-1];
            RansEncPutAlias(&rans, &ptr, &stats, s, prob_bits);
        }
        
        RansEncFlush(&rans, &ptr);
        rans_begin = ptr;
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    printf("rANS: %d bytes\n", (int) (out_buf + out_max_size - rans_begin));
    
    // try rANS decode
    for (int run=0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        RansState rans;
        uint8_t* ptr = rans_begin;
        RansDecInit(&rans, &ptr);
        
        for (size_t i=0; i < in_size; i++) {
            uint32_t s = RansDecGetAlias(&rans, &stats, prob_bits);
            dec_bytes[i] = (uint8_t) s;
            RansDecRenorm(&rans, &ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");
    
    // ---- interleaved rANS encode/decode.
    memset(dec_bytes, 0xcc, in_size);
    
    printf("\ninterleaved rANS encode:\n");
    for (int run=0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        RansState rans0, rans1;
        RansEncInit(&rans0);
        RansEncInit(&rans1);
        
        uint8_t* ptr = out_buf + out_max_size;
        
        if (in_size & 1) {
            int s = in_bytes[in_size - 1];
            RansEncPutAlias(&rans0, &ptr, &stats, s, prob_bits);
        }
        
        for (size_t i=(in_size & ~1); i > 0; i -= 2) {
            int s1 = in_bytes[i-1];
            int s0 = in_bytes[i-2];
            RansEncPutAlias(&rans1, &ptr, &stats, s1, prob_bits);
            RansEncPutAlias(&rans0, &ptr, &stats, s0, prob_bits);
        }
        
        RansEncFlush(&rans1, &ptr);
        RansEncFlush(&rans0, &ptr);
        rans_begin = ptr;
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    printf("interleaved rANS: %d bytes\n", (int) (out_buf + out_max_size - rans_begin));
    
    // try interleaved rANS decode
    for (int run=0; run < 5; run++) {
        auto start = std::chrono::high_resolution_clock::now();
        
        RansState rans0, rans1;
        uint8_t* ptr = rans_begin;
        RansDecInit(&rans0, &ptr);
        RansDecInit(&rans1, &ptr);
        
        for (size_t i=0; i < (in_size & ~1); i += 2) {
            uint32_t s0 = RansDecGetAlias(&rans0, &stats, prob_bits);
            uint32_t s1 = RansDecGetAlias(&rans1, &stats, prob_bits);
            dec_bytes[i+0] = (uint8_t) s0;
            dec_bytes[i+1] = (uint8_t) s1;
            RansDecRenorm(&rans0, &ptr);
            RansDecRenorm(&rans1, &ptr);
        }
        
        if (in_size & 1) {
            uint32_t s0 = RansDecGetAlias(&rans0, &stats, prob_bits);
            dec_bytes[in_size - 1] = (uint8_t) s0;
            RansDecRenorm(&rans0, &ptr);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = end - start;
        printf("Elapsed: %.2f ms, %.1f symbols/ms (%5.1fMiB/s)\n",
            elapsed_ms.count(),
            in_size / elapsed_ms.count() / 1000.0,
            (in_size / (1024.0 * 1024.0)) / (elapsed_ms.count() / 1000.0));
    }
    
    if (memcmp(in_bytes, dec_bytes, in_size) == 0)
        printf("decode ok!\n");
    else
        printf("ERROR: bad decoder!\n");
    
    delete[] out_buf;
    delete[] dec_bytes;
    delete[] in_bytes;
    
    return 0;
}