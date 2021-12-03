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
#define SPH_T32(x)    ((x) & SPH_C32(0xFFFFFFFF))

#define SPH_C64(x)    ((sph_u64)(x ## UL))
#define SPH_T64(x)    ((x) & SPH_C64(0xFFFFFFFFFFFFFFFF))

#define SPH_ROTL32(x,n) rotate(x,(uint)n)     //faster with driver 14.6
#define SPH_ROTR32(x,n) rotate(x,(uint)(32-n))
#define SPH_ROTL64(x,n) rotate(x,(ulong)n)
#define SPH_ROTR64(x,n) rotate(x,(ulong)(64-n))

#define SWAP4(x) as_uint(as_uchar4(x).wzyx)

#include "keccak1600.cl"

typedef union {
    uint h4[8];
    ulong h8[4];
    uint4 h16[2];
    ulong2 hl16[2];
    ulong4 h32;
} hash_t;


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global uint *header, __constant uint* gmatrix, __global uint* output, const ulong target)
{
    uint tid = get_local_id(0);
    uint gid = get_global_id(0);
    hash_t hash;

    uint pdata[50] = {0};
    for (int i = 0; i < 19; i++) {
        pdata[i] = header[i];
    }
    pdata[19] = gid;

    uchar hash_first[32];
    uchar hash_second[32];
    uchar hash_xored[32];

    __local uint vector[64 * WORKSIZE];
    uint product[64];

    ((uchar *) pdata)[80] = 0x06;
    ((uchar *) pdata)[135] = 0x80;

    keccak_block(pdata);

    for (int i = 0; i < 4; i++) {
        ((ulong *)hash_first)[i] = ((ulong *) pdata)[i];
    }
    for (int i = 0; i < 32; ++i) {
        vector[(2*i) * WORKSIZE + tid] = (hash_first[i] >> 4);
        vector[(2*i+1) * WORKSIZE + tid] = hash_first[i] & 0xF;
    }

    for (int i = 0; i < 64; ++i) {
        uint sum = 0;
#ifdef __gfx900__
        #pragma nounroll
#endif
        for (int k = 0; k < 4; k++) {
            ulong2 buf0 = ((__constant ulong2 *)gmatrix)[i * 16 + k * 4 + 0];
            ulong2 buf1 = ((__constant ulong2 *)gmatrix)[i * 16 + k * 4 + 1];
            ulong2 buf2 = ((__constant ulong2 *)gmatrix)[i * 16 + k * 4 + 2];
            ulong2 buf3 = ((__constant ulong2 *)gmatrix)[i * 16 + k * 4 + 3];
            uint *m0 = (uint *)&buf0;
            for (int j = 0; j < 4; j++) {
                sum += m0[j] * vector[((k * 4 + 0) * 4 + j) * WORKSIZE + tid];
            }
            uint *m1 = (uint *)&buf1;
            for (int j = 0; j < 4; j++) {
                sum += m1[j] * vector[((k * 4 + 1) * 4 + j) * WORKSIZE + tid];
            }
            uint *m2 = (uint *)&buf2;
            for (int j = 0; j < 4; j++) {
                sum += m2[j] * vector[((k * 4 + 2) * 4 + j) * WORKSIZE + tid];
            }
            uint *m3 = (uint *)&buf3;
            for (int j = 0; j < 4; j++) {
                sum += m3[j] * vector[((k * 4 + 3) * 4 + j) * WORKSIZE + tid];
            }
        }
        product[(i)] = (sum >> 10);
    }

    for (int i = 0; i < 32; ++i) {
        hash_second[i] = (product[(2*i)] << 4) | (product[(2*i+1)]);
    }

    for (int i = 0; i < 32; ++i) {
        hash_xored[i] = hash_first[i] ^ hash_second[i];
    }

    uint tmp[50] = {0};
    for (int i = 0; i < 32; i++) {
        ((uchar *) tmp)[i] = hash_xored[i];
    }

    ((uchar *)tmp)[32] = 0x06;
    ((uchar *)tmp)[135] = 0x80;

    keccak_block(tmp);

    for (int i = 0; i < 4; i++) {
        hash.h8[i] = ((ulong *) tmp)[i];
    }

    bool result = ( hash.h8[3] <= target);
    if (result) {
		output[atomic_inc(output + 0xFF)] = SWAP4(gid);
	}

    barrier(CLK_GLOBAL_MEM_FENCE);
}