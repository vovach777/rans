// Simple byte-aligned rANS encoder/decoder - public domain - Fabian 'ryg' Giesen 2014
//
// Not intended to be "industrial strength"; just meant to illustrate the general
// idea.

#pragma once
#include <cstdint>
#include <cassert>
#include <functional>
#define RansAssert assert

// READ ME FIRST:
//
// This is designed like a typical arithmetic coder API, but there's three
// twists you absolutely should be aware of before you start hacking:
//
// 1. You need to encode data in *reverse* - last symbol first. rANS works
//    like a stack: last in, first out.
// 2. Likewise, the encoder outputs bytes *in reverse* - that is, you give
//    it a pointer to the *end* of your buffer (exclusive), and it will
//    slowly move towards the beginning as more bytes are emitted.
// 3. Unlike basically any other entropy coder implementation you might
//    have used, you can interleave data from multiple independent rANS
//    encoders into the same bytestream without any extra signaling;
//    you can also just write some bytes by yourself in the middle if
//    you want to. This is in addition to the usual arithmetic encoder
//    property of being able to switch models on the fly. Writing raw
//    bytes can be useful when you have some data that you know is
//    incompressible, and is cheaper than going through the rANS encode
//    function. Using multiple rANS coders on the same byte stream wastes
//    a few bytes compared to using just one, but execution of two
//    independent encoders can happen in parallel on superscalar and
//    Out-of-Order CPUs, so this can be *much* faster in tight decoding
//    loops.
//
//    This is why all the rANS functions take the write pointer as an
//    argument instead of just storing it in some context struct.

// --------------------------------------------------------------------------

// L ('l' in the paper) is the lower bound of our normalization interval.
// Between this and our byte-aligned emission, we use 31 (not 32!) bits.
// This is done intentionally because exact reciprocals for 31-bit uints
// fit in 32-bit uints: this permits some optimizations during encoding.
namespace rANS
{
    // using PutByteReverse = std::function<void(uint8_t)>;
    // using GetByte = std::function<uint8_t()>;
    constexpr uint32_t RANS_BYTE_L = (1u << 23); // lower bound of our normalization interval
    constexpr uint32_t DefaultScaleBits = 14;

    // ---- Stats

    template <uint32_t scale_bits = DefaultScaleBits, int LOG2NSYMS = 8>
    struct SymbolStats
    {
        static constexpr int NSYMS = 1 << LOG2NSYMS;
        static constexpr uint32_t target_total = 1 << scale_bits;

        std::vector<uint32_t> freqs;     // 256
        std::vector<uint32_t> cum_freqs; // 257

        // alias table
        std::vector<uint32_t> divider;     // 256
        std::vector<uint32_t> slot_adjust; // 512
        std::vector<uint32_t> slot_freqs;  // 512
        std::vector<uint8_t> sym_id;       // 512

        // for encoder
        std::vector<uint32_t> alias_remap; // sum of freqs (unlimited =)

        template <typename ByteIter>
        void count_freqs(ByteIter begin, ByteIter end)
        {
            freqs = std::vector<uint32_t>(NSYMS, 0);
            for (auto it = begin; it != end; it++)
            {
                if (freqs[*it]++ == 0x1000000)
                {
                    for (int j = 0; j < NSYMS; j++)
                    {
                        freqs[j] = (freqs[j] + 1) >> 1;
                    }
                }
            }
        }
        template <typename Iter>
        void load_freqs(Iter begin, Iter end)
        {
            freqs = std::vector<uint32_t>(begin,end);
            freqs.resize(NSYMS,0);
            calc_cum_freqs();
            if (cum_freqs[NSYMS] != target_total)
                throw std::runtime_error("Bad normalized cumulative frequencies!");
        }

        void calc_cum_freqs()
        {
            cum_freqs = std::vector<uint32_t>(NSYMS + 1, 0);
            for (int i = 0; i < NSYMS; i++)
            {
                cum_freqs[i + 1] = cum_freqs[i] + freqs[i];
            }
        }

        void normalize_freqs()
        {
            assert(target_total >= NSYMS);

            calc_cum_freqs();
            uint32_t cur_total = cum_freqs[NSYMS];

            // resample distribution based on cumulative freqs
            for (int i = 1; i <= NSYMS; i++)
                cum_freqs[i] = ((uint64_t)target_total * cum_freqs[i]) / cur_total;

            // if we nuked any non-0 frequency symbol to 0, we need to steal
            // the range to make the frequency nonzero from elsewhere.
            //
            // this is not at all optimal, i'm just doing the first thing that comes to mind.
            for (int i = 0; i < NSYMS; i++)
            {
                if (freqs[i] && cum_freqs[i + 1] == cum_freqs[i])
                {
                    // symbol i was set to zero freq

                    // find best symbol to steal frequency from (try to steal from low-freq ones)
                    uint32_t best_freq = ~0u;
                    int best_steal = -1;
                    for (int j = 0; j < NSYMS; j++)
                    {
                        uint32_t freq = cum_freqs[j + 1] - cum_freqs[j];
                        if (freq > 1 && freq < best_freq)
                        {
                            best_freq = freq;
                            best_steal = j;
                        }
                    }
                    assert(best_steal != -1);

                    // and steal from it!
                    if (best_steal < i)
                    {
                        for (int j = best_steal + 1; j <= i; j++)
                            cum_freqs[j]--;
                    }
                    else
                    {
                        assert(best_steal > i);
                        for (int j = i + 1; j <= best_steal; j++)
                            cum_freqs[j]++;
                    }
                }
            }

            // calculate updated freqs and make sure we didn't screw anything up
            assert(cum_freqs[0] == 0 && cum_freqs[NSYMS] == target_total);
            for (int i = 0; i < NSYMS; i++)
            {
                if (freqs[i] == 0)
                    assert(cum_freqs[i + 1] == cum_freqs[i]);
                else
                    assert(cum_freqs[i + 1] > cum_freqs[i]);

                // calc updated freq
                freqs[i] = cum_freqs[i + 1] - cum_freqs[i];
            }
        }

