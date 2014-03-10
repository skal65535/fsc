//Copyright 2014 The FSC Authors. All Rights Reserved.
//
//Licensed under the Apache License, Version 2.0 (the "License");
//you may not use this file except in compliance with the License.
//You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
//------------------------------------------------------------------------------
//
// Finite State Coder (FSC) encoder implementation
//
//  based on Jarek Duda's paper: http://arxiv.org/pdf/1311.2540v1.pdf
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./fsc.h"

#include <stdio.h>
#include <assert.h>
#include <math.h>

#include "./bits.h"

// #define SHOW_SIMULATION

//------------------------------------------------------------------------------
// States and tables

// Analyze counts[] and renormalize with error-diffusion so that
// the total is rescaled to be equal to TAB_SIZE exactly.

#define FIX_BITS 30   // fixed-point precision for correction
#define FIX_ONE (1U << FIX_BITS)
#define DESCALE_BITS (FIX_BITS - log_tab_size)
#define DESCALE_ONE  (1U << DESCALE_BITS)
#define DESCALE_MASK (DESCALE_ONE - 1)

void FSCCountSymbols(const uint8_t* in, size_t in_size, uint32_t counts[MAX_SYMBOLS]) {
  size_t n = 0;
  memset(counts, 0, MAX_SYMBOLS * sizeof(counts[0]));
  for (; n < in_size; ++n) ++counts[in[n]];
}

int FSCNormalizeCounts(uint32_t counts[MAX_SYMBOLS], int max_symbol,
                       int log_tab_size) {
  const int tab_size = 1 << log_tab_size;
  uint64_t total = 0;
  int nb_symbols = 0;
  int n;
  int last_nz = 0;
  for (n = 0; n < max_symbol; ++n) {
    total += counts[n];
    if (counts[n] > 0) {
      ++nb_symbols;
      last_nz = n + 1;
    }
  }
  if (nb_symbols < 1) return 0;   // won't work
  if (log_tab_size > LOG_TAB_SIZE) return 0;
  if (nb_symbols > tab_size) return 0;
  max_symbol = last_nz;

  if (total >= tab_size) {
    if (nb_symbols == tab_size) {
      // corner case of mandatory uniform distribution
      for (n = 0; n < max_symbol; ++n) counts[n] = 1;
      total = nb_symbols;
    } else {
      // Need to prevent some small counts from going to zero
      uint64_t correction = total;
      uint64_t total_correction = 0;
      while (correction != 0) {
        total_correction += correction;
        correction = (correction * nb_symbols) >> log_tab_size;
      }
      total_correction >>= log_tab_size;
      for (n = 0; n < max_symbol; ++n) {
        if (counts[n]) counts[n] += total_correction;
      }
      total += total_correction * nb_symbols;
    }
  }
  {
    uint32_t sum = 0;
    const int32_t mult = FIX_ONE / total;   // multiplier
    const int32_t error = FIX_ONE % total;
    int32_t cumul = (error < DESCALE_ONE) ? (DESCALE_ONE + error) >> 1
                                           : error;
    for (n = 0; n < max_symbol; ++n) {
      if (counts[n] > 0) {
        int64_t c = (int64_t)counts[n] * mult + cumul;
        counts[n] = c >> DESCALE_BITS;
        cumul = c & DESCALE_MASK;
        if (counts[n] <= 0) {
//          fprintf(stderr, "Normalization problem. "
//                          "log_tab_size may be too small.\n");
          counts[n] = 1;
          cumul -= DESCALE_ONE;
        }
        sum += counts[n];
      }
    }
    if (sum != tab_size) {
      fprintf(stderr, "Normalization error!! %d / %d\n", sum, tab_size);
      return 0;
    }
  }
  return max_symbol;
}

// -----------------------------------------------------------------------------

typedef struct {  // for delayed bitstream writing
  uint16_t val_;
  uint8_t  nb_bits_;
} token_t;

typedef struct {
  int32_t offset_;
  uint16_t wrap_;
  uint8_t  nb_bits_;
} transf_t;

typedef struct {
  int max_symbol_;
  uint16_t states_[TAB_SIZE];
  transf_t transforms_[MAX_SYMBOLS];
  size_t in_size_;
  int log_tab_size_;
} FSCEncoder;

static int Log2Ceil(uint32_t v) {   // not a critical function
  if (v > 1) {
    int s = 31;
    while (v <= (1U << s)) --s;
    return s + 1;
  } else {
    return 0;
  }
}

// -----------------------------------------------------------------------------

#define MAX_INSERT_ITERATION 0   // limit bucket-sort complexity (0=off)

