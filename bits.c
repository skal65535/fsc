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
// Bit reader/writer
//
// Author: Skal (pascal.massimino@gmail.com)

#include "./bits.h"
#include <string.h>   // for memcpy()

#define MAX_BITS 16   // max number of bit we have to read or write

//------------------------------------------------------------------------------
// endian-ness

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define htole32 OSSwapHostToLittleInt32
#define htole16 OSSwapHostToLittleInt16
#define le16toh OSSwapLittleToHostInt16
#define le32toh OSSwapLittleToHostInt32
#else
#include <endian.h>
#endif

#if (RBYTES == 4)
#define RSWAP le32toh
#else
#define RSWAP le16toh
#endif

#if (WBYTES == 4)
#define WSWAP htole32
#else
#define WSWAP htole16
#endif

//------------------------------------------------------------------------------
// BitReader

#define LBITS (sizeof(fsc_val_t) * 8)

void FSCInitBitReader(FSCBitReader* const br,
                      const uint8_t* const start, size_t length) {
  size_t i;
  assert(br != NULL);
  assert(start != NULL);

  br->buf_ = start;
  br->end_ = start + length;
  br->bits_ = 0;
  br->bit_pos_ = 0;
  br->eof_ = 0;
  for (i = 0; i < sizeof(br->bits_) && i < length; ++i) {
    br->bits_ |= ((fsc_val_t)(*br->buf_++)) << (8 * i);
  }
}

void FSCDoFillBitWindow(FSCBitReader* const br) {
  if (br->buf_ + sizeof(br->bits_) < br->end_) {
    // read several bytes at a time without bswap
    br->bits_ >>= RBITS;
    br->bit_pos_ -= RBITS;
    br->bits_ |= (fsc_val_t)RSWAP(*(const fsc_val_t*)(br->buf_)) << (LBITS - RBITS);
    br->buf_ += RBYTES;
    return;
  } else {  // finish with bytes
    while (br->bit_pos_ >= 8 && br->buf_ < br->end_) {
      br->bit_pos_ -= 8;
      br->bits_ >>= 8;
      br->bits_ |= ((fsc_val_t)(*br->buf_++)) << (LBITS - 8);
    }
    br->eof_ = (br->buf_ == br->end_) && (br->bit_pos_ >= LBITS);
  }
}

uint32_t FSCReadBits(FSCBitReader* const br, int nb) {
  assert(nb > 0 && nb <= RBITS);
  FSCFillBitWindow(br);
  const uint32_t ret =
      (uint32_t)(br->bits_ >> br->bit_pos_) & ((1 << nb) - 1);
  br->bit_pos_ += nb;
  return ret;
}

//------------------------------------------------------------------------------
// BitWriter

// 'new_size' is in fsc_wval_t units
static int SetSize(FSCBitWriter* const bw, size_t new_size) {
  if (new_size < 4096) new_size = 4096;
  fsc_wval_t* const new_buf = (fsc_wval_t*)malloc(new_size * sizeof(*new_buf));
  if (new_buf == NULL) {
    bw->error_ = 1;
    return 0;
  }
  const size_t cur_size = bw->cur_ - bw->buf_;
  if (cur_size > 0) memcpy(new_buf, bw->buf_, cur_size * sizeof(*new_buf));
  free(bw->buf_);
  bw->buf_ = new_buf;
  bw->cur_ = new_buf + cur_size;
  bw->end_ = bw->buf_ + new_size;
  return 1;
}

static int GrowSize(FSCBitWriter* const bw) {
  size_t new_size = (3 * (bw->end_ - bw->buf_)) >> 1;
  return SetSize(bw, new_size + 16384u);
}

static void CheckRoom(FSCBitWriter* const bw, int nb) {
  uint8_t* const cur = (uint8_t*)bw->cur_;
  uint8_t* const end = (uint8_t*)bw->end_;
  if (&cur[(nb + 7) >> 3] > end) GrowSize(bw);
}

int FSCBitWriterInit(FSCBitWriter* const bw, size_t expected_size) {
  memset(bw, 0, sizeof(*bw));
  return SetSize(bw, expected_size / sizeof(*bw->buf_));
}

void FSCBitWriterFlush(FSCBitWriter* const bw) {
  CheckRoom(bw, bw->used_);
  uint8_t* p = (uint8_t*)bw->cur_;
  while (bw->used_ > 0) {
    *p++ = bw->bits_;
    bw->bits_ >>= 8;
    bw->used_ -= 8;
  }
  bw->cur_ = (fsc_wval_t*)p;   // alignment might be off, that's ok
  bw->used_ = 0;
}

void FSCBitWriterDestroy(FSCBitWriter* const bw) {
  if (bw != NULL) {
    free(bw->buf_);
    memset(bw, 0, sizeof(*bw));
  }
}

void FSCWriteBits(FSCBitWriter* const bw, int nb, uint32_t bits) {
  assert(nb <= MAX_BITS);
  assert(bits < (1 << nb));
  if (nb > 0) {
    bw->bits_ |= ((fsc_val_t)bits) << bw->used_;
    bw->used_ += nb;
    if (bw->used_ >= WBITS) {
      CheckRoom(bw, WBITS);
      *bw->cur_++ = WSWAP(bw->bits_);
      bw->bits_ >>= WBITS;
      bw->used_ -= WBITS;
    }
  }
}

int FSCAppend(FSCBitWriter* const bw, const uint8_t* const buf, size_t len) {
  const size_t extra_size = (len - 1) / sizeof(fsc_wval_t) + 1;
  FSCBitWriterFlush(bw);
  if (bw->cur_ + extra_size > bw->end_ && !SetSize(bw, extra_size)) {
    return 0;
  }
  memcpy(bw->cur_, buf, extra_size * sizeof(fsc_wval_t));
  bw->cur_ += extra_size;
  return 1;
}

//------------------------------------------------------------------------------
