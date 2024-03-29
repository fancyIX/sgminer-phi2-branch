/* 0x10 kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2014  phm
 * Copyright (c) 2014 Girino Vey
 * Copyright (c) 2022 fancyIX
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @author   phm <phm@inbox.com>
 */

#ifdef __ECLIPSE_EDITOR__
  #include "OpenCLKernel.hpp"
#endif

#ifndef DARKCOIN_MOD_CL
#define DARKCOIN_MOD_CL

#if __ENDIAN_LITTLE__
  #define SPH_LITTLE_ENDIAN 1
#else
  #define SPH_BIG_ENDIAN 1
#endif

#define SPH_UPTR sph_u64
typedef unsigned int sph_u32;
typedef int sph_s32;

#ifndef __OPENCL_VERSION__
  typedef unsigned long long sph_u64;
  typedef long long sph_s64;
#else
  typedef unsigned long sph_u64;
  typedef long sph_s64;
#endif

#define SPH_64 1
#define SPH_64_TRUE 1

#define SPH_C32(x) ((sph_u32)(x ## U))
#define SPH_T32(x) (as_uint(x))
#define SPH_ROTL32(x, n) rotate(as_uint(x), as_uint(n))
#define SPH_ROTR32(x, n) SPH_ROTL32(x, (32 - (n)))

#define SPH_C64(x) ((sph_u64)(x ## UL))
#define SPH_T64(x) (as_ulong(x))
#define SPH_ROTL64(x, n) rotate(as_ulong(x), (n) & 0xFFFFFFFFFFFFFFFFUL)
#define SPH_ROTR64(x, n) SPH_ROTL64(x, (64 - (n)))

#define SPH_ECHO_64 1
#define SPH_KECCAK_64 1
#define SPH_JH_64 1
#define SPH_SIMD_NOCOPY 0
#define SPH_KECCAK_NOCOPY 0
#define SPH_SMALL_FOOTPRINT_GROESTL 0
#define SPH_GROESTL_BIG_ENDIAN 0
#define SPH_CUBEHASH_UNROLL 0

#ifndef SPH_COMPACT_BLAKE_64
  #define SPH_COMPACT_BLAKE_64 0
#endif
#ifndef SPH_LUFFA_PARALLEL
  #define SPH_LUFFA_PARALLEL 0
#endif
#ifndef SPH_KECCAK_UNROLL
  #define SPH_KECCAK_UNROLL   0
#endif

//#include "aes_helper.cl"
#include "blake.cl"
#include "bmw.cl"
//#include "groestl.cl"
#include "groestlf.cl"
#include "jh.cl"
#include "keccak.cl"
#include "skein.cl"
#include "luffa.cl"
#include "cubehash.cl"
#include "shavite.cl"
#include "simd_navi.cl"
#include "echo.cl"

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
#endif

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

#define CONST_EXP2 q[i+0] + SPH_ROTL64(q[i+1], 5) + q[i+2] + SPH_ROTL64(q[i+3], 11) + \
  q[i+4] + SPH_ROTL64(q[i+5], 27) + q[i+6] + SPH_ROTL64(q[i+7], 32) + \
  q[i+8] + SPH_ROTL64(q[i+9], 37) + q[i+10] + SPH_ROTL64(q[i+11], 43) + \
  q[i+12] + SPH_ROTL64(q[i+13], 53) + (SHR(q[i+14],1) ^ q[i+14]) + (SHR(q[i+15],2) ^ q[i+15])

