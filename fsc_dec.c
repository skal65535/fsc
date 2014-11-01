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
// Finite State Coder (FSC) decoder implementation
//
//  based on Jarek Duda's paper: http://arxiv.org/pdf/1311.2540v1.pdf
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./fsc.h"
#include <stdio.h>
#include <assert.h>

#include "./bits.h"

//------------------------------------------------------------------------------
// Decoding

typedef enum {
  FSC_OK = 0,
  FSC_ERROR = 1,
  FSC_EOF = 2
} FSC_STATUS;

typedef struct {
  int16_t next_;      // relative delta jump from this state to the next
  uint8_t symbol_;    // symbol associated to the state
  int8_t len_;        // number of bits to read for transitioning this state
} FSCState;

struct FSCDecoder {
  FSCBitReader br_;
  FSC_STATUS status_;
  int log_tab_size_;
  uint32_t out_size_;
  FSCState tab_[TAB_SIZE];   // ~16k for LOG_TAB_SIZE=12
};

//------------------------------------------------------------------------------
// State table building

static int Log2(uint32_t v) {
  int s = 31;
  while (v < (1 << s)) --s;
  return s;
}

static int BuildStateTable(FSCDecoder* dec, uint32_t counts[], int max_symbol) {
  int s, i, pos;
  uint16_t state[MAX_SYMBOLS];   // next state of symbol 's'
  FSCState* const tab = dec->tab_;
  const int log_tab_size = dec->log_tab_size_;
  const int tab_size = 1 << log_tab_size;

  assert(max_symbol <= MAX_SYMBOLS && max_symbol > 0);
  for (s = 0; s < max_symbol; ++s) state[s] = counts[s];

  uint8_t* const symbols = (uint8_t*)malloc(tab_size * sizeof(*symbols));
  if (symbols == NULL) return 0;
  if (!BuildSpreadTable_ptr(max_symbol, counts, log_tab_size, symbols)) {
    free(symbols);
    return 0;
  }

  for (pos = 0; pos < tab_size; ++pos) {
    s = symbols[pos];
    tab[pos].symbol_ = s;
    const int next_state = state[s]++;
    const int nb_bits = log_tab_size - Log2(next_state);
    const int new_pos = (next_state << nb_bits) - tab_size;
    tab[pos].next_ = new_pos - pos;   // how to jump from Is to I
    tab[pos].len_  = nb_bits;
  }
  free(symbols);
  if (pos != tab_size) return 0;   // input not normalized!

  return 1;
}

//------------------------------------------------------------------------------
// Decoding loop

static int GetBlock(FSCDecoder* dec, uint8_t* out, int size, FSCBitReader* br) {
  if (dec->status_ == FSC_OK) {
    const FSCState* state = dec->tab_;   // state_idx=0 at start
    int next_nb_bits = dec->log_tab_size_;
    int n;
    for (n = 0; n < size; ++n) {
      FSCFillBitWindow(br);
      state += FSCSeeBits(br) & ((1 << next_nb_bits) - 1);
      FSCDiscardBits(br, next_nb_bits);
      *out++ = state->symbol_;
      next_nb_bits = state->len_;
      state += state->next_;
    }
    dec->status_ = dec->br_.eof_ ? FSC_EOF : FSC_OK;
    return size;
  }
  return 0;
}

//------------------------------------------------------------------------------
// Header

static int ReadSequence(uint32_t seq[], int len, int sparse, int nb_bits,
                        FSCBitReader* br) {
  uint32_t total = 1 << nb_bits;
  uint32_t half = total >> 1;
  int i;
  if (sparse == 2) sparse = FSCReadBits(br, 1);
  for (i = 0; i < len - 1; ++i ) {
    uint16_t c;
    if (sparse && !FSCReadBits(br, 1)) {
      seq[i] = 0;
      continue;
    }
    c = FSCReadBits(br, nb_bits);
    seq[i] = c;
    if (total < c) return 0;   // normalization problem
    total -= c;
    if (total < half) {
      --nb_bits;
      half >>= 1;
    }
  }
  seq[len - 1] = total;   // remaining part
  return 1;
}