// insert with limited bucket sort
#define INSERT(s, key) do {                      \
  const double k = (key);                        \
  const int b = (int)(k);                        \
  if (b < tab_size) {                            \
    const int S = (s);                           \
    int16_t* p = &buckets[b];                    \
    int M = MAX_INSERT_ITERATION;                \
    while (M-- && *p != -1 && keys[*p] < k) {    \
      p = &next[*p];                             \
    }                                            \
    next[S] = *p;                                \
    *p = S;                                      \
    keys[S] = k;                                 \
  }                                              \
} while (0)

void BuildSpreadTableBucket(int max_symbol, const uint32_t counts[],
                            int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  int s, n, pos;
  int16_t  buckets[TAB_SIZE];        // entry to linked list of bucket's symbol
  int16_t  next[MAX_SYMBOLS];        // linked list of symbols in the same bucket
  double keys[MAX_SYMBOLS];           // key associated to each symbol
  for (n = 0; n < tab_size; ++n) {
    buckets[n] = -1;  // NIL
  }
  for (s = 0; s < max_symbol; ++s) {
    if (counts[s] > 0) {
      INSERT(s, 0.5 * tab_size / counts[s]);
    }
  }
  for (n = 0, pos = 0; n < tab_size && pos < tab_size; ++pos) {
    while (1) {
      const int s = buckets[pos];
      if (s < 0) break;
      symbols[n++] = s;
      buckets[pos] = next[s];   // POP s
      INSERT(s, keys[s] + 1. * tab_size / counts[s]);
    }
  }
  // n < tab_size can happen due to rounding errors
  for (; n != tab_size; ++n) symbols[n] = symbols[n - 1];
}

static inline int ReverseBits(int i, int max_bits) {
  const int tab_size = 1 << max_bits;
  int v = 0, n = max_bits;
  while (n-- > 0) {
    v |= (i & 1) << n;
    i >>= 1;
  }
  return v;
}

void BuildSpreadTableReverse(int max_symbol, const uint32_t counts[],
                             int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  int s, n, pos;
  for (s = 0, pos = 0; s < max_symbol; ++s) {
    for (n = 0; n < counts[s]; ++n, ++pos) {
      symbols[ReverseBits(pos, log_tab_size)] = s;
    }
  }
}

void BuildSpreadTableModulo(int max_symbol, const uint32_t counts[],
                             int log_tab_size, uint8_t symbols[]) {
  const int tab_size = 1 << log_tab_size;
  const int kStep = ((tab_size >> 1) + (tab_size >> 3) + 1);
  int s, n, pos;
  for (s = 0, pos = 0; s < max_symbol; ++s) {
    for (n = 0; n < counts[s]; ++n, ++pos) {
      const int v = pos * kStep;
      const int slot = (v ^ CRYPTO_KEY) & (tab_size - 1);
      symbols[slot] = s;
    }
  }
}

void (*BuildSpreadTable_ptr)(int max_symbol, const uint32_t counts[],
                             int log_tab_size, uint8_t symbols[])
    = BuildSpreadTableBucket;

// -----------------------------------------------------------------------------

static int BuildTables(FSCEncoder* const enc, const uint32_t counts[]) {
  int s, pos;
  const int log_tab_size = enc->log_tab_size_;
  const int tab_size = 1 << log_tab_size;
  uint16_t state[MAX_SYMBOLS];
  uint8_t symbols[TAB_SIZE];  // symbols, spread on the [0, TAB_SIZE) interval
  const int max_symbol = enc->max_symbol_;
  uint16_t* const tab = enc->states_;
  transf_t* const transforms = enc->transforms_;

  assert(max_symbol <= MAX_SYMBOLS && max_symbol > 0);

  for (s = 0, pos = 0; s < max_symbol; ++s) {
    int cnt = counts[s];
    // start of Is segment of symbol 's' in the states_ array
    // Length of the Is segment: cnt
    // Sum of all segments = tab_size
    state[s] = pos;
    // We map the [tab_size, 2*tab_size) segment to Is segments
    // and then remap then to I using symbols[]
    if (cnt > 0) {
      transf_t* const t = &transforms[s];
      t->nb_bits_ = log_tab_size - Log2Ceil(cnt);  // log(1/ps)
      t->wrap_ = cnt << (1 + t->nb_bits_);
      t->offset_ = pos - cnt;
      pos += cnt;
    }
  }
  if (pos != tab_size) return 0;   // input not normalized!

  // Prepare map from symbol to state
  BuildSpreadTable_ptr(max_symbol, counts, log_tab_size, symbols);
  for (pos = 0; pos < tab_size; ++pos) {
    const uint8_t s = symbols[pos];
    tab[state[s]++] = pos + tab_size;
  }
  return max_symbol;
}