        // Set up the alias table.
        void make_alias_table()
        {
            // verify that our distribution sum divides the number of buckets
            uint32_t sum = cum_freqs[NSYMS];
            assert(sum != 0 && (sum % NSYMS) == 0);
            assert(sum >= NSYMS);

            // target size in every bucket
            uint32_t tgt_sum = sum / NSYMS;

            // okay, prepare a sweep of vose's algorithm to distribute
            // the symbols into buckets
            auto remaining = freqs;
            divider = std::vector(NSYMS, tgt_sum);
            sym_id = std::vector(NSYMS * 2, uint8_t{});

            for (int i = 0; i < NSYMS; i++)
            {
                sym_id[i * 2 + 0] = i;
                sym_id[i * 2 + 1] = i;
            }

            // a "small" symbol is one with less than tgt_sum slots left to distribute
            // a "large" symbol is one with >=tgt_sum slots.
            // find initial small/large buckets
            int cur_large = 0;
            int cur_small = 0;
            while (cur_large < NSYMS && remaining[cur_large] < tgt_sum)
                cur_large++;
            while (cur_small < NSYMS && remaining[cur_small] >= tgt_sum)
                cur_small++;

            // cur_small is definitely a small bucket
            // next_small *might* be.
            int next_small = cur_small + 1;

            // top up small buckets from large buckets until we're done
            // this might turn the large bucket we stole from into a small bucket itself.
            while (cur_large < NSYMS && cur_small < NSYMS)
            {
                // this bucket is split between cur_small and cur_large
                sym_id[cur_small * 2 + 0] = cur_large;
                divider[cur_small] = remaining[cur_small];

                // take the amount we took out of cur_large's bucket
                remaining[cur_large] -= tgt_sum - divider[cur_small];

                // if the large bucket is still large *or* we haven't processed it yet...
                if (remaining[cur_large] >= tgt_sum || next_small <= cur_large)
                {
                    // find the next small bucket to process
                    cur_small = next_small;
                    while (cur_small < NSYMS && remaining[cur_small] >= tgt_sum)
                        cur_small++;
                    next_small = cur_small + 1;
                }
                else // the large bucket we just made small is behind us, need to back-track
                    cur_small = cur_large;

                // if cur_large isn't large anymore, forward to a bucket that is
                while (cur_large < NSYMS && remaining[cur_large] < tgt_sum)
                    cur_large++;
            }

            // okay, we now have our alias mapping; distribute the code slots in order
            auto assigned = std::vector(NSYMS, uint32_t{0});
            alias_remap = std::vector(sum, uint32_t{});
            slot_adjust = slot_freqs = std::vector(NSYMS * 2, uint32_t{0});

            for (int i = 0; i < NSYMS; i++)
            {
                int j = sym_id[i * 2 + 0];
                uint32_t sym0_height = divider[i];
                uint32_t sym1_height = tgt_sum - divider[i];
                uint32_t base0 = assigned[i];
                uint32_t base1 = assigned[j];
                uint32_t cbase0 = cum_freqs[i] + base0;
                uint32_t cbase1 = cum_freqs[j] + base1;

                divider[i] = i * tgt_sum + sym0_height;

                slot_freqs[i * 2 + 1] = freqs[i];
                slot_freqs[i * 2 + 0] = freqs[j];
                slot_adjust[i * 2 + 1] = i * tgt_sum - base0;
                slot_adjust[i * 2 + 0] = i * tgt_sum - (base1 - sym0_height);
                for (uint32_t k = 0; k < sym0_height; k++)
                    alias_remap[cbase0 + k] = k + i * tgt_sum;
                for (uint32_t k = 0; k < sym1_height; k++)
                    alias_remap[cbase1 + k] = (k + sym0_height) + i * tgt_sum;
                assigned[i] += sym0_height;
                assigned[j] += sym1_height;
            }

            // check that each symbol got the number of slots it needed
            for (int i = 0; i < NSYMS; i++)
                assert(assigned[i] == freqs[i]);
        }
    };