typedef union {
  unsigned char h1[64];
  uint h4[16];
  ulong h8[8];
} hash_t;

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, __global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // blake
  sph_u64 H0 = SPH_C64(0x6A09E667F3BCC908), H1 = SPH_C64(0xBB67AE8584CAA73B);
  sph_u64 H2 = SPH_C64(0x3C6EF372FE94F82B), H3 = SPH_C64(0xA54FF53A5F1D36F1);
  sph_u64 H4 = SPH_C64(0x510E527FADE682D1), H5 = SPH_C64(0x9B05688C2B3E6C1F);
  sph_u64 H6 = SPH_C64(0x1F83D9ABFB41BD6B), H7 = SPH_C64(0x5BE0CD19137E2179);
  sph_u64 S0 = 0, S1 = 0, S2 = 0, S3 = 0;
  sph_u64 T0 = SPH_C64(0xFFFFFFFFFFFFFC00) + (80 << 3), T1 = 0xFFFFFFFFFFFFFFFF;;

  if ((T0 = SPH_T64(T0 + 1024)) < 1024)
  T1 = SPH_T64(T1 + 1);

  sph_u64 M0, M1, M2, M3, M4, M5, M6, M7;
  sph_u64 M8, M9, MA, MB, MC, MD, ME, MF;
  sph_u64 V0, V1, V2, V3, V4, V5, V6, V7;
  sph_u64 V8, V9, VA, VB, VC, VD, VE, VF;

  M0 = DEC64BE(block + 0);
  M1 = DEC64BE(block + 8);
  M2 = DEC64BE(block + 16);
  M3 = DEC64BE(block + 24);
  M4 = DEC64BE(block + 32);
  M5 = DEC64BE(block + 40);
  M6 = DEC64BE(block + 48);
  M7 = DEC64BE(block + 56);
  M8 = DEC64BE(block + 64);
  M9 = DEC64BE(block + 72);
  M9 &= 0xFFFFFFFF00000000;
  M9 ^= SWAP4(gid);
  MA = 0x8000000000000000;
  MB = 0;
  MC = 0;
  MD = 1;
  ME = 0;
  MF = 0x280;

  COMPRESS64;

  hash->h8[0] = H0;
  hash->h8[1] = H1;
  hash->h8[2] = H2;
  hash->h8[3] = H3;
  hash->h8[4] = H4;
  hash->h8[5] = H5;
  hash->h8[6] = H6;
  hash->h8[7] = H7;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search1(__global hash_t* hashes)
{

  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // skein

  sph_u64 h0 = SPH_C64(0x4903ADFF749C51CE), h1 = SPH_C64(0x0D95DE399746DF03), h2 = SPH_C64(0x8FD1934127C79BCE), h3 = SPH_C64(0x9A255629FF352CB1), h4 = SPH_C64(0x5DB62599DF6CA7B0), h5 = SPH_C64(0xEABE394CA9D5C3F4), h6 = SPH_C64(0x991112C71A75B523), h7 = SPH_C64(0xAE18A40B660FCC33);
  sph_u64 m0, m1, m2, m3, m4, m5, m6, m7;
  sph_u64 bcount = 0;

  m0 = SWAP8(hash->h8[0]);
  m1 = SWAP8(hash->h8[1]);
  m2 = SWAP8(hash->h8[2]);
  m3 = SWAP8(hash->h8[3]);
  m4 = SWAP8(hash->h8[4]);
  m5 = SWAP8(hash->h8[5]);
  m6 = SWAP8(hash->h8[6]);
  m7 = SWAP8(hash->h8[7]);

  UBI_BIG(480, 64);

  bcount = 0;
  m0 = m1 = m2 = m3 = m4 = m5 = m6 = m7 = 0;

  UBI_BIG(510, 8);

  hash->h8[0] = SWAP8(h0);
  hash->h8[1] = SWAP8(h1);
  hash->h8[2] = SWAP8(h2);
  hash->h8[3] = SWAP8(h3);
  hash->h8[4] = SWAP8(h4);
  hash->h8[5] = SWAP8(h5);
  hash->h8[6] = SWAP8(h6);
  hash->h8[7] = SWAP8(h7);

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search2(__global hash_t* hashes)
{
 uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // bmw
  sph_u64 BMW_H[16];

#pragma unroll 16
  for(unsigned u = 0; u < 16; u++)
  BMW_H[u] = BMW_IV512[u];

  sph_u64 mv[16],q[32];
    sph_u64 tmp;

  mv[0] = SWAP8(hash->h8[0]);
  mv[1] = SWAP8(hash->h8[1]);
  mv[2] = SWAP8(hash->h8[2]);
  mv[3] = SWAP8(hash->h8[3]);
  mv[4] = SWAP8(hash->h8[4]);
  mv[5] = SWAP8(hash->h8[5]);
  mv[6] = SWAP8(hash->h8[6]);
  mv[7] = SWAP8(hash->h8[7]);
  mv[8] = 0x80;
  mv[9] = 0;
  mv[10] = 0;
  mv[11] = 0;
  mv[12] = 0;
  mv[13] = 0;
  mv[14] = 0;
  mv[15] = SPH_C64(512);

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  sph_u64 XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  sph_u64 XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

#pragma unroll 16
  for(int i=0;i<16;i++)
  {
  mv[i] = BMW_H[i];
  BMW_H[i] = 0xaaaaaaaaaaaaaaa0ull + (sph_u64)i;
  }

  tmp = (mv[5] ^ BMW_H[5]) - (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]) + (mv[14] ^ BMW_H[14]);
  q[0] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[1];
  tmp = (mv[6] ^ BMW_H[6]) - (mv[8] ^ BMW_H[8]) + (mv[11] ^ BMW_H[11]) + (mv[14] ^ BMW_H[14]) - (mv[15] ^ BMW_H[15]);
  q[1] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[2];
  tmp = (mv[0] ^ BMW_H[0]) + (mv[7] ^ BMW_H[7]) + (mv[9] ^ BMW_H[9]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[2] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[3];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[1] ^ BMW_H[1]) + (mv[8] ^ BMW_H[8]) - (mv[10] ^ BMW_H[10]) + (mv[13] ^ BMW_H[13]);
  q[3] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[4];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[2] ^ BMW_H[2]) + (mv[9] ^ BMW_H[9]) - (mv[11] ^ BMW_H[11]) - (mv[14] ^ BMW_H[14]);
  q[4] = (SHR(tmp, 1) ^ tmp) + BMW_H[5];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[2] ^ BMW_H[2]) + (mv[10] ^ BMW_H[10]) - (mv[12] ^ BMW_H[12]) + (mv[15] ^ BMW_H[15]);
  q[5] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[6];
  tmp = (mv[4] ^ BMW_H[4]) - (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) - (mv[11] ^ BMW_H[11]) + (mv[13] ^ BMW_H[13]);
  q[6] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[7];
  tmp = (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[5] ^ BMW_H[5]) - (mv[12] ^ BMW_H[12]) - (mv[14] ^ BMW_H[14]);
  q[7] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[8];
  tmp = (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) - (mv[6] ^ BMW_H[6]) + (mv[13] ^ BMW_H[13]) - (mv[15] ^ BMW_H[15]);
  q[8] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[9];
  tmp = (mv[0] ^ BMW_H[0]) - (mv[3] ^ BMW_H[3]) + (mv[6] ^ BMW_H[6]) - (mv[7] ^ BMW_H[7]) + (mv[14] ^ BMW_H[14]);
  q[9] = (SHR(tmp, 1) ^ tmp) + BMW_H[10];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[1] ^ BMW_H[1]) - (mv[4] ^ BMW_H[4]) - (mv[7] ^ BMW_H[7]) + (mv[15] ^ BMW_H[15]);
  q[10] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[11];
  tmp = (mv[8] ^ BMW_H[8]) - (mv[0] ^ BMW_H[0]) - (mv[2] ^ BMW_H[2]) - (mv[5] ^ BMW_H[5]) + (mv[9] ^ BMW_H[9]);
  q[11] = (SHR(tmp, 1) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 13) ^ SPH_ROTL64(tmp, 43)) + BMW_H[12];
  tmp = (mv[1] ^ BMW_H[1]) + (mv[3] ^ BMW_H[3]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[10] ^ BMW_H[10]);
  q[12] = (SHR(tmp, 2) ^ SHL(tmp, 1) ^ SPH_ROTL64(tmp, 19) ^ SPH_ROTL64(tmp, 53)) + BMW_H[13];
  tmp = (mv[2] ^ BMW_H[2]) + (mv[4] ^ BMW_H[4]) + (mv[7] ^ BMW_H[7]) + (mv[10] ^ BMW_H[10]) + (mv[11] ^ BMW_H[11]);
  q[13] = (SHR(tmp, 2) ^ SHL(tmp, 2) ^ SPH_ROTL64(tmp, 28) ^ SPH_ROTL64(tmp, 59)) + BMW_H[14];
  tmp = (mv[3] ^ BMW_H[3]) - (mv[5] ^ BMW_H[5]) + (mv[8] ^ BMW_H[8]) - (mv[11] ^ BMW_H[11]) - (mv[12] ^ BMW_H[12]);
  q[14] = (SHR(tmp, 1) ^ tmp) + BMW_H[15];
  tmp = (mv[12] ^ BMW_H[12]) - (mv[4] ^ BMW_H[4]) - (mv[6] ^ BMW_H[6]) - (mv[9] ^ BMW_H[9]) + (mv[13] ^ BMW_H[13]);
  q[15] = (SHR(tmp, 1) ^ SHL(tmp, 3) ^ SPH_ROTL64(tmp, 4) ^ SPH_ROTL64(tmp, 37)) + BMW_H[0];

