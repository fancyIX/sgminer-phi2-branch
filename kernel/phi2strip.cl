/* Phi2 kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2016 tpruvot
 * Copyright (c) 2018 fancyIX
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

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)
#define SWAP32(x) as_ulong(as_uint2(x).s10)

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

#define WOLF_JH_64BIT 1

#if defined(__GCNMINC__)
uint2 __attribute__((overloadable)) amd_bitalign(uint2 src0, uint2 src1, uint src2)
{
	uint dstx = 0;
	uint dsty = 0;
    __asm ("v_alignbit_b32 %[dstx], %[src0x], %[src1x], %[src2x]\n"
          "v_alignbit_b32 %[dsty], %[src0y], %[src1y], %[src2y]"
          : [dstx] "=&v" (dstx), [dsty] "=&v" (dsty)
          : [src0x] "v" (src0.x), [src1x] "v" (src1.x), [src2x] "v" (src2),
		    [src0y] "v" (src0.y), [src1y] "v" (src1.y), [src2y] "v" (src2));
	return (uint2) (dstx, dsty);
}
uint2 __attribute__((overloadable)) amd_bytealign(uint2 src0, uint2 src1, uint src2)
{
	uint dstx = 0;
	uint dsty = 0;
    __asm ("v_alignbyte_b32 %[dstx], %[src0x], %[src1x], %[src2x]\n"
          "v_alignbyte_b32 %[dsty], %[src0y], %[src1y], %[src2y]"
          : [dstx] "=&v" (dstx), [dsty] "=&v" (dsty)
          : [src0x] "v" (src0.x), [src1x] "v" (src1.x), [src2x] "v" (src2),
		    [src0y] "v" (src0.y), [src1y] "v" (src1.y), [src2y] "v" (src2));
	return (uint2) (dstx, dsty);
}
#else
#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable
#endif

#include "lyra2mdz.cl"

/// lyra2 algo p2 


__attribute__((reqd_work_group_size(4, 5, 1)))
__kernel void search2(__global uchar* sharedDataBuf)
{
  uint gid = get_global_id(1);
  __global lyraState_t *lyraState = (__global lyraState_t *)(sharedDataBuf + ((8 * 4  * 4) * (gid-get_global_offset(1))));

  __local ulong roundPad[12 * 5];
  __local ulong *xchange = roundPad + get_local_id(1) * 4;

  //__global ulong *notepad = buffer + get_local_id(0) + 4 * SLOT;
  __local ulong notepadLDS[192 * 4 * 5];
  __local ulong *notepad = notepadLDS + LOCAL_LINEAR;
  const int player = get_local_id(0);

  ulong state[4];

  //-------------------------------------
  // Load Lyra state
  state[0] = (ulong)(lyraState->h8[player]);
  state[1] = (ulong)(lyraState->h8[player+4]);
  state[2] = (ulong)(lyraState->h8[player+8]);
  state[3] = (ulong)(lyraState->h8[player+12]);
  
  __local ulong *dst = notepad + HYPERMATRIX_COUNT;
  for (int loop = 0; loop < LYRA_ROUNDS; loop++) { // write columns and rows 'in order'
    dst -= STATE_BLOCK_COUNT; // but blocks backwards
    for(int cp = 0; cp < 3; cp++) dst[cp * REG_ROW_COUNT] = state[cp];
    round_lyra_4way(state, xchange);
  }
  
  make_hyper_one(state, xchange, notepad);
  
  make_next_hyper(1, 0, 2, state, roundPad, notepad);
   
  make_next_hyper(2, 1, 3, state, roundPad, notepad);
  
  make_next_hyper(3, 0, 4, state, roundPad, notepad);
 
  make_next_hyper(4, 3, 5, state, roundPad, notepad);
  
  make_next_hyper(5, 2, 6, state, roundPad, notepad);
  make_next_hyper(6, 1, 7, state, roundPad, notepad);

  uint modify;
  uint row = 0;
  uint pre = 7;

  __local uint *shorter = (__local uint*)roundPad;
  for (int i = 0; i < LYRA_ROUNDS; i++) {
    if(get_local_id(0) == 0) {
      shorter[get_local_id(1)] = (uint)(state[0] % 8);
    }
    barrier(CLK_LOCAL_MEM_FENCE); // nop
    modify = shorter[get_local_id(1)];
    hyper_xor(pre, modify, row, state, roundPad, notepad);
    pre = row;
    row = (row + 3) % 8;
  }

  notepad += HYPERMATRIX_COUNT * modify;
  for(int loop = 0; loop < 3; loop++) state[loop] ^= notepad[loop * REG_ROW_COUNT];
/**/
  //-------------------------------------
  // save lyra state  

#if 1
  lyraState->h8[player] = state[0];
  lyraState->h8[player+4] = state[1];
  lyraState->h8[player+8] = state[2];
  lyraState->h8[player+12] = state[3];
#else
  lyraState->h8[player] = notepad[HYPERMATRIX_COUNT *6 + 2 * STATE_BLOCK_COUNT];
  lyraState->h8[player+4] = notepad[HYPERMATRIX_COUNT * 6 + 2 * STATE_BLOCK_COUNT + 1* REG_ROW_COUNT];
  lyraState->h8[player+8] = notepad[HYPERMATRIX_COUNT * 6 + 2 * STATE_BLOCK_COUNT + 2* REG_ROW_COUNT];
  //lyraState->h8[player+12] = notepad[24];
#endif
  barrier(CLK_GLOBAL_MEM_FENCE);
}