    // State for a rANS encoder. Yep, that's all there is to it.
    template <uint32_t scale_bits = DefaultScaleBits>
    struct State
    {
        uint32_t r{RANS_BYTE_L}; // Initialize a rANS encoder.

        // Renormalize the encoder. Internal function.

        template <typename PutByteReverse>
        inline void RansEncRenorm(PutByteReverse&& putByteReverse, uint32_t freq)
        {
            uint32_t x_max = ((RANS_BYTE_L >> scale_bits) << 8) * freq; // this turns into a shift.
            if (r >= x_max)
            {
                do
                {
                    putByteReverse((r & 0xff));
                    r >>= 8;
                } while (r >= x_max);
            }
        }

        // Encodes a single symbol with range start "start" and frequency "freq".
        // All frequencies are assumed to sum to "1 << scale_bits", and the
        // resulting bytes get written to ptr (which is updated).
        //
        // NOTE: With rANS, you need to encode symbols in *reverse order*, i.e. from
        // beginning to end! Likewise, the output bytestream is written *backwards*:
        // ptr starts pointing at the end of the output buffer and keeps decrementing.

        template <typename PutByteReverse>
        inline void RansEncPut(PutByteReverse&& putByteReverse, uint32_t start, uint32_t freq)
        {
            // renormalize
            RansEncRenorm(putByteReverse, freq);

            // x = C(s,x)
            r = ((r / freq) << scale_bits) + (r % freq) + start;
        }

        // Flushes the rANS encoder.
        template <typename PutByteReverse>
        inline void RansEncFlush(PutByteReverse&& putByteReverse)
        {
            putByteReverse(uint8_t(r >> 24));
            putByteReverse(uint8_t(r >> 16));
            putByteReverse(uint8_t(r >> 8));
            putByteReverse(uint8_t(r));
        }

        // Initializes a rANS decoder.
        // Unlike the encoder, the decoder works forwards as you'd expect.
        template <typename GetByte>
        inline void RansDecInit(GetByte&& getByte)
        {
            r = getByte() << 0;
            r |= getByte() << 8;
            r |= getByte() << 16;
            r |= getByte() << 24;
        }

        // Returns the current cumulative frequency (map it to a symbol yourself!)
        inline uint32_t RansDecGet()
        {
            return r & ((1u << scale_bits) - 1);
        }

        // Advances in the bit stream by "popping" a single symbol with range start
        // "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits",
        // and the resulting bytes get written to ptr (which is updated).
        template <typename GetByte>
        inline void RansDecAdvance(GetByte&& getByte, uint32_t start, uint32_t freq)
        {
            uint32_t mask = (1u << scale_bits) - 1;

            // s, x = D(x)
            r = freq * (r >> scale_bits) + (r & mask) - start;

            // renormalize
            if (r < RANS_BYTE_L)
            {
                do
                {
                    r <<= 8;
                    r |= getByte();
                } while (r < RANS_BYTE_L);
            }
        }

        // Advances in the bit stream by "popping" a single symbol with range start
        // "start" and frequency "freq". All frequencies are assumed to sum to "1 << scale_bits".
        // No renormalization or output happens.
        inline void RansDecAdvanceStep(uint32_t start, uint32_t freq)
        {
            uint32_t mask = (1u << scale_bits) - 1;

            // s, x = D(x)
            r = freq * (r >> scale_bits) + (r & mask) - start;
        }

        // Renormalize.
        template <typename GetByte>
        inline void RansDecRenorm(GetByte&& getByte)
        {
            // renormalize
            if (r < RANS_BYTE_L)
            {
                do
                {
                    r <<= 8;
                    r |= getByte();
                } while (r < RANS_BYTE_L);
            }
        }

        // ---- rANS encoding/decoding with alias table
        template <int LOG2NSYMS, typename PutByteReverse>
        inline void RansEncPutAlias(PutByteReverse&& putByteReverse, const SymbolStats<scale_bits, LOG2NSYMS> &syms, int s)
        {
            // renormalize
            uint32_t freq = syms.freqs[s];
            RansEncRenorm(std::forward<PutByteReverse>(putByteReverse), freq);

            // x = C(s,x)
            // NOTE: alias_remap here could be replaced with e.g. a binary search.
            r = ((r / freq) << scale_bits) + syms.alias_remap[(r % freq) + syms.cum_freqs[s]];
        }

        template <int LOG2NSYMS>
        inline uint32_t RansDecGetAlias(const SymbolStats<scale_bits, LOG2NSYMS> &syms)
        {
            // figure out symbol via alias table
            uint32_t mask = (1u << scale_bits) - 1; // constant for fixed scale_bits!
            uint32_t xm = r & mask;
            uint32_t bucket_id = xm >> (scale_bits - LOG2NSYMS);
            uint32_t bucket2 = bucket_id * 2;
            if (xm < syms.divider[bucket_id])
                bucket2++;

            // s, x = D(x)
            r = syms.slot_freqs[bucket2] * (r >> scale_bits) + xm - syms.slot_adjust[bucket2];
            return syms.sym_id[bucket2];
        }
    };

}