#pragma unroll 2
  for(int i=0;i<2;i++)
  {
  q[i+16] =
    (SHR(q[i], 1) ^ SHL(q[i], 2) ^ SPH_ROTL64(q[i], 13) ^ SPH_ROTL64(q[i], 43)) +
    (SHR(q[i+1], 2) ^ SHL(q[i+1], 1) ^ SPH_ROTL64(q[i+1], 19) ^ SPH_ROTL64(q[i+1], 53)) +
    (SHR(q[i+2], 2) ^ SHL(q[i+2], 2) ^ SPH_ROTL64(q[i+2], 28) ^ SPH_ROTL64(q[i+2], 59)) +
    (SHR(q[i+3], 1) ^ SHL(q[i+3], 3) ^ SPH_ROTL64(q[i+3], 4) ^ SPH_ROTL64(q[i+3], 37)) +
    (SHR(q[i+4], 1) ^ SHL(q[i+4], 2) ^ SPH_ROTL64(q[i+4], 13) ^ SPH_ROTL64(q[i+4], 43)) +
    (SHR(q[i+5], 2) ^ SHL(q[i+5], 1) ^ SPH_ROTL64(q[i+5], 19) ^ SPH_ROTL64(q[i+5], 53)) +
    (SHR(q[i+6], 2) ^ SHL(q[i+6], 2) ^ SPH_ROTL64(q[i+6], 28) ^ SPH_ROTL64(q[i+6], 59)) +
    (SHR(q[i+7], 1) ^ SHL(q[i+7], 3) ^ SPH_ROTL64(q[i+7], 4) ^ SPH_ROTL64(q[i+7], 37)) +
    (SHR(q[i+8], 1) ^ SHL(q[i+8], 2) ^ SPH_ROTL64(q[i+8], 13) ^ SPH_ROTL64(q[i+8], 43)) +
    (SHR(q[i+9], 2) ^ SHL(q[i+9], 1) ^ SPH_ROTL64(q[i+9], 19) ^ SPH_ROTL64(q[i+9], 53)) +
    (SHR(q[i+10], 2) ^ SHL(q[i+10], 2) ^ SPH_ROTL64(q[i+10], 28) ^ SPH_ROTL64(q[i+10], 59)) +
    (SHR(q[i+11], 1) ^ SHL(q[i+11], 3) ^ SPH_ROTL64(q[i+11], 4) ^ SPH_ROTL64(q[i+11], 37)) +
    (SHR(q[i+12], 1) ^ SHL(q[i+12], 2) ^ SPH_ROTL64(q[i+12], 13) ^ SPH_ROTL64(q[i+12], 43)) +
    (SHR(q[i+13], 2) ^ SHL(q[i+13], 1) ^ SPH_ROTL64(q[i+13], 19) ^ SPH_ROTL64(q[i+13], 53)) +
    (SHR(q[i+14], 2) ^ SHL(q[i+14], 2) ^ SPH_ROTL64(q[i+14], 28) ^ SPH_ROTL64(q[i+14], 59)) +
    (SHR(q[i+15], 1) ^ SHL(q[i+15], 3) ^ SPH_ROTL64(q[i+15], 4) ^ SPH_ROTL64(q[i+15], 37)) +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=2;i<6;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i+10], i+11) ) ^ BMW_H[i+7]);
  }