static int EncoderInit(FSCEncoder* const enc, uint32_t counts[],
                       int max_symbol, int log_tab_size) {
  memset(enc, 0, sizeof(*enc));
  if (max_symbol == 0) max_symbol = MAX_SYMBOLS;
  if (log_tab_size < 1 || log_tab_size > LOG_TAB_SIZE) return 0;
  enc->log_tab_size_ = log_tab_size;
  enc->max_symbol_ = FSCNormalizeCounts(counts, max_symbol, enc->log_tab_size_);
  if (enc->max_symbol_ < 1) return 0;
  if (enc->max_symbol_ > (1 << enc->log_tab_size_)) return 0;
  if (!BuildTables(enc, counts)) return 0;
  return 1;
}

// -----------------------------------------------------------------------------
// Coding loop

static void PutBlock(const FSCEncoder* enc, const uint8_t* in, int size,
                     FSCBitWriter* bw) {
  token_t tokens[BLOCK_SIZE];
  const transf_t* const transforms = enc->transforms_;
  const uint16_t* const states = enc->states_;
  const int log_tab_size = enc->log_tab_size_;
  const int tab_size = 1 << log_tab_size;
  int state = tab_size;
  int k;
  for (k = size - 1; k >= 0; --k) {
    const transf_t* const transf = &transforms[in[k]];
    const int extra_bit = (state >= transf->wrap_);
    const int nb_bits = transf->nb_bits_ + extra_bit;
    tokens[k].nb_bits_ = nb_bits;
    tokens[k].val_ = state & ((1 << nb_bits) - 1);
    state = states[(state >> nb_bits) + transf->offset_];
  }
  // Direction reversal
  FSCWriteBits(bw, log_tab_size, state & (tab_size - 1));
  for (k = 0; k < size - 1; ++k) {   // no need to write the last token
    FSCWriteBits(bw, tokens[k].nb_bits_, tokens[k].val_);
  }
}

// -----------------------------------------------------------------------------
// Coding

static int SparseIsBetter(uint32_t seq[], int len, int nb_bits) {
  uint32_t total = 1 << nb_bits;
  uint32_t half = total >> 1;
  int i;
  int saved_bits = -(len - 1);
  for (i = 0; i < len - 1; ++i) {
    const uint32_t c = seq[i];
    if (c == 0) saved_bits += nb_bits;
    total -= c;
    if (total < half) {
      --nb_bits;
      half >>= 1;
    }
  }
  return (saved_bits > 0);
}

static int WriteSequence(uint32_t seq[], int len, int sparse, int nb_bits,
                         FSCBitWriter* bw) {
  uint32_t total = 1 << nb_bits;
  uint32_t half = total >> 1;
  int i;
  int total_bits = 0;
  if (sparse == 2) {
    sparse = SparseIsBetter(seq, len, nb_bits);
    FSCWriteBits(bw, 1, sparse);
  }
  for (i = 0; i < len - 1; ++i) {
    const uint32_t c = seq[i];
    if (sparse) {
      FSCWriteBits(bw, 1, c > 0);
      total_bits += 1;
      if (c == 0) continue;
    }
    FSCWriteBits(bw, nb_bits, c);
    total_bits += nb_bits;
    total -= c;
    if (total < half) {
      --nb_bits;
      half >>= 1;
    }
  }
  if (total != seq[len - 1]) return -1;  // verify normalization
  return total_bits;
}

// Write the distribution table as header
static int WriteHeader(FSCEncoder* const enc, uint32_t counts[MAX_SYMBOLS],
                       FSCBitWriter* bw) {
  const int max_symbol = enc->max_symbol_;
  const int log_tab_size = enc->log_tab_size_;
  uint32_t tab_size = 1u << log_tab_size;
  FSCWriteBits(bw, 8, max_symbol - 1);

  if (max_symbol < HDR_SYMBOL_LIMIT) {  // Method #1 for small alphabet
    if (WriteSequence(counts, max_symbol, 2, log_tab_size, bw) < 0) {
      return 0;
    }
  } else {  // Method #2 for large alphabet
    uint8_t bins[MAX_SYMBOLS];
    uint32_t bHisto[LOG_TAB_SIZE + 1] = { 0 };
    uint16_t bits[MAX_SYMBOLS];
    int i;
    // Decompose into prefix and suffix
    {
      uint32_t total = tab_size;
      for (i = 0; i < max_symbol; ++i) {
        const int c = counts[i] + 1;
        int bin, b;
        for (bin = 0, b = c; b != 1; ++bin) { b >>= 1; }
        if (bin > log_tab_size) return 0;
        bins[i] = bin;             // prefix
        bits[i] = c - (1 << bin);  // suffix
        ++bHisto[bin];             // record prefix distribution
        if (total < counts[i]) return 0;
        total -= counts[i];
      }
      if (total != 0) return 0;   // Unnormalized distribution!?
    }
    if (bHisto[0] == max_symbol - 1) {   // only one symbol?
      FSCWriteBits(bw, 4, 16 - 1);   // special marker for sparse case
    } else {  // Compress the prefix sequence using a sub-encoder
      FSCEncoder enc2;
      if (!EncoderInit(&enc2, bHisto, log_tab_size + 1, TAB_HDR_BITS)) {
        fprintf(stderr, "Sub-Encoder initialization failed!\n");
        return 0;
      }
      const int hlen = enc2.max_symbol_;
      FSCWriteBits(bw, 4, hlen - 1);
      if (WriteSequence(bHisto, hlen, 2, TAB_HDR_BITS, bw) < 0) {
        return 0;
      }
      PutBlock(&enc2, bins, max_symbol - 1, bw);
      // Write the suffix sequence
      for (i = 0; i < max_symbol - 1; ++i) {
        FSCWriteBits(bw, bins[i], bits[i]);
      }
    }
  }
  return !bw->error_;
}