static int ReadHeader(FSCDecoder* dec, FSCBitReader* br) {
  uint32_t counts[TAB_SIZE];
  const int log_tab_size = dec->log_tab_size_;
  const uint32_t tab_size = 1 << log_tab_size;
  const int max_symbol = 1 + FSCReadBits(br, 8);
  if (max_symbol < HDR_SYMBOL_LIMIT) {  // Use method #1 for small alphabet
    if (!ReadSequence(counts, max_symbol, 2, log_tab_size, br)) {
      return 0;
    }
  } else {  // Use more complex method #2 for large alphabet
    const int hlen = 1 + FSCReadBits(br, 4);
    uint32_t bHisto[LOG_TAB_SIZE + 1];
    uint8_t bins[MAX_SYMBOLS] = { 0 };
    if (hlen == 16) {   // sparse case
      int i;
      for (i = 0; i < max_symbol - 1; ++i) counts[i] = 0;
      counts[max_symbol - 1] = tab_size;
    } else {
      if (!ReadSequence(bHisto, hlen, 2, TAB_HDR_BITS, br)) {
        return 0;
      }
      {
        FSCDecoder dec2;
        memset(&dec2, 0, sizeof(dec2));
        dec2.log_tab_size_ = TAB_HDR_BITS;
        if (hlen > log_tab_size) return 0;
        if (!BuildStateTable(&dec2, bHisto, hlen)) {
          fprintf(stderr, "Sub-Decoder initialization failed!\n");
          return 0;
        }
        GetBlock(&dec2, bins, max_symbol - 1, br);
      }
      {
        int i;
        uint32_t total = tab_size;
        for (i = 0; i < max_symbol - 1; ++i) {
          const int b = bins[i];
          const int residue = (b > 0) ? FSCReadBits(br, b) : 0;
          const int c = (1 << b) | residue;
          counts[i] = c - 1;
          if (total < counts[i]) return 0;   // normalization error
          total -= counts[i];
        }
        counts[max_symbol - 1] = total;   // remaining part
      }
    }
  }
  if (br->eof_) return 0;
  return BuildStateTable(dec, counts, max_symbol);
}

//------------------------------------------------------------------------------

FSCDecoder* FSCInit(const uint8_t* input, size_t len) {
  FSCDecoder* dec = (FSCDecoder*)calloc(1, sizeof(*dec));
  if (dec == NULL) return NULL;

  FSCInitBitReader(&dec->br_, input, len);
  dec->log_tab_size_ = LOG_TAB_SIZE - FSCReadBits(&dec->br_, 4);
  dec->out_size_ = 0;
  int i;
  for (i = 0; i < 8 && FSCReadBits(&dec->br_, 1); ++i) {
    dec->out_size_ |= FSCReadBits(&dec->br_, 8) << (8 * i);
  }

  dec->status_ = ReadHeader(dec, &dec->br_) ? FSC_OK : FSC_ERROR;
  return dec;
}

int FSCIsOk(FSCDecoder* dec) {
  return (dec != NULL) && (dec->status_ != FSC_ERROR);
}

void FSCDelete(FSCDecoder* dec) {
  free(dec);
}

int FSCDecompress(FSCDecoder* dec, uint8_t** out, size_t* out_size) {
  if (dec == NULL || out == NULL || out_size == NULL) return 0;
  *out = (uint8_t*)malloc(dec->out_size_ * sizeof(*out));
  if (*out == NULL) return 0;
  *out_size = dec->out_size_;
  size_t size = dec->out_size_;
  uint8_t* ptr = *out;
  while (size > 0 && dec->status_ == FSC_OK) {
    const int got =
        GetBlock(dec, ptr, (size > BLOCK_SIZE) ? BLOCK_SIZE : size, &dec->br_);
    ptr += got;
    size -= got;
  }
  if (dec->status_ == FSC_ERROR) {
    free(*out);
    *out = 0;
    *out_size = 0;
    return 0;
  }
  return 1;
}

//------------------------------------------------------------------------------

int FSCDecode(const uint8_t* in, size_t in_size, uint8_t** out, size_t* size) {
  FSCDecoder* const dec = FSCInit(in, in_size);
  if (dec == NULL || out == NULL || size == NULL) return 0;
  const int ok = FSCDecompress(dec, out, size) && FSCIsOk(dec);
  FSCDelete(dec);
  return ok;
}

//------------------------------------------------------------------------------