#pragma unroll 3
  for(int i=6;i<9;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i+7]);
  }

#pragma unroll 4
  for(int i=9;i<13;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i+3], i+4) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

#pragma unroll 3
  for(int i=13;i<16;i++)
  {
  q[i+16] = CONST_EXP2 +
    (( ((i+16)*(0x0555555555555555ull)) + SPH_ROTL64(mv[i], i+1) +
    SPH_ROTL64(mv[i-13], (i-13)+1) - SPH_ROTL64(mv[i-6], (i-6)+1) ) ^ BMW_H[i-9]);
  }

  XL64 = q[16]^q[17]^q[18]^q[19]^q[20]^q[21]^q[22]^q[23];
  XH64 = XL64^q[24]^q[25]^q[26]^q[27]^q[28]^q[29]^q[30]^q[31];

  BMW_H[0] = (SHL(XH64, 5) ^ SHR(q[16],5) ^ mv[0]) + ( XL64 ^ q[24] ^ q[0]);
  BMW_H[1] = (SHR(XH64, 7) ^ SHL(q[17],8) ^ mv[1]) + ( XL64 ^ q[25] ^ q[1]);
  BMW_H[2] = (SHR(XH64, 5) ^ SHL(q[18],5) ^ mv[2]) + ( XL64 ^ q[26] ^ q[2]);
  BMW_H[3] = (SHR(XH64, 1) ^ SHL(q[19],5) ^ mv[3]) + ( XL64 ^ q[27] ^ q[3]);
  BMW_H[4] = (SHR(XH64, 3) ^ q[20] ^ mv[4]) + ( XL64 ^ q[28] ^ q[4]);
  BMW_H[5] = (SHL(XH64, 6) ^ SHR(q[21],6) ^ mv[5]) + ( XL64 ^ q[29] ^ q[5]);
  BMW_H[6] = (SHR(XH64, 4) ^ SHL(q[22],6) ^ mv[6]) + ( XL64 ^ q[30] ^ q[6]);
  BMW_H[7] = (SHR(XH64,11) ^ SHL(q[23],2) ^ mv[7]) + ( XL64 ^ q[31] ^ q[7]);

  BMW_H[8] = SPH_ROTL64(BMW_H[4], 9) + ( XH64 ^ q[24] ^ mv[8]) + (SHL(XL64,8) ^ q[23] ^ q[8]);
  BMW_H[9] = SPH_ROTL64(BMW_H[5],10) + ( XH64 ^ q[25] ^ mv[9]) + (SHR(XL64,6) ^ q[16] ^ q[9]);
  BMW_H[10] = SPH_ROTL64(BMW_H[6],11) + ( XH64 ^ q[26] ^ mv[10]) + (SHL(XL64,6) ^ q[17] ^ q[10]);
  BMW_H[11] = SPH_ROTL64(BMW_H[7],12) + ( XH64 ^ q[27] ^ mv[11]) + (SHL(XL64,4) ^ q[18] ^ q[11]);
  BMW_H[12] = SPH_ROTL64(BMW_H[0],13) + ( XH64 ^ q[28] ^ mv[12]) + (SHR(XL64,3) ^ q[19] ^ q[12]);
  BMW_H[13] = SPH_ROTL64(BMW_H[1],14) + ( XH64 ^ q[29] ^ mv[13]) + (SHR(XL64,4) ^ q[20] ^ q[13]);
  BMW_H[14] = SPH_ROTL64(BMW_H[2],15) + ( XH64 ^ q[30] ^ mv[14]) + (SHR(XL64,7) ^ q[21] ^ q[14]);
  BMW_H[15] = SPH_ROTL64(BMW_H[3],16) + ( XH64 ^ q[31] ^ mv[15]) + (SHR(XL64,2) ^ q[22] ^ q[15]);

  hash->h8[0] = SWAP8(BMW_H[8]);
  hash->h8[1] = SWAP8(BMW_H[9]);
  hash->h8[2] = SWAP8(BMW_H[10]);
  hash->h8[3] = SWAP8(BMW_H[11]);
  hash->h8[4] = SWAP8(BMW_H[12]);
  hash->h8[5] = SWAP8(BMW_H[13]);
  hash->h8[6] = SWAP8(BMW_H[14]);
  hash->h8[7] = SWAP8(BMW_H[15]);

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(32, 1, 1)))
__kernel void search3(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[(gid-get_global_offset(0)) >> 2]);

  uint thr = (get_local_id(0) & 0x3);
  const uint THF = 4;

  uint paddedInput[8];

  #pragma unroll
		for(int k=0;k<4;k++) paddedInput[k] = SWAP4(((__global uint*) hash)[(1 - (thr & 1) + 2 * (thr / 2)) + (k * THF)]);

  #pragma unroll
  for(int k=4;k<8;k++) paddedInput[k] = 0;

  if (thr == 0) paddedInput[4] = 0x80; // end of data tag
  if (thr == 3) paddedInput[7] = 0x01000000;

  uint msgBitsliced[8];
  to_bitslice_quad(paddedInput, msgBitsliced);

  uint state[8];
  groestl512_progressMessage_quad(state, msgBitsliced);

  // Nur der erste von jeweils 4 Threads bekommt das Ergebns-Hash
  uint out_state[16];
  from_bitslice_quad(state, out_state);

    if (thr == 0) {
      #pragma unroll
      for (int i = 0; i < 4; i++) {
        ((__global uint4*) hash)[i].x = SWAP4(((uint4*) out_state)[i].y);
        ((__global uint4*) hash)[i].y = SWAP4(((uint4*) out_state)[i].x);
        ((__global uint4*) hash)[i].z = SWAP4(((uint4*) out_state)[i].w);
        ((__global uint4*) hash)[i].w = SWAP4(((uint4*) out_state)[i].z);
      }
		}
