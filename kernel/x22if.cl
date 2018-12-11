#ifndef X22I_CL
#define X22I_CL
#pragma OPENCL EXTENSION cl_amd_printf : enable

#define DEBUG(x)

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

#define SPH_C32(x)    ((sph_u32)(x ## U))
#define SPH_T32(x) (as_uint(x))
#define SPH_ROTL32(x, n) rotate(as_uint(x), as_uint(n))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))
#define SPH_ROTL64(x, n) rotate(as_ulong(x), (n) & 0xFFFFFFFFFFFFFFFFUL)

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x) (as_ulong(x))

#define SPH_ECHO_64 1
#define SPH_KECCAK_64 1
#define SPH_JH_64 1
#define SPH_SIMD_NOCOPY 0
#define SPH_KECCAK_NOCOPY 0
#define SPH_SMALL_FOOTPRINT_GROESTL 0
#define SPH_GROESTL_BIG_ENDIAN 0
#define CUBEHASH_FORCED_UNROLL 4

#ifndef SPH_COMPACT_BLAKE_64
  #define SPH_COMPACT_BLAKE_64 0
#endif
#ifndef SPH_LUFFA_PARALLEL
  #define SPH_LUFFA_PARALLEL 0
#endif
#ifndef SPH_KECCAK_UNROLL
  #define SPH_KECCAK_UNROLL 0
#endif
#define SPH_HAMSI_EXPAND_BIG 1


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

ulong FAST_ROTL64_LO(const uint2 x, const uint y) { return(as_ulong(amd_bitalign(x, x.s10, 32 - y))); }
ulong FAST_ROTL64_HI(const uint2 x, const uint y) { return(as_ulong(amd_bitalign(x.s10, x, 32 - (y - 32)))); }
ulong ROTL64_1(const uint2 vv, const int r) { return as_ulong(amd_bitalign((vv).xy, (vv).yx, 32 - r)); }
ulong ROTL64_2(const uint2 vv, const int r) { return as_ulong((amd_bitalign((vv).yx, (vv).xy, 64 - r))); }

#define VSWAP8(x)	(((x) >> 56) | (((x) >> 40) & 0x000000000000FF00UL) | (((x) >> 24) & 0x0000000000FF0000UL) \
          | (((x) >>  8) & 0x00000000FF000000UL) | (((x) <<  8) & 0x000000FF00000000UL) \
          | (((x) << 24) & 0x0000FF0000000000UL) | (((x) << 40) & 0x00FF000000000000UL) | (((x) << 56) & 0xFF00000000000000UL))

#define WOLF_JH_64BIT 1



#include "swifftxf.cl"


#define SWAP4(x) as_uint(as_uchar4(x).wzyx)
#define SWAP8(x) as_ulong(as_uchar8(x).s76543210)
#define SWAP32(a)    (as_uint(as_uchar4(a).wzyx))

#if SPH_BIG_ENDIAN
  #define DEC64E(x) (x)
  #define DEC32E(x) (x)
  #define DEC64BE(x) (*(const __global sph_u64 *) (x))
  #define DEC32LE(x) SWAP4(*(const __global sph_u32 *) (x))
#else
  #define DEC64E(x) SWAP8(x)
  #define DEC32E(x) SWAP4(x)
  #define DEC64BE(x) SWAP8(*(const __global sph_u64 *) (x))
  #define DEC32LE(x) (*(const __global sph_u32 *) (x))
#endif

#define ENC64E DEC64E
#define ENC32E DEC32E

#define SHL(x, n) ((x) << (n))
#define SHR(x, n) ((x) >> (n))

typedef union {
  unsigned char h1[64];
  uint h4[16];
  ulong h8[8];
} hash_t;

typedef union {
    uint h[8];
    uint4 h4[2];
    ulong2 hl4[2];
    ulong4 h8;
} hashly_t;

// swifftx hash hash1 hash2 hash3
__attribute__((reqd_work_group_size(8, 32, 1)))
__kernel void search16(__global uint *g_hash, __global uint *g_hash1, __global uint *g_hash2, __global uint *g_hash3)
{
    uint gid = get_global_id(1);
    uint offset = get_global_offset(1);
    uint thread = gid - offset;
    uint tid = get_local_id(1);

    __global uchar8* inout = (__global uchar8*) &g_hash [thread<<4];
    __global uchar8* in1   = (__global uchar8*) &g_hash1[thread<<4];
    __global uchar8* in2   = (__global uchar8*) &g_hash2[thread<<4];
    __global uchar8* in3   = (__global uchar8*) &g_hash3[thread<<4];

  __local unsigned char S_SBox[256];
  __local swift_int16_t S_fftTable[256 * EIGHTH_N];
  __local swift_int16_t S_As[3 * SFT_M * SFT_N];
  swift_int32_t S_sum[3*SFT_N/ SFT_NSTRIDE];
  swift_int32_t T_sum[8];
  uchar8 S_intermediate[4];
  ushort S_carry;
  swift_int32_t pairs[EIGHTH_N / 2 ];
  char S_multipliers[8];
  uchar8 S_input[4];

#pragma unroll
  for (int i = 0; i < 8; i++) {
    S_multipliers[i] = multipliers[i + (SFT_STRIDE << 3)];
  }

const int blockSize = min(256, SFT_NSLOT); //blockDim.x;

  if (tid < 256) {
    #pragma unroll
    for (int i=0; i<(256/blockSize/8) && (tid % 8 == 0); i++) {
      ((__local ulong *)S_SBox)[SFT_STRIDE + SFT_NSTRIDE * (tid / 8 + blockSize * (i))] = ((__constant ulong *)SFT_SBox)[SFT_STRIDE + SFT_NSTRIDE * (tid / 8 + blockSize * (i))];
    }
#define SFT_IDX(i) (SFT_STRIDE + SFT_NSTRIDE * (tid + blockSize * (i)))
    #pragma unroll
    for (int i=0; i<(256 * EIGHTH_N)/blockSize/8/4; i++) {
      ((__local ulong *)S_fftTable)[SFT_IDX(i)] = ((__constant ulong *)fftTable)[SFT_IDX(i)];
    }
    #pragma unroll
    for (int i=0; i<(3 * SFT_M * SFT_N)/blockSize/8/4; i++) {
      ((__local ulong *)S_As)[SFT_IDX(i)] = ((__constant ulong *)As)[SFT_IDX(i)];
    }
    barrier(CLK_LOCAL_MEM_FENCE);
  }
  {
    __global unsigned char* inoutptr = (__global unsigned char*)inout;
      S_input[0] = inout[SFT_STRIDE];
      S_input[1] = in1[SFT_STRIDE];
      S_input[2] = in2[SFT_STRIDE];
      S_input[3] = in3[SFT_STRIDE];
    e_ComputeSingleSWIFFTX(inoutptr, S_input, S_SBox, S_As, S_fftTable, S_multipliers, S_sum, S_intermediate, S_carry, pairs,T_sum);
   }
   barrier(CLK_LOCAL_MEM_FENCE);
}

#endif