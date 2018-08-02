/* Phi2 kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2016 tpruvot
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
 * @author tpruvot 2016
 * @author fancyIX 2018
 */

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
#else
  typedef unsigned long sph_u64;
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
#define SPH_JH_64 1
#define SPH_CUBEHASH_UNROLL 0

#include "skein.cl"
#include "jh.cl"
#include "cubehash.cl"
#include "fugue.cl"
#include "gost-mod.cl"
#include "echo.cl"
#define memshift 3
#include "lyra2f.cl"
//#include "lyra2v16t.cl" TBR

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)

#if SPH_BIG_ENDIAN
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
  #define DEC64LE(x) SWAP8(*(const __global sph_u64 *) (x));
#else
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC64LE(x) (*(const __global sph_u64 *) (x));
#endif

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

typedef union {
  unsigned char h1[64];
  unsigned short h2[32];
  uint h4[16];
  ulong h8[8];
} hash_t;

typedef union {
    uint h4[8];
    ulong h8[4];
    uint4 h16[2];
    ulong2 hl16[2];
    ulong4 h32;
} hash2_t;

typedef union {
    uint h4[32];
    ulong h8[16];
    uint4 h16[8];
    ulong2 hl16[8];
    ulong4 h32[4];
} lyraState_t;

struct SharedState
{
    ulong s0;
    ulong s1;
    ulong s2;
};

#define SWAP8_INPUT(x)   x
#define SWAP8_USELESS(x) x


#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x));
  #define DEC32LE(x) SWAP4(*(const __global sph_u32 *) (x));
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x));
  #define DEC32LE(x) (*(const __global sph_u32 *) (x));
#endif

