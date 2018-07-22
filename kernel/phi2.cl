/* Phi kernel implementation.
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
#include "lyra2v16.cl"

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
  unsigned char h1[32];
  unsigned short h2[16];
  uint h4[8];
  ulong h8[4];
} hash2_t;

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
__kernel void search(__global unsigned char* block, __global hash_t* hashes)
{
    uint gid = get_global_id(0);
    __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

// cubehash512_cuda_hash_80
    #ifdef DEBUG_PRINT
    if (gid == 0x12345) {
        printf("input: \n");
        printblock(block, 80);
    }
    #endif

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

    #ifdef DEBUG_PRINT
    if (gid == 0x12345) {
        printf("cubehash_80 output: \n");
        printhash(*hash);
    }
    #endif
    barrier(CLK_GLOBAL_MEM_FENCE);
}


// cubehash_144
__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search0(__global unsigned char* block, __global hash_t* hashes)
{
    uint gid = get_global_id(0);
    __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);

// cubehash512_cuda_hash_80
    #ifdef DEBUG_PRINT
    if (gid == 0x12345) {
        printf("input: \n");
        printblock(block, 80);
    }
    #endif

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

    #ifdef DEBUG_PRINT
    if (gid == 0x12345) {
        printf("cubehash_80 output: \n");
        printhash(*hash);
    }
    #endif
    barrier(CLK_GLOBAL_MEM_FENCE);
}


/// lyra2 algo 


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search1(__global uchar* hashes,__global uchar* matrix)
{
 uint gid = get_global_id(0);
 // __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);
  __global hash_t *hash = (__global hash_t *)(hashes + (8 * sizeof(ulong)* (gid - get_global_offset(0))));
  __global ulong4 *DMatrix = (__global ulong4 *)(matrix + (4 * memshift * 8 * 8 * 8 * (gid - get_global_offset(0))));

//  uint offset = (4 * memshift * 4 * 4 * sizeof(ulong)* (get_global_id(0) % MAX_GLOBAL_THREADS))/32;
  ulong4 state[4];

  state[0].x = hash->h8[0]; //password
  state[0].y = hash->h8[1]; //password
  state[0].z = hash->h8[2]; //password
  state[0].w = hash->h8[3]; //password
  state[1] = state[0];
  state[2] = (ulong4)(0x6a09e667f3bcc908UL, 0xbb67ae8584caa73bUL, 0x3c6ef372fe94f82bUL, 0xa54ff53a5f1d36f1UL);
  state[3] = (ulong4)(0x510e527fade682d1UL, 0x9b05688c2b3e6c1fUL, 0x1f83d9abfb41bd6bUL, 0x5be0cd19137e2179UL);


  for (int i = 0; i<12; i++) { round_lyra(state); } 

  //state[0] ^= (ulong4)(0x20,0x20,0x20,0x01);
  //state[1] ^= (ulong4)(0x08,0x08,0x80,0x0100000000000000);

  for (int i = 0; i<12; i++) { round_lyra(state); } 

// reducedsqueezedrow0
  uint ps1 = (memshift * 7);
//#pragma unroll 4
  for (int i = 0; i < 8; i++)
  {
	  uint s1 = ps1 - memshift * i;
	  for (int j = 0; j < 3; j++)
		  (DMatrix)[j+s1] = state[j];

	  round_lyra(state);
  }

  reduceDuplexf(state, DMatrix);
 
  reduceDuplexRowSetupf(1, 0, 2, state, DMatrix);
  reduceDuplexRowSetupf(2, 1, 3, state, DMatrix);
  reduceDuplexRowSetupf(3, 0, 4, state, DMatrix);
  reduceDuplexRowSetupf(4, 3, 5, state, DMatrix);
  reduceDuplexRowSetupf(5, 2, 6, state, DMatrix);
  reduceDuplexRowSetupf(6, 1, 7, state, DMatrix);

  uint rowa;

  rowa = state[0].x & 7;
  reduceDuplexRowf(7, rowa, 0, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(0, rowa, 3, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(3, rowa, 6, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(6, rowa, 1, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(1, rowa, 4, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(4, rowa, 7, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(7, rowa, 2, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(2, rowa, 5, state, DMatrix);

  uint shift = (memshift * 8 * rowa);

  for (int j = 0; j < 3; j++)
	  state[j] ^= (DMatrix)[j+shift];

  for (int i = 0; i < 12; i++)
	  round_lyra(state);

  for (int i = 0; i<4; i++) {hash->h8[i] = ((ulong*)state)[i];} 

//================================================= 2nd half

state[0].x = hash->h8[0 + 4]; //password
  state[0].y = hash->h8[1 + 4]; //password
  state[0].z = hash->h8[2 + 4]; //password
  state[0].w = hash->h8[3 + 4]; //password
  state[1] = state[0];
  state[2] = (ulong4)(0x6a09e667f3bcc908UL, 0xbb67ae8584caa73bUL, 0x3c6ef372fe94f82bUL, 0xa54ff53a5f1d36f1UL);
  state[3] = (ulong4)(0x510e527fade682d1UL, 0x9b05688c2b3e6c1fUL, 0x1f83d9abfb41bd6bUL, 0x5be0cd19137e2179UL);


for (int i = 0; i<12; i++) { round_lyra(state); } 

  //state[0] ^= (ulong4)(0x20,0x20,0x20,0x01);
  //state[1] ^= (ulong4)(0x08,0x08,0x80,0x0100000000000000);

  for (int i = 0; i<12; i++) { round_lyra(state); } 

// reducedsqueezedrow0
   ps1 = (memshift * 7);
//#pragma unroll 4
  for (int i = 0; i < 8; i++)
  {
	  uint s1 = ps1 - memshift * i;
	  for (int j = 0; j < 3; j++)
		  (DMatrix)[j+s1] = state[j];

	  round_lyra(state);
  }

  reduceDuplexf(state, DMatrix);
 
  reduceDuplexRowSetupf(1, 0, 2, state, DMatrix);
  reduceDuplexRowSetupf(2, 1, 3, state, DMatrix);
  reduceDuplexRowSetupf(3, 0, 4, state, DMatrix);
  reduceDuplexRowSetupf(4, 3, 5, state, DMatrix);
  reduceDuplexRowSetupf(5, 2, 6, state, DMatrix);
  reduceDuplexRowSetupf(6, 1, 7, state, DMatrix);

  rowa = state[0].x & 7;
  reduceDuplexRowf(7, rowa, 0, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(0, rowa, 3, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(3, rowa, 6, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(6, rowa, 1, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(1, rowa, 4, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(4, rowa, 7, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(7, rowa, 2, state, DMatrix);
  rowa = state[0].x & 7;
  reduceDuplexRowf(2, rowa, 5, state, DMatrix);

  shift = (memshift * 8 * rowa);

  for (int j = 0; j < 3; j++)
	  state[j] ^= (DMatrix)[j+shift];

  for (int i = 0; i < 12; i++)
	  round_lyra(state);

  for (int i = 0; i<4; i++) {hash->h8[i + 4] = ((ulong*)state)[i];} 


barrier(CLK_LOCAL_MEM_FENCE);


}


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search2(__global hash_t* hashes)
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
__kernel void search3(__global hash_t* hashes, __global hash_t* branches, __global hash_t* nonceBranches)
{
// phi_filter_cuda

  uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);
  __global hash_t *branch = &(branches[gid-get_global_offset(0)]);
  __global hash_t *nonceBranch = &(nonceBranches[gid-get_global_offset(0)]);
     nonceBranch->h1[0] = hash->h1[0] & 1;
		if (nonceBranch->h1[0]) return;
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
__kernel void search4(__global hash_t* hashes)
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
__kernel void search5(__global hash_t* hashes)
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
__kernel void search6(__global hash_t* hashes)
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
__kernel void search7(__global hash_t* hashes, __global hash_t* branches, __global hash_t* nonceBranches)
{
//phi_merge_cuda
    uint gid = get_global_id(0);
  __global hash_t *hash = &(hashes[gid-get_global_offset(0)]);
  __global hash_t *branch = &(branches[gid-get_global_offset(0)]);
  __global hash_t *nonceBranch = &(nonceBranches[gid-get_global_offset(0)]);
		if (nonceBranch->h1[0]) return;
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
__kernel void search8(__global hash_t* hashes)
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
__kernel void search9(__global hash_t* hashes, __global uint* output, const ulong target)
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
