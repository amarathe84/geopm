/*
 * Copyright 2017, 2018 Science and Technology Facilities Council (UK)
 * IBM Confidential
 * OCO Source Materials
 * 5747-SM3
 * (c) Copyright IBM Corp. 2017, 2018
 * The source code for this program is not published or otherwise
 * divested of its trade secrets, irrespective of what has
 * been deposited with the U.S. Copyright Office.
 *
 * Copyright (c) 2015, 2016, 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef GEOPM_HASH_H_INCLUDE
#define GEOPM_HASH_H_INCLUDE
#endif

#include "geopm_arch.h"

#include <stdint.h>
#ifdef X86
#include <smmintrin.h>
#endif

#ifdef __cplusplus
extern "C"
{
#endif
#ifdef POWERPC
unsigned int crc32_vpmsum(unsigned int crc, unsigned char *p, unsigned long len);
#endif

static inline uint64_t geopm_crc32_u64(uint64_t begin, uint64_t key)
{
#ifdef X86
  return _mm_crc32_u64(begin, key);
#elif defined(POWERPC)
  unsigned char key_c[9];
  int pos = 0;

  while(key != 0) {
    key_c[pos++] = key & 0xFF;
    key >>= sizeof(uint64_t);
  }
  key_c[pos] = '\0';

  return crc32_vpmsum(begin, key_c, pos);
#else
  // TODO: Implement generic version without speed-up obtainable with
  //
  //
  // architectures
  assert(0);
#endif

}

uint64_t geopm_crc32_str(uint64_t begin, const char *key);
#ifdef __cplusplus
}
#endif