// cubehash_80
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, __global hash_t* hashes, uint has_roots)
{
    uint gid = get_global_id(0);
    __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  if (!has_roots) {
// cubehash512_cuda_hash_80

    sph_u32 x0 = SPH_C32(0x2AEA2A61), x1 = SPH_C32(0x50F494D4), x2 = SPH_C32(0x2D538B8B), x3 = SPH_C32(0x4167D83E);
    sph_u32 x4 = SPH_C32(0x3FEE2313), x5 = SPH_C32(0xC701CF8C), x6 = SPH_C32(0xCC39968E), x7 = SPH_C32(0x50AC5695);
    sph_u32 x8 = SPH_C32(0x4D42C787), x9 = SPH_C32(0xA647A8B3), xa = SPH_C32(0x97CF0BEF), xb = SPH_C32(0x825B4537);
    sph_u32 xc = SPH_C32(0xEEF864D2), xd = SPH_C32(0xF22090C4), xe = SPH_C32(0xD0E5CD33), xf = SPH_C32(0xA23911AE);
    sph_u32 xg = SPH_C32(0xFCD398D9), xh = SPH_C32(0x148FE485), xi = SPH_C32(0x1B017BEF), xj = SPH_C32(0xB6444532);
    sph_u32 xk = SPH_C32(0x6A536159), xl = SPH_C32(0x2FF5781C), xm = SPH_C32(0x91FA7934), xn = SPH_C32(0x0DBADEA9);
    sph_u32 xo = SPH_C32(0xD65C8A2B), xp = SPH_C32(0xA5A70E75), xq = SPH_C32(0xB1C62456), xr = SPH_C32(0xBC796576);
    sph_u32 xs = SPH_C32(0x1921C8F7), xt = SPH_C32(0xE7989AF1), xu = SPH_C32(0x7795D246), xv = SPH_C32(0xD43E3B44);

    x0 ^= DEC32LE(block + 0);
    x1 ^= DEC32LE(block + 4);
    x2 ^= DEC32LE(block + 8);
    x3 ^= DEC32LE(block + 12);
    x4 ^= DEC32LE(block + 16);
    x5 ^= DEC32LE(block + 20);
    x6 ^= DEC32LE(block + 24);
    x7 ^= DEC32LE(block + 28);

    SIXTEEN_ROUNDS;
    x0 ^= DEC32LE(block + 32);
    x1 ^= DEC32LE(block + 36);
    x2 ^= DEC32LE(block + 40);
    x3 ^= DEC32LE(block + 44);
    x4 ^= DEC32LE(block + 48);
    x5 ^= DEC32LE(block + 52);
    x6 ^= DEC32LE(block + 56);
    x7 ^= DEC32LE(block + 60);

    SIXTEEN_ROUNDS;
    x0 ^= DEC32LE(block + 64);
    x1 ^= DEC32LE(block + 68);
    x2 ^= DEC32LE(block + 72);
    x3 ^= gid;
    x4 ^= 0x80;

    SIXTEEN_ROUNDS;
    xv ^= SPH_C32(1);

    #pragma unroll
    for (int i = 0; i < 10; i++) {
        SIXTEEN_ROUNDS;
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

  } else {
    // cubehash512_cuda_hash_144

    sph_u32 x0 = SPH_C32(0x2AEA2A61), x1 = SPH_C32(0x50F494D4), x2 = SPH_C32(0x2D538B8B), x3 = SPH_C32(0x4167D83E);
    sph_u32 x4 = SPH_C32(0x3FEE2313), x5 = SPH_C32(0xC701CF8C), x6 = SPH_C32(0xCC39968E), x7 = SPH_C32(0x50AC5695);
    sph_u32 x8 = SPH_C32(0x4D42C787), x9 = SPH_C32(0xA647A8B3), xa = SPH_C32(0x97CF0BEF), xb = SPH_C32(0x825B4537);
    sph_u32 xc = SPH_C32(0xEEF864D2), xd = SPH_C32(0xF22090C4), xe = SPH_C32(0xD0E5CD33), xf = SPH_C32(0xA23911AE);
    sph_u32 xg = SPH_C32(0xFCD398D9), xh = SPH_C32(0x148FE485), xi = SPH_C32(0x1B017BEF), xj = SPH_C32(0xB6444532);
    sph_u32 xk = SPH_C32(0x6A536159), xl = SPH_C32(0x2FF5781C), xm = SPH_C32(0x91FA7934), xn = SPH_C32(0x0DBADEA9);
    sph_u32 xo = SPH_C32(0xD65C8A2B), xp = SPH_C32(0xA5A70E75), xq = SPH_C32(0xB1C62456), xr = SPH_C32(0xBC796576);
    sph_u32 xs = SPH_C32(0x1921C8F7), xt = SPH_C32(0xE7989AF1), xu = SPH_C32(0x7795D246), xv = SPH_C32(0xD43E3B44);

    x0 ^= DEC32LE(block + 0);
    x1 ^= DEC32LE(block + 4);
    x2 ^= DEC32LE(block + 8);
    x3 ^= DEC32LE(block + 12);
    x4 ^= DEC32LE(block + 16);
    x5 ^= DEC32LE(block + 20);
    x6 ^= DEC32LE(block + 24);
    x7 ^= DEC32LE(block + 28);

    SIXTEEN_ROUNDS;
    x0 ^= DEC32LE(block + 32);
    x1 ^= DEC32LE(block + 36);
    x2 ^= DEC32LE(block + 40);
    x3 ^= DEC32LE(block + 44);
    x4 ^= DEC32LE(block + 48);
    x5 ^= DEC32LE(block + 52);
    x6 ^= DEC32LE(block + 56);
    x7 ^= DEC32LE(block + 60);

    SIXTEEN_ROUNDS;
    x0 ^= DEC32LE(block + 64);
    x1 ^= DEC32LE(block + 68);
    x2 ^= DEC32LE(block + 72);
    x3 ^= gid;
    x4 ^= DEC32LE(block + 80);
    x5 ^= DEC32LE(block + 84);
    x6 ^= DEC32LE(block + 88);
    x7 ^= DEC32LE(block + 92);

    SIXTEEN_ROUNDS;

    x0 ^= DEC32LE(block + 96);
    x1 ^= DEC32LE(block + 100);
    x2 ^= DEC32LE(block + 104);
    x3 ^= DEC32LE(block + 108);
    x4 ^= DEC32LE(block + 112);
    x5 ^= DEC32LE(block + 116);
    x6 ^= DEC32LE(block + 120);
    x7 ^= DEC32LE(block + 124);

    SIXTEEN_ROUNDS;

    x0 ^= DEC32LE(block + 128);
    x1 ^= DEC32LE(block + 132);
    x2 ^= DEC32LE(block + 136);
    x3 ^= DEC32LE(block + 140);
    x4 ^= 0x80;

    SIXTEEN_ROUNDS;
    xv ^= SPH_C32(1);

    #pragma unroll
    for (int i = 0; i < 10; i++) {
        SIXTEEN_ROUNDS;
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
}



/// lyra2 p1 

__attribute__((reqd_work_group_size(WORKSIZE2, 1, 1)))
__kernel void search1(__global uint* hashes, __global uchar* sharedDataBuf)
{
    int gid = get_global_id(0);

    __global hash2_t *hash = (__global hash2_t *)(hashes + (8* (gid-get_global_offset(0))));
    __global lyraState_t *lyraState = (__global lyraState_t *)(sharedDataBuf + ((8 * 4  * 4 + 8 * 192 * 4) * (gid-get_global_offset(0))));

    ulong ttr;

    ulong2 state[8];
    // state0
    state[0] = hash->hl16[0];
    state[1] = hash->hl16[1];
    // state1
    state[2] = state[0];
    state[3] = state[1];
    // state2
    state[4] = (ulong2)(0x6a09e667f3bcc908UL, 0xbb67ae8584caa73bUL);
    state[5] = (ulong2)(0x3c6ef372fe94f82bUL, 0xa54ff53a5f1d36f1UL);
    // state3 (low,high,..
    state[6] = (ulong2)(0x510e527fade682d1UL, 0x9b05688c2b3e6c1fUL);
    state[7] = (ulong2)(0x1f83d9abfb41bd6bUL, 0x5be0cd19137e2179UL);

    // Absorbing salt, password and basil: this is the only place in which the block length is hard-coded to 512 bits
    for (int i = 0; i < 24; ++i)
    {
        roundLyra(state);
    }

    // state0
    lyraState->hl16[0] = state[0];
    lyraState->hl16[1] = state[1];
    // state1
    lyraState->hl16[2] = state[2];
    lyraState->hl16[3] = state[3];
    // state2
    lyraState->hl16[4] = state[4];
    lyraState->hl16[5] = state[5];
    // state3
    lyraState->hl16[6] = state[6];
    lyraState->hl16[7] = state[7];

    barrier(CLK_GLOBAL_MEM_FENCE);
}

// lyra2 p2
__attribute__((reqd_work_group_size(WORKSIZE8, 1, 1)))
__kernel void search2(__global uchar* sharedDataBuf)
{
    uint gid = get_global_id(0);
    uint lIdx = (uint)get_local_id(0);

    __global lyraState_t *lyraState = (__global lyraState_t *)(sharedDataBuf + ((8 * 4  * 4 + 8 * 192 * 4) * ((gid-get_global_offset(0)) >> 2)));
    //__global ulong *lMatrix = (__global ulong *)(sharedDataBuf + (8 * 4 * 4 + (8 * 192 * (lIdx & 3)) + (8 * 4  * 4 + 8 * 192 * 4) * ((gid-get_global_offset(0)) >> 2)));

    __local struct SharedState smState[WORKSIZE8];

    ulong state[4];
    ulong ttr;
    ulong tmpRes;

    uint2 st2;

    
    uint gr4 = ((lIdx >> 2) << 2);

    //-------------------------------------
    // Load Lyra state
    state[0] = (ulong)(lyraState->h8[(lIdx & 3)]);
    state[1] = (ulong)(lyraState->h8[(lIdx & 3)+4]);
    state[2] = (ulong)(lyraState->h8[(lIdx & 3)+8]);
    state[3] = (ulong)(lyraState->h8[(lIdx & 3)+12]);

    //-------------------------------------
    ulong lMatrix[192];
    ulong state0[24];
    ulong state1[24];

    // loop 1
    {
        state0[21] = state[0];
        state0[22] = state[1];
        state0[23] = state[2];
        
        roundLyra_sm(state);
        
        state0[18] = state[0];
        state0[19] = state[1];
        state0[20] = state[2];
        
        roundLyra_sm(state);
        
        state0[15] = state[0];
        state0[16] = state[1];
        state0[17] = state[2];
        
        roundLyra_sm(state);
        
        state0[12] = state[0];
        state0[13] = state[1];
        state0[14] = state[2];
        
        roundLyra_sm(state);
        state0[ 9] = state[0];
        state0[10] = state[1];
        state0[11] = state[2];
        
        roundLyra_sm(state);
        
        state0[6] = state[0];
        state0[7] = state[1];
        state0[8] = state[2];
        
        roundLyra_sm(state);
        
        state0[3] = state[0];
        state0[4] = state[1];
        state0[5] = state[2];
        
        roundLyra_sm(state);
        
        state0[0] = state[0];
        state0[1] = state[1];
        state0[2] = state[2];
        
        roundLyra_sm(state);
    }

    // loop 2
    {
        state[0] ^= state0[0];
        state[1] ^= state0[1];
        state[2] ^= state0[2];
        roundLyra_sm(state);
        state1[21] = state0[0] ^ state[0];
        state1[22] = state0[1] ^ state[1];
        state1[23] = state0[2] ^ state[2];
        
        
        state[0] ^= state0[3];
        state[1] ^= state0[4];
        state[2] ^= state0[5];
        roundLyra_sm(state);
        state1[18] = state0[3] ^ state[0];
        state1[19] = state0[4] ^ state[1];
        state1[20] = state0[5] ^ state[2];
        
        
        state[0] ^= state0[6];
        state[1] ^= state0[7];
        state[2] ^= state0[8];
        roundLyra_sm(state);
        state1[15] = state0[6] ^ state[0];
        state1[16] = state0[7] ^ state[1];
        state1[17] = state0[8] ^ state[2];
        
        
        state[0] ^= state0[ 9];
        state[1] ^= state0[10];
        state[2] ^= state0[11];
        roundLyra_sm(state);
        state1[12] = state0[ 9] ^ state[0];
        state1[13] = state0[10] ^ state[1];
        state1[14] = state0[11] ^ state[2];

        state[0] ^= state0[12];
        state[1] ^= state0[13];
        state[2] ^= state0[14];
        roundLyra_sm(state);
        state1[ 9] = state0[12] ^ state[0];
        state1[10] = state0[13] ^ state[1];
        state1[11] = state0[14] ^ state[2];
        
        
        state[0] ^= state0[15];
        state[1] ^= state0[16];
        state[2] ^= state0[17];
        roundLyra_sm(state);
        state1[6] = state0[15] ^ state[0];
        state1[7] = state0[16] ^ state[1];
        state1[8] = state0[17] ^ state[2];
        
        
        state[0] ^= state0[18];
        state[1] ^= state0[19];
        state[2] ^= state0[20];
        roundLyra_sm(state);
        state1[3] = state0[18] ^ state[0];
        state1[4] = state0[19] ^ state[1];
        state1[5] = state0[20] ^ state[2];
        
        
        state[0] ^= state0[21];
        state[1] ^= state0[22];
        state[2] ^= state0[23];
        roundLyra_sm(state);
        state1[0] = state0[21] ^ state[0];
        state1[1] = state0[22] ^ state[1];
        state1[2] = state0[23] ^ state[2];
    }

    ulong state2[3];
    ulong t0,c0;
    loop3p1_iteration(  0,  1,  2, 69, 70, 71); // inc dec
    loop3p1_iteration(  3,  4,  5, 66, 67, 68);
    loop3p1_iteration(  6,  7,  8, 63, 64, 65);
    loop3p1_iteration(  9, 10, 11, 60, 61, 62);
    loop3p1_iteration( 12, 13, 14, 57, 58, 59);
    loop3p1_iteration( 15, 16, 17, 54, 55, 56);
    loop3p1_iteration( 18, 19, 20, 51, 52, 53);
    loop3p1_iteration( 21, 22, 23, 48, 49, 50); // 1 0 2 // 48 24 72

    loop3p2_iteration(  0,  1,  2, 21, 22, 23, 93, 94, 95, 24, 25, 26); // dec inc
    loop3p2_iteration(  3,  4,  5, 18, 19, 20, 90, 91, 92, 27, 28, 29);
    loop3p2_iteration(  6,  7,  8, 15, 16, 17, 87, 88, 89, 30, 31, 32);
    loop3p2_iteration(  9, 10, 11, 12, 13, 14, 84, 85, 86, 33, 34, 35);
    loop3p2_iteration( 12, 13, 14,  9, 10, 11, 81, 82, 83, 36, 37, 38);
    loop3p2_iteration( 15, 16, 17,  6,  7,  8, 78, 79, 80, 39, 40, 41);
    loop3p2_iteration( 18, 19, 20,  3,  4,  5, 75, 76, 77, 42, 43, 44);
    loop3p2_iteration( 21, 22, 23,  0,  1,  2, 72, 73, 74, 45, 46, 47); // 2 1 3 // 72 48 96

#pragma unroll 24
    for (int i = 0; i < 24; i++) {
        state1[i] = state0[i];
        state0[i] = (lMatrix)[i];
    }

    loop3p1_iteration(  0,  1,  2,117,118,119); // inc dec
    loop3p1_iteration(  3,  4,  5,114,115,116);
    loop3p1_iteration(  6,  7,  8,111,112,113);
    loop3p1_iteration(  9, 10, 11,108,109,110);
    loop3p1_iteration( 12, 13, 14,105,106,107);
    loop3p1_iteration( 15, 16, 17,102,103,104);
    loop3p1_iteration( 18, 19, 20,99,100, 101);
    loop3p1_iteration( 21, 22, 23, 96, 97, 98); // 3 0 4 // 96 24 120


    loop3p2_iteration(  0,  1,  2, 21, 22, 23,141,142,143, 72, 73, 74); // dec inc
    loop3p2_iteration(  3,  4,  5, 18, 19, 20,138,139,140, 75, 76, 77);
    loop3p2_iteration(  6,  7,  8, 15, 16, 17,135,136,137, 78, 79, 80);
    loop3p2_iteration(  9, 10, 11, 12, 13, 14,132,133,134, 81, 82, 83);
    loop3p2_iteration( 12, 13, 14,  9, 10, 11,129,130,131, 84, 85, 86);
    loop3p2_iteration( 15, 16, 17,  6,  7,  8,126,127,128, 87, 88, 89);
    loop3p2_iteration( 18, 19, 20,  3,  4,  5,123,124,125, 90, 91, 92);
    loop3p2_iteration( 21, 22, 23,  0,  1,  2,120,121,122, 93, 94, 95); // 4 3 5 // 120 96 144


    ulong temp0[24];
#pragma unroll 24
    for (int i = 0; i < 24; i++) {
        state1[i] = state0[i];
        state0[i] = (lMatrix)[i + 48];
        temp0[i] = (lMatrix)[i];
    }

    loop3p1_iteration(  0,  1,  2,165,166,167); // inc dec
    loop3p1_iteration(  3,  4,  5,162,163,164);
    loop3p1_iteration(  6,  7,  8,159,160,161);
    loop3p1_iteration(  9, 10, 11,156,157,158);
    loop3p1_iteration( 12, 13, 14,153,154,155);
    loop3p1_iteration( 15, 16, 17,150,151,152);
    loop3p1_iteration( 18, 19, 20,147,148,149);
    loop3p1_iteration( 21, 22, 23,144,145,146); // 5 2 6 // 144 72 168


#pragma unroll 24
    for (int i = 0; i < 24; i++) {
        state1[i] = (lMatrix)[i + 24];
        (lMatrix)[i + 48] = (lMatrix)[i];
        (lMatrix)[i] = temp0[i];
    }


    loop3p2_iteration(  0,  1,  2, 21, 22, 23,189,190,191, 24, 25, 26); // dec inc
    loop3p2_iteration(  3,  4,  5, 18, 19, 20,186,187,188, 27, 28, 29);
    loop3p2_iteration(  6,  7,  8, 15, 16, 17,183,184,185, 30, 31, 32);
    loop3p2_iteration(  9, 10, 11, 12, 13, 14,180,181,182, 33, 34, 35);
    loop3p2_iteration( 12, 13, 14,  9, 10, 11,177,178,179, 36, 37, 38);
    loop3p2_iteration( 15, 16, 17,  6,  7,  8,174,175,176, 39, 40, 41);
    loop3p2_iteration( 18, 19, 20,  3,  4,  5,171,172,173, 42, 43, 44);
    loop3p2_iteration( 21, 22, 23,  0,  1,  2,168,169,170, 45, 46, 47); // 6 1 7 // 168 48 192

    ulong a_state1_0, a_state1_1, a_state1_2;
    ulong a_state2_0, a_state2_1, a_state2_2;
    ulong b0,b1;

#pragma unroll 24
    for (int i = 0; i < 24; i++) {
        state0[i] = (lMatrix)[168 + i];
    }

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    uint rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration(168,169,170,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170,  0,  1,  2); // 7 0 // 192 24
    wanderIteration(171,172,173,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173,  3,  4,  5);
    wanderIteration(174,175,176,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176,  6,  7,  8);
    wanderIteration(177,178,179,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179,  9, 10, 11);
    wanderIteration(180,181,182, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182, 12, 13, 14);
    wanderIteration(183,184,185, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185, 15, 16, 17);
    wanderIteration(186,187,188, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188, 18, 19, 20);
    wanderIteration(189,190,191, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191, 21, 22, 23);


    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration(  0,  1,  2,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170, 72, 73, 74); // 0 3 // 24 96
    wanderIteration(  3,  4,  5,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173, 75, 76, 77);
    wanderIteration(  6,  7,  8,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176, 78, 79, 80);
    wanderIteration(  9, 10, 11,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179, 81, 82, 83);
    wanderIteration( 12, 13, 14, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182, 84, 85, 86);
    wanderIteration( 15, 16, 17, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185, 87, 88, 89);
    wanderIteration( 18, 19, 20, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188, 90, 91, 92);
    wanderIteration( 21, 22, 23, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191, 93, 94, 95);

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration( 72, 73, 74,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170,144,145,146); // 3 6 // 96 168
    wanderIteration( 75, 76, 77,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173,147,148,149);
    wanderIteration( 78, 79, 80,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176,150,151,152);
    wanderIteration( 81, 82, 83,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179,153,154,155);
    wanderIteration( 84, 85, 86, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182,156,157,158);
    wanderIteration( 87, 88, 89, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185,159,160,161);
    wanderIteration( 90, 91, 92, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188,162,163,164);
    wanderIteration( 93, 94, 95, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191,165,166,167);

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration(144,145,146,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170, 24, 25, 26); // 6 1 // 168 48
    wanderIteration(147,148,149,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173, 27, 28, 29);
    wanderIteration(150,151,152,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176, 30, 31, 32);
    wanderIteration(153,154,155,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179, 33, 34, 35);
    wanderIteration(156,157,158, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182, 36, 37, 38);
    wanderIteration(159,160,161, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185, 39, 40, 41);
    wanderIteration(162,163,164, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188, 42, 43, 44);
    wanderIteration(165,166,167, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191, 45, 46, 47);

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration( 24, 25, 26,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170, 96, 97, 98); // 1 4 // 48 120
    wanderIteration( 27, 28, 29,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173, 99,100,101);
    wanderIteration( 30, 31, 32,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176,102,103,104);
    wanderIteration( 33, 34, 35,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179,105,106,107);
    wanderIteration( 36, 37, 38, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182,108,109,110);
    wanderIteration( 39, 40, 41, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185,111,112,113);
    wanderIteration( 42, 43, 44, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188,114,115,116);
    wanderIteration( 45, 46, 47, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191,117,118,119);

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration( 96, 97, 98,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170,168,169,170); // 4 7 // 120 192
    wanderIteration( 99,100,101,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173,171,172,173);
    wanderIteration(102,103,104,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176,174,175,176);
    wanderIteration(105,106,107,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179,177,178,179);
    wanderIteration(108,109,110, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182,180,181,182);
    wanderIteration(111,112,113, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185,183,184,185);
    wanderIteration(114,115,116, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188,186,187,188);
    wanderIteration(117,118,119, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191,189,190,191);

    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;
    wanderIteration(168,169,170,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170, 48, 49, 50); // 7 2 // 192 72
    wanderIteration(171,172,173,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173, 51, 52, 53);
    wanderIteration(174,175,176,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176, 54, 55, 56);
    wanderIteration(177,178,179,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179, 57, 58, 59);
    wanderIteration(180,181,182, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182, 60, 61, 62);
    wanderIteration(183,184,185, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185, 63, 64, 65);
    wanderIteration(186,187,188, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188, 66, 67, 68);
    wanderIteration(189,190,191, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191, 69, 70, 71);

    //------------------------------------
    // Wandering phase part2 (last iteration)
    smState[lIdx].s0 = state[0];
    barrier(CLK_LOCAL_MEM_FENCE);
    rowa = (uint)smState[gr4].s0 & 7;

    int i, j;
    ulong last[3];

    b0 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[0]: (lMatrix)[48]) : ((rowa < 6) ? (lMatrix)[96]: (lMatrix)[144]);
    b1 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[24]: (lMatrix)[72]) : ((rowa < 6) ? (lMatrix)[120]: (lMatrix)[168]);
    last[0] = ((rowa & 0x1U) < 1)? b0: b1;

    b0 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[1]: (lMatrix)[49]) : ((rowa < 6) ? (lMatrix)[97]: (lMatrix)[145]);
    b1 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[25]: (lMatrix)[73]) : ((rowa < 6) ? (lMatrix)[121]: (lMatrix)[169]);
    last[1] = ((rowa & 0x1U) < 1)? b0: b1;

    b0 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[2]: (lMatrix)[50]) : ((rowa < 6) ? (lMatrix)[98]: (lMatrix)[146]);
    b1 = (rowa < 4)? ((rowa < 2) ? (lMatrix)[26]: (lMatrix)[74]) : ((rowa < 6) ? (lMatrix)[122]: (lMatrix)[170]);
    last[2] = ((rowa & 0x1U) < 1)? b0: b1;


    t0 = (lMatrix)[48];
    c0 = last[0] + t0;
    state[0] ^= c0;
    
    t0 = (lMatrix)[49];
    c0 = last[1] + t0;
    state[1] ^= c0;
    
    t0 = (lMatrix)[50];
    c0 = last[2] + t0;
    state[2] ^= c0;

    roundLyra_sm(state);
   
    smState[lIdx].s0 = state[0];
    smState[lIdx].s1 = state[1];
    smState[lIdx].s2 = state[2];
    barrier(CLK_LOCAL_MEM_FENCE);
    ulong Data0 = smState[gr4 + ((lIdx-1) & 3)].s0;
    ulong Data1 = smState[gr4 + ((lIdx-1) & 3)].s1;
    ulong Data2 = smState[gr4 + ((lIdx-1) & 3)].s2;  
    if((lIdx&3) == 0)
    {
        last[1] ^= Data0;
        last[2] ^= Data1;
        last[0] ^= Data2;
    }
    else
    {
        last[0] ^= Data0;
        last[1] ^= Data1;
        last[2] ^= Data2;
    }

    if(rowa == 5)
    {
        last[0] ^= state[0];
        last[1] ^= state[1];
        last[2] ^= state[2];
    }

    //wanderIteration( 48, 49, 50,  0,  1,  2, 24, 25, 26, 48, 49, 50, 72, 73, 74, 96, 97, 98,120,121,122,144,145,146,168,169,170); // 2 5 // 72 144
    wanderIterationP2( 51, 52, 53,  3,  4,  5, 27, 28, 29, 51, 52, 53, 75, 76, 77, 99,100,101,123,124,125,147,148,149,171,172,173);
    wanderIterationP2( 54, 55, 56,  6,  7,  8, 30, 31, 32, 54, 55, 56, 78, 79, 80,102,103,104,126,127,128,150,151,152,174,175,176);
    wanderIterationP2( 57, 58, 59,  9, 10, 11, 33, 34, 35, 57, 58, 59, 81, 82, 83,105,106,107,129,130,131,153,154,155,177,178,179);
    wanderIterationP2( 60, 61, 62, 12, 13, 14, 36, 37, 38, 60, 61, 62, 84, 85, 86,108,109,110,132,133,134,156,157,158,180,181,182);
    wanderIterationP2( 63, 64, 65, 15, 16, 17, 39, 40, 41, 63, 64, 65, 87, 88, 89,111,112,113,135,136,137,159,160,161,183,184,185);
    wanderIterationP2( 66, 67, 68, 18, 19, 20, 42, 43, 44, 66, 67, 68, 90, 91, 92,114,115,116,138,139,140,162,163,164,186,187,188);
    wanderIterationP2( 69, 70, 71, 21, 22, 23, 45, 46, 47, 69, 70, 71, 93, 94, 95,117,118,119,141,142,143,165,166,167,189,190,191);

    state[0] ^= last[0];
    state[1] ^= last[1];
    state[2] ^= last[2];

    //-------------------------------------
    // save lyra state    
    lyraState->h8[(lIdx & 3)] = state[0];
    lyraState->h8[(lIdx & 3)+4] = state[1];
    lyraState->h8[(lIdx & 3)+8] = state[2];
    lyraState->h8[(lIdx & 3)+12] = state[3];
    
    barrier(CLK_GLOBAL_MEM_FENCE);
}