barrier(CLK_GLOBAL_MEM_FENCE);
}


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search4(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // jh

  sph_u64 h0h = C64e(0x6fd14b963e00aa17), h0l = C64e(0x636a2e057a15d543), h1h = C64e(0x8a225e8d0c97ef0b), h1l = C64e(0xe9341259f2b3c361), h2h = C64e(0x891da0c1536f801e), h2l = C64e(0x2aa9056bea2b6d80), h3h = C64e(0x588eccdb2075baa6), h3l = C64e(0xa90f3a76baf83bf7);
  sph_u64 h4h = C64e(0x0169e60541e34a69), h4l = C64e(0x46b58a8e2e6fe65a), h5h = C64e(0x1047a7d0c1843c24), h5l = C64e(0x3b6e71b12d5ac199), h6h = C64e(0xcf57f6ec9db1f856), h6l = C64e(0xa706887c5716b156), h7h = C64e(0xe3c2fcdfe68517fb), h7l = C64e(0x545a4678cc8cdd4b);
  sph_u64 tmp;

  for(int i = 0; i < 2; i++)
  {
  if (i == 0)
  {
    h0h ^= DEC64E(hash->h8[0]);
    h0l ^= DEC64E(hash->h8[1]);
    h1h ^= DEC64E(hash->h8[2]);
    h1l ^= DEC64E(hash->h8[3]);
    h2h ^= DEC64E(hash->h8[4]);
    h2l ^= DEC64E(hash->h8[5]);
    h3h ^= DEC64E(hash->h8[6]);
    h3l ^= DEC64E(hash->h8[7]);
  }
  else if(i == 1)
  {
    h4h ^= DEC64E(hash->h8[0]);
    h4l ^= DEC64E(hash->h8[1]);
    h5h ^= DEC64E(hash->h8[2]);
    h5l ^= DEC64E(hash->h8[3]);
    h6h ^= DEC64E(hash->h8[4]);
    h6l ^= DEC64E(hash->h8[5]);
    h7h ^= DEC64E(hash->h8[6]);
    h7l ^= DEC64E(hash->h8[7]);

    h0h ^= 0x80;
    h3l ^= 0x2000000000000;
  }
  E8;
  }
  h4h ^= 0x80;
  h7l ^= 0x2000000000000;

  hash->h8[0] = DEC64E(h4h);
  hash->h8[1] = DEC64E(h4l);
  hash->h8[2] = DEC64E(h5h);
  hash->h8[3] = DEC64E(h5l);
  hash->h8[4] = DEC64E(h6h);
  hash->h8[5] = DEC64E(h6l);
  hash->h8[6] = DEC64E(h7h);
  hash->h8[7] = DEC64E(h7l);

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search5(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // luffa

  sph_u32 V00 = SPH_C32(0x6d251e69), V01 = SPH_C32(0x44b051e0), V02 = SPH_C32(0x4eaa6fb4), V03 = SPH_C32(0xdbf78465), V04 = SPH_C32(0x6e292011), V05 = SPH_C32(0x90152df4), V06 = SPH_C32(0xee058139), V07 = SPH_C32(0xdef610bb);
  sph_u32 V10 = SPH_C32(0xc3b44b95), V11 = SPH_C32(0xd9d2f256), V12 = SPH_C32(0x70eee9a0), V13 = SPH_C32(0xde099fa3), V14 = SPH_C32(0x5d9b0557), V15 = SPH_C32(0x8fc944b3), V16 = SPH_C32(0xcf1ccf0e), V17 = SPH_C32(0x746cd581);
  sph_u32 V20 = SPH_C32(0xf7efc89d), V21 = SPH_C32(0x5dba5781), V22 = SPH_C32(0x04016ce5), V23 = SPH_C32(0xad659c05), V24 = SPH_C32(0x0306194f), V25 = SPH_C32(0x666d1836), V26 = SPH_C32(0x24aa230a), V27 = SPH_C32(0x8b264ae7);
  sph_u32 V30 = SPH_C32(0x858075d5), V31 = SPH_C32(0x36d79cce), V32 = SPH_C32(0xe571f7d7), V33 = SPH_C32(0x204b1f67), V34 = SPH_C32(0x35870c6a), V35 = SPH_C32(0x57e9e923), V36 = SPH_C32(0x14bcb808), V37 = SPH_C32(0x7cde72ce);
  sph_u32 V40 = SPH_C32(0x6c68e9be), V41 = SPH_C32(0x5ec41e22), V42 = SPH_C32(0xc825b7c7), V43 = SPH_C32(0xaffb4363), V44 = SPH_C32(0xf5df3999), V45 = SPH_C32(0x0fc688f1), V46 = SPH_C32(0xb07224cc), V47 = SPH_C32(0x03e86cea);

  DECL_TMP8(M);

  M0 = hash->h4[1];
  M1 = hash->h4[0];
  M2 = hash->h4[3];
  M3 = hash->h4[2];
  M4 = hash->h4[5];
  M5 = hash->h4[4];
  M6 = hash->h4[7];
  M7 = hash->h4[6];

  for(uint i = 0; i < 5; i++)
  {
  MI5;
  LUFFA_P5;

  if(i == 0)
  {
    M0 = hash->h4[9];
    M1 = hash->h4[8];
    M2 = hash->h4[11];
    M3 = hash->h4[10];
    M4 = hash->h4[13];
    M5 = hash->h4[12];
    M6 = hash->h4[15];
    M7 = hash->h4[14];
  }
  else if(i == 1)
  {
    M0 = 0x80000000;
    M1 = M2 = M3 = M4 = M5 = M6 = M7 = 0;
  }
  else if(i == 2)
    M0 = M1 = M2 = M3 = M4 = M5 = M6 = M7 = 0;
  else if(i == 3)
  {
    hash->h4[1] = V00 ^ V10 ^ V20 ^ V30 ^ V40;
    hash->h4[0] = V01 ^ V11 ^ V21 ^ V31 ^ V41;
    hash->h4[3] = V02 ^ V12 ^ V22 ^ V32 ^ V42;
    hash->h4[2] = V03 ^ V13 ^ V23 ^ V33 ^ V43;
    hash->h4[5] = V04 ^ V14 ^ V24 ^ V34 ^ V44;
    hash->h4[4] = V05 ^ V15 ^ V25 ^ V35 ^ V45;
    hash->h4[7] = V06 ^ V16 ^ V26 ^ V36 ^ V46;
    hash->h4[6] = V07 ^ V17 ^ V27 ^ V37 ^ V47;
  }
  }

  hash->h4[9] = V00 ^ V10 ^ V20 ^ V30 ^ V40;
  hash->h4[8] = V01 ^ V11 ^ V21 ^ V31 ^ V41;
  hash->h4[11] = V02 ^ V12 ^ V22 ^ V32 ^ V42;
  hash->h4[10] = V03 ^ V13 ^ V23 ^ V33 ^ V43;
  hash->h4[13] = V04 ^ V14 ^ V24 ^ V34 ^ V44;
  hash->h4[12] = V05 ^ V15 ^ V25 ^ V35 ^ V45;
  hash->h4[15] = V06 ^ V16 ^ V26 ^ V36 ^ V46;
  hash->h4[14] = V07 ^ V17 ^ V27 ^ V37 ^ V47;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search6(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // keccak

  sph_u64 a00 = 0, a01 = 0, a02 = 0, a03 = 0, a04 = 0;
  sph_u64 a10 = 0, a11 = 0, a12 = 0, a13 = 0, a14 = 0;
  sph_u64 a20 = 0, a21 = 0, a22 = 0, a23 = 0, a24 = 0;
  sph_u64 a30 = 0, a31 = 0, a32 = 0, a33 = 0, a34 = 0;
  sph_u64 a40 = 0, a41 = 0, a42 = 0, a43 = 0, a44 = 0;

  a10 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a20 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a31 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a22 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a23 = SPH_C64(0xFFFFFFFFFFFFFFFF);
  a04 = SPH_C64(0xFFFFFFFFFFFFFFFF);

  a00 ^= SWAP8(hash->h8[0]);
  a10 ^= SWAP8(hash->h8[1]);
  a20 ^= SWAP8(hash->h8[2]);
  a30 ^= SWAP8(hash->h8[3]);
  a40 ^= SWAP8(hash->h8[4]);
  a01 ^= SWAP8(hash->h8[5]);
  a11 ^= SWAP8(hash->h8[6]);
  a21 ^= SWAP8(hash->h8[7]);
  a31 ^= 0x8000000000000001;
  KECCAK_F_1600;

  // Finalize the "lane complement"
  a10 = ~a10;
  a20 = ~a20;

  hash->h8[0] = SWAP8(a00);
  hash->h8[1] = SWAP8(a10);
  hash->h8[2] = SWAP8(a20);
  hash->h8[3] = SWAP8(a30);
  hash->h8[4] = SWAP8(a40);
  hash->h8[5] = SWAP8(a01);
  hash->h8[6] = SWAP8(a11);
  hash->h8[7] = SWAP8(a21);

  barrier(CLK_GLOBAL_MEM_FENCE);
}



__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search7(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // cubehash.h1

  sph_u32 x0 = SPH_C32(0x2AEA2A61), x1 = SPH_C32(0x50F494D4), x2 = SPH_C32(0x2D538B8B), x3 = SPH_C32(0x4167D83E);
  sph_u32 x4 = SPH_C32(0x3FEE2313), x5 = SPH_C32(0xC701CF8C), x6 = SPH_C32(0xCC39968E), x7 = SPH_C32(0x50AC5695);
  sph_u32 x8 = SPH_C32(0x4D42C787), x9 = SPH_C32(0xA647A8B3), xa = SPH_C32(0x97CF0BEF), xb = SPH_C32(0x825B4537);
  sph_u32 xc = SPH_C32(0xEEF864D2), xd = SPH_C32(0xF22090C4), xe = SPH_C32(0xD0E5CD33), xf = SPH_C32(0xA23911AE);
  sph_u32 xg = SPH_C32(0xFCD398D9), xh = SPH_C32(0x148FE485), xi = SPH_C32(0x1B017BEF), xj = SPH_C32(0xB6444532);
  sph_u32 xk = SPH_C32(0x6A536159), xl = SPH_C32(0x2FF5781C), xm = SPH_C32(0x91FA7934), xn = SPH_C32(0x0DBADEA9);
  sph_u32 xo = SPH_C32(0xD65C8A2B), xp = SPH_C32(0xA5A70E75), xq = SPH_C32(0xB1C62456), xr = SPH_C32(0xBC796576);
  sph_u32 xs = SPH_C32(0x1921C8F7), xt = SPH_C32(0xE7989AF1), xu = SPH_C32(0x7795D246), xv = SPH_C32(0xD43E3B44);

  x0 ^= SWAP4(hash->h4[1]);
  x1 ^= SWAP4(hash->h4[0]);
  x2 ^= SWAP4(hash->h4[3]);
  x3 ^= SWAP4(hash->h4[2]);
  x4 ^= SWAP4(hash->h4[5]);
  x5 ^= SWAP4(hash->h4[4]);
  x6 ^= SWAP4(hash->h4[7]);
  x7 ^= SWAP4(hash->h4[6]);

  for (int i = 0; i < 13; i ++)
  {
  SIXTEEN_ROUNDS;

  if (i == 0)
  {
    x0 ^= SWAP4(hash->h4[9]);
    x1 ^= SWAP4(hash->h4[8]);
    x2 ^= SWAP4(hash->h4[11]);
    x3 ^= SWAP4(hash->h4[10]);
    x4 ^= SWAP4(hash->h4[13]);
    x5 ^= SWAP4(hash->h4[12]);
    x6 ^= SWAP4(hash->h4[15]);
    x7 ^= SWAP4(hash->h4[14]);
  }
  else if(i == 1)
    x0 ^= 0x80;
  else if (i == 2)
    xv ^= SPH_C32(1);
  }

  hash->h4[0] = x0;
  hash->h4[1] = x1;
  hash->h4[2] = x2;
  hash->h4[3] = x3;
  hash->h4[4] = x4;
  hash->h4[5] = x5;
  hash->h4[6] = x6;
  hash->h4[7] = x7;
  hash->h4[8] = x8;
  hash->h4[9] = x9;
  hash->h4[10] = xa;
  hash->h4[11] = xb;
  hash->h4[12] = xc;
  hash->h4[13] = xd;
  hash->h4[14] = xe;
  hash->h4[15] = xf;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(32, 1, 1)))
__kernel void search8(__global uint* g_hash)
{
  __local uint4 g_temp4[4 * 64];

  uint gid = get_global_id(0);
  int threadBloc = (gid-get_global_offset(0)) / 8;
  int hashPosition = threadBloc * 16;
  __global uint *inpHash = &g_hash[hashPosition];

  // Read hash per 8 threads
  uint Hash[2];
  int ndx = get_local_id(0) & 7;
  Hash[0] = inpHash[ndx];
  Hash[1] = inpHash[ndx + 8];

  Expansion(Hash, g_temp4);
  x11_simd512_gpu_compress_64(inpHash, g_temp4);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search9(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  __local sph_u32 AES0[256], AES1[256], AES2[256], AES3[256];

  int init = get_local_id(0);
  int step = get_local_size(0);

  for (int i = init; i < 256; i += step)
  {
  AES0[i] = AES0_C[i];
  AES1[i] = AES1_C[i];
  AES2[i] = AES2_C[i];
  AES3[i] = AES3_C[i];
  }

  barrier(CLK_LOCAL_MEM_FENCE);

  // shavite
  // IV
  sph_u32 h0 = SPH_C32(0x72FCCDD8), h1 = SPH_C32(0x79CA4727), h2 = SPH_C32(0x128A077B), h3 = SPH_C32(0x40D55AEC);
  sph_u32 h4 = SPH_C32(0xD1901A06), h5 = SPH_C32(0x430AE307), h6 = SPH_C32(0xB29F5CD1), h7 = SPH_C32(0xDF07FBFC);
  sph_u32 h8 = SPH_C32(0x8E45D73D), h9 = SPH_C32(0x681AB538), hA = SPH_C32(0xBDE86578), hB = SPH_C32(0xDD577E47);
  sph_u32 hC = SPH_C32(0xE275EADE), hD = SPH_C32(0x502D9FCD), hE = SPH_C32(0xB9357178), hF = SPH_C32(0x022A4B9A);

  // state
  sph_u32 rk00, rk01, rk02, rk03, rk04, rk05, rk06, rk07;
  sph_u32 rk08, rk09, rk0A, rk0B, rk0C, rk0D, rk0E, rk0F;
  sph_u32 rk10, rk11, rk12, rk13, rk14, rk15, rk16, rk17;
  sph_u32 rk18, rk19, rk1A, rk1B, rk1C, rk1D, rk1E, rk1F;

  sph_u32 sc_count0 = (64 << 3), sc_count1 = 0, sc_count2 = 0, sc_count3 = 0;

  rk00 = hash->h4[0];
  rk01 = hash->h4[1];
  rk02 = hash->h4[2];
  rk03 = hash->h4[3];
  rk04 = hash->h4[4];
  rk05 = hash->h4[5];
  rk06 = hash->h4[6];
  rk07 = hash->h4[7];
  rk08 = hash->h4[8];
  rk09 = hash->h4[9];
  rk0A = hash->h4[10];
  rk0B = hash->h4[11];
  rk0C = hash->h4[12];
  rk0D = hash->h4[13];
  rk0E = hash->h4[14];
  rk0F = hash->h4[15];
  rk10 = 0x80;
  rk11 = rk12 = rk13 = rk14 = rk15 = rk16 = rk17 = rk18 = rk19 = rk1A = 0;
  rk1B = 0x2000000;
  rk1C = rk1D = rk1E = 0;
  rk1F = 0x2000000;

  c512(buf);

  hash->h4[0] = h0;
  hash->h4[1] = h1;
  hash->h4[2] = h2;
  hash->h4[3] = h3;
  hash->h4[4] = h4;
  hash->h4[5] = h5;
  hash->h4[6] = h6;
  hash->h4[7] = h7;
  hash->h4[8] = h8;
  hash->h4[9] = h9;
  hash->h4[10] = hA;
  hash->h4[11] = hB;
  hash->h4[12] = hC;
  hash->h4[13] = hD;
  hash->h4[14] = hE;
  hash->h4[15] = hF;

  barrier(CLK_GLOBAL_MEM_FENCE);
}



__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search10(__global hash_t* hashes, __global uint* output, const ulong target)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  __local sph_u32 AES0[256], AES1[256], AES2[256], AES3[256];

  int init = get_local_id(0);
  int step = get_local_size(0);

  for (int i = init; i < 256; i += step)
  {
  AES0[i] = AES0_C[i];
  AES1[i] = AES1_C[i];
  AES2[i] = AES2_C[i];
  AES3[i] = AES3_C[i];
  }

  barrier(CLK_LOCAL_MEM_FENCE);

  // copies hashes to "hash"
  // echo
  sph_u64 W00, W01, W10, W11, W20, W21, W30, W31, W40, W41, W50, W51, W60, W61, W70, W71, W80, W81, W90, W91, WA0, WA1, WB0, WB1, WC0, WC1, WD0, WD1, WE0, WE1, WF0, WF1;
  sph_u64 Vb00, Vb01, Vb10, Vb11, Vb20, Vb21, Vb30, Vb31, Vb40, Vb41, Vb50, Vb51, Vb60, Vb61, Vb70, Vb71;
  Vb00 = Vb10 = Vb20 = Vb30 = Vb40 = Vb50 = Vb60 = Vb70 = 512UL;
  Vb01 = Vb11 = Vb21 = Vb31 = Vb41 = Vb51 = Vb61 = Vb71 = 0;

  sph_u32 K0 = 512;
  sph_u32 K1 = 0;
  sph_u32 K2 = 0;
  sph_u32 K3 = 0;

  W00 = Vb00;
  W01 = Vb01;
  W10 = Vb10;
  W11 = Vb11;
  W20 = Vb20;
  W21 = Vb21;
  W30 = Vb30;
  W31 = Vb31;
  W40 = Vb40;
  W41 = Vb41;
  W50 = Vb50;
  W51 = Vb51;
  W60 = Vb60;
  W61 = Vb61;
  W70 = Vb70;
  W71 = Vb71;
  W80 = hash->h8[0];
  W81 = hash->h8[1];
  W90 = hash->h8[2];
  W91 = hash->h8[3];
  WA0 = hash->h8[4];
  WA1 = hash->h8[5];
  WB0 = hash->h8[6];
  WB1 = hash->h8[7];
  WC0 = 0x80;
  WC1 = 0;
  WD0 = 0;
  WD1 = 0;
  WE0 = 0;
  WE1 = 0x200000000000000;
  WF0 = 0x200;
  WF1 = 0;

  for (unsigned u = 0; u < 10; u ++)
  BIG_ROUND;

  Vb00 ^= hash->h8[0] ^ W00 ^ W80;
  Vb01 ^= hash->h8[1] ^ W01 ^ W81;
  Vb10 ^= hash->h8[2] ^ W10 ^ W90;
  Vb11 ^= hash->h8[3] ^ W11 ^ W91;
  Vb20 ^= hash->h8[4] ^ W20 ^ WA0;
  Vb21 ^= hash->h8[5] ^ W21 ^ WA1;
  Vb30 ^= hash->h8[6] ^ W30 ^ WB0;
  Vb31 ^= hash->h8[7] ^ W31 ^ WB1;

  barrier(CLK_GLOBAL_MEM_FENCE);

  bool result = (Vb11 <= target);

  if (result)
  output[atomic_inc(output+0xFF)] = SWAP4(gid);
}

#endif// DARKCOIN_MOD_CL