// -----------------------------------------------------------------------------
// Simulation and comparison against ideal case

#ifdef SHOW_SIMULATION
static void SimulateCoding(const FSCEncoder* enc, const uint32_t counts[],
                           const uint8_t* message, size_t size, int tab_size) {
  const int max_symbol = enc->max_symbol_;
  int s, N;
  const transf_t* const transforms = enc->transforms_;
  const uint16_t* const states = enc->states_;
  int state = tab_size;
  double S0 = 0., S1 = 0.;    // theoretical entropy
  {
    const double norm = 1. / tab_size;
    for (s = 0; s < max_symbol; ++s) {
      if (counts[s] > 0) {
        const double p = norm * counts[s];
        S0 += -p * log(p);
      }
    }
    S0 /= 8. * log(2.);
  }

  {
    uint32_t real_counts[MAX_SYMBOLS];
    FSCCountSymbols(message, size, real_counts);
    const double real_norm = 1. / size;
    for (N = 0; N < size; ++N) {
      S1 += -log(real_norm * real_counts[message[N]]);
    }
    S1 /= size * 8. * log(2.);
  }

  size_t bits = 0;   // count overhead too?
  for (N = size - 1; N >= 0; --N) {
    const transf_t* const transf = &transforms[message[N]];
    const int nb_bits = transf->nb_bits_ + (state >= transf->wrap_);
    bits += nb_bits;
    state = states[(state >> nb_bits) + transf->offset_];
  }
  printf("ENTROPY:\n");
  printf("  Simulated: %.2lf%%    (imperfect coder,       real message)\n",
         100. * bits / (size * 8.));
  printf("  Real:      %.2lf%%    (perfect approx. coder, real message)\n",
         100. * S1);
  printf("  Theory:    %.2lf%%    (perfect approx. coder, perfect message)\n",
         100. * S0);
}
#endif

// -----------------------------------------------------------------------------
// Entry point

static int Encode(const uint8_t* in, size_t size,
                  uint32_t counts[MAX_SYMBOLS],
                  uint8_t** out, size_t* out_size, int log_tab_size) {
  int ok = 0;
  FSCEncoder enc;
  FSCBitWriter bw;

  if (!FSCBitWriterInit(&bw, size >> 8)) return 0;

  if (!EncoderInit(&enc, counts, 0, log_tab_size)) goto end;
  FSCWriteBits(&bw, 4, LOG_TAB_SIZE - log_tab_size);
  size_t val = size;
  while (val) {
    FSCWriteBits(&bw, 1, 1);
    FSCWriteBits(&bw, 8, val & 0xff);
    val >>= 8;
  }
  FSCWriteBits(&bw, 1, 0);

  if (!WriteHeader(&enc, counts, &bw)) goto end;

#ifdef SHOW_SIMULATION
  SimulateCoding(&enc, counts, in, size, 1 << log_tab_size);
#endif

  while (size > 0) {
    const int next = (size > BLOCK_SIZE) ? BLOCK_SIZE : size;
    PutBlock(&enc, in, next, &bw);
    in += next;
    size -= next;
  }
  FSCBitWriterFlush(&bw);
  ok = !bw.error_;

 end:
  if (ok) {
    *out = FSCBitWriterFinish(&bw);
    *out_size = FSCBitWriterNumBytes(&bw);
  } else {
    FSCBitWriterDestroy(&bw);
  }
  return ok;
}

int FSCEncode(const uint8_t* in, size_t in_size,
              uint8_t** out, size_t* out_size, int log_tab_size) {
  uint32_t counts[MAX_SYMBOLS];
  FSCCountSymbols(in, in_size, counts);
  return Encode(in, in_size, counts, out, out_size, log_tab_size);
}

// -----------------------------------------------------------------------------