// lyra2 p3

__attribute__((reqd_work_group_size(WORKSIZE2, 1, 1)))
__kernel void search3(__global uint* hashes, __global uchar* sharedDataBuf)
{
    int gid = get_global_id(0);

    __global hash2_t *hash = (__global hash2_t *)(hashes + (8* (gid-get_global_offset(0))));
    __global lyraState_t *lyraState = (__global lyraState_t *)(sharedDataBuf + ((8 * 4  * 4 + 8 * 192 * 4) * (gid-get_global_offset(0))));

    ulong ttr;

    ulong2 state[8];
    // 1. load lyra State
    state[0] = lyraState->hl16[0];
    state[1] = lyraState->hl16[1];
    state[2] = lyraState->hl16[2];
    state[3] = lyraState->hl16[3];
    state[4] = lyraState->hl16[4];
    state[5] = lyraState->hl16[5];
    state[6] = lyraState->hl16[6];
    state[7] = lyraState->hl16[7];

    // 2. rounds
    for (int i = 0; i < 12; ++i)
    {
        roundLyra(state);
    }

    // 3. store result
    hash->hl16[0] = state[0];
    hash->hl16[1] = state[1];
    
    barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search4(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // jh quark_jh512_cpu_hash_64

  sph_u64 h0h = C64e(0x6fd14b963e00aa17), h0l = C64e(0x636a2e057a15d543), h1h = C64e(0x8a225e8d0c97ef0b), h1l = C64e(0xe9341259f2b3c361), h2h = C64e(0x891da0c1536f801e), h2l = C64e(0x2aa9056bea2b6d80), h3h = C64e(0x588eccdb2075baa6), h3l = C64e(0xa90f3a76baf83bf7);
  sph_u64 h4h = C64e(0x0169e60541e34a69), h4l = C64e(0x46b58a8e2e6fe65a), h5h = C64e(0x1047a7d0c1843c24), h5l = C64e(0x3b6e71b12d5ac199), h6h = C64e(0xcf57f6ec9db1f856), h6l = C64e(0xa706887c5716b156), h7h = C64e(0xe3c2fcdfe68517fb), h7l = C64e(0x545a4678cc8cdd4b);
  sph_u64 tmp;

  for(int i = 0; i < 2; i++)
  {
  if (i == 0)
  {
    h0h ^= (hash->h8[0]);
    h0l ^= (hash->h8[1]);
    h1h ^= (hash->h8[2]);
    h1l ^= (hash->h8[3]);
    h2h ^= (hash->h8[4]);
    h2l ^= (hash->h8[5]);
    h3h ^= (hash->h8[6]);
    h3l ^= (hash->h8[7]);
  }
  else if(i == 1)
  {
    h4h ^= (hash->h8[0]);
    h4l ^= (hash->h8[1]);
    h5h ^= (hash->h8[2]);
    h5l ^= (hash->h8[3]);
    h6h ^= (hash->h8[4]);
    h6l ^= (hash->h8[5]);
    h7h ^= (hash->h8[6]);
    h7l ^= (hash->h8[7]);

    h0h ^= 0x80;
    h3l ^= 0x2000000000000;
  }
  E8;
  }
  h4h ^= 0x80;
  h7l ^= 0x2000000000000;

  hash->h8[0] = (h4h);
  hash->h8[1] = (h4l);
  hash->h8[2] = (h5h);
  hash->h8[3] = (h5l);
  hash->h8[4] = (h6h);
  hash->h8[5] = (h6l);
  hash->h8[6] = (h7h);
  hash->h8[7] = (h7l);

  barrier(CLK_GLOBAL_MEM_FENCE);
}


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search5(__global hash_t* hashes, __global hash_t* branches, __global uchar* nonceBranches)
{
// phi_filter_cuda

  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);
  __global hash_t *branch = &(branches[gid-get_global_offset(0)]);
  __global uchar *nonceBranch = &(nonceBranches[gid-get_global_offset(0)]);
  *nonceBranch = hash->h1[0] & 1;
	if (*nonceBranch) return;
  __global uint4 *pdst = (__global uint4*)((branch));
  __global uint4 *psrc = (__global uint4*)((hash));
  uint4 data;
	data = psrc[0]; pdst[0] = data;
	data = psrc[1]; pdst[1] = data;
	data = psrc[2]; pdst[2] = data;
	data = psrc[3]; pdst[3] = data;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search6(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  // gost streebog_cpu_hash_64

  sph_u64 message[8], out[8];
  sph_u64 len = 512;

  __local sph_u64 lT[8][256];

  int init = get_local_id(0);
  int step = get_local_size(0);

  _Pragma("unroll") for(int j=init;j<256;j+=step) {
      _Pragma("unroll") for (int i=0; i<8; i++) lT[i][j] = T[i][j];
  }
  barrier(CLK_LOCAL_MEM_FENCE);

  message[0] = (hash->h8[0]);
  message[1] = (hash->h8[1]);
  message[2] = (hash->h8[2]);
  message[3] = (hash->h8[3]);
  message[4] = (hash->h8[4]);
  message[5] = (hash->h8[5]);
  message[6] = (hash->h8[6]);
  message[7] = (hash->h8[7]);

  GOST_HASH_512(message, out);

  hash->h8[0] = (out[0]);
  hash->h8[1] = (out[1]);
  hash->h8[2] = (out[2]);
  hash->h8[3] = (out[3]);
  hash->h8[4] = (out[4]);
  hash->h8[5] = (out[5]);
  hash->h8[6] = (out[6]);
  hash->h8[7] = (out[7]);

  barrier(CLK_GLOBAL_MEM_FENCE);
}


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search7(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

// x11_echo512_cpu_hash_64

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

  hash->h8[0] = Vb00;
  hash->h8[1] = Vb01;
  hash->h8[2] = Vb10;
  hash->h8[3] = Vb11;
  hash->h8[4] = Vb20;
  hash->h8[5] = Vb21;
  hash->h8[6] = Vb30;
  hash->h8[7] = Vb31;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search8(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

// x11_echo512_cpu_hash_64

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

  hash->h8[0] = Vb00;
  hash->h8[1] = Vb01;
  hash->h8[2] = Vb10;
  hash->h8[3] = Vb11;
  hash->h8[4] = Vb20;
  hash->h8[5] = Vb21;
  hash->h8[6] = Vb30;
  hash->h8[7] = Vb31;

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search9(__global hash_t* hashes, __global hash_t* branches, __global uchar* nonceBranches)
{
//phi_merge_cuda
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);
  __global hash_t *branch = &(branches[gid-get_global_offset(0)]);
  __global uchar *nonceBranch = &(nonceBranches[gid-get_global_offset(0)]);
  if (*nonceBranch) return;
  __global uint4 *pdst = (__global uint4*)((hash));
  __global uint4 *psrc = (__global uint4*)((branch));
  uint4 data;
  data = psrc[0]; pdst[0] = data;
  data = psrc[1]; pdst[1] = data;
  data = psrc[2]; pdst[2] = data;
  data = psrc[3]; pdst[3] = data;
  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search10(__global hash_t* hashes)
{
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

// quark_skein512_cpu_hash_64

  sph_u64 h0 = SPH_C64(0x4903ADFF749C51CE), h1 = SPH_C64(0x0D95DE399746DF03), h2 = SPH_C64(0x8FD1934127C79BCE), h3 = SPH_C64(0x9A255629FF352CB1), h4 = SPH_C64(0x5DB62599DF6CA7B0), h5 = SPH_C64(0xEABE394CA9D5C3F4), h6 = SPH_C64(0x991112C71A75B523), h7 = SPH_C64(0xAE18A40B660FCC33);
  sph_u64 m0, m1, m2, m3, m4, m5, m6, m7;
  sph_u64 bcount = 0;

  m0 = (hash->h8[0]);
  m1 = (hash->h8[1]);
  m2 = (hash->h8[2]);
  m3 = (hash->h8[3]);
  m4 = (hash->h8[4]);
  m5 = (hash->h8[5]);
  m6 = (hash->h8[6]);
  m7 = (hash->h8[7]);
  UBI_BIG(480, 64);
  bcount = 0;
  m0 = m1 = m2 = m3 = m4 = m5 = m6 = m7 = 0;
  UBI_BIG(510, 8);
  hash->h8[0] = (h0);
  hash->h8[1] = (h1);
  hash->h8[2] = (h2);
  hash->h8[3] = (h3);
  hash->h8[4] = (h4);
  hash->h8[5] = (h5);
  hash->h8[6] = (h6);
  hash->h8[7] = (h7);

  barrier(CLK_GLOBAL_MEM_FENCE);
}

__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search11(__global hash_t* hashes, __global uint* output, const ulong target)
{
// phi_final_compress_cuda
  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

  __global uint2 *pdst = (__global uint2*)((hash));
  __global uint2 *psrc = (__global uint2*)((hash));
  uint2 data;
  data = psrc[4]; pdst[0] ^= data;
  data = psrc[5]; pdst[1] ^= data;
  data = psrc[6]; pdst[2] ^= data;
  data = psrc[7]; pdst[3] ^= data;

  bool result = ((hash->h8[3]) <= target);
  if (result)
    output[atomic_inc(output+0xFF)] = SWAP4(gid);

  barrier(CLK_GLOBAL_MEM_FENCE);
}
