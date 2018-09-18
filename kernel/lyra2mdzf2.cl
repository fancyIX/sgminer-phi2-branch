/*
 * Lyra2RE kernel implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 * Copyright (c) 2014 djm34
 * Copyright (c) 2014 James Lovejoy
 * Copyright (c) 2017 MaxDZ8
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
 * @author   djm34
 * @author   fancyIX 2018
 */
/*
 * This file is mostly the same as lyra2rev2.cl: differences:
 * - Cubehash implementation is now shared across search2 and search5
 * - Cubehash is reformulated, this reduces ISA size to almost 1/3
 *   leaving rooms for other algorithms.
 * - Lyra is reformulated in 4 stages: arithmetically intensive
 *   head and tail, one coherent matrix expansion, one incoherent mess.
*/

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

#define ROTR64(x2, y) (y < 32 ? (y % 8 == 0 ? (((amd_bytealign(x2.s10, x2, y / 8)))) : (((amd_bitalign(x2.s10, x2, y))))) : (((amd_bitalign(x2, x2.s10, (y - 32))))))


/// lyra2 algo  ///////////////////////////////////////////////////////////
#define HASH_SIZE (256 / 8) // size in bytes of an hash in/out
#define SLOT (get_global_id(1) - get_global_offset(1))
#define LOCAL_LINEAR (get_local_id(0) & 3)
#define REG_ROW_COUNT (1) // ideally all happen at the same clock
#define STATE_BLOCK_COUNT (3 * REG_ROW_COUNT)  // very close instructions
#define LYRA_ROUNDS 8
#define HYPERMATRIX_COUNT (LYRA_ROUNDS * STATE_BLOCK_COUNT)

// Usually just #define G(a,b,c,d)...; I have no time to read the Lyra paper
// but that looks like some kind of block cipher I guess.
#define cipher_G_macro(s) \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = SWAP32(s[3]); \
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 24); \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = ROTR64(s[3], 16); \
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 63);

#define pull_state(state) \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p00], %[p00] quad_perm:[0,0,0,0]\n" \
	      "v_mov_b32_dpp  %[p01], %[p01] quad_perm:[0,0,0,0]\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] quad_perm:[1,1,1,1]\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] quad_perm:[1,1,1,1]\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] quad_perm:[2,2,2,2]\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] quad_perm:[2,2,2,2]\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] quad_perm:[3,3,3,3]\n" \
		  "s_nop 0" \
		  : [p00] "=&v" (state[0].x), \
		    [p01] "=&v" (state[0].y), \
			[p10] "=&v" (state[1].x), \
		    [p11] "=&v" (state[1].y), \
			[p20] "=&v" (state[2].x), \
			[p21] "=&v" (state[2].y), \
			[p30] "=&v" (state[3].x), \
			[p31] "=&v" (state[3].y) \
		  : [p00] "v" (state[0].x), \
		    [p00] "v" (state[0].x), \
		    [p10] "v" (state[1].x), \
		    [p11] "v" (state[1].y), \
			[p20] "v" (state[2].x), \
			[p21] "v" (state[2].y), \
			[p30] "v" (state[3].x), \
			[p31] "v" (state[3].y));

#define push_state(state) \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p00], %[p00] quad_perm:[3,3,3,3]\n" \
	      "v_mov_b32_dpp  %[p01], %[p01] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] quad_perm:[3,3,3,3]\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] quad_perm:[3,3,3,3]\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] quad_perm:[3,3,3,3]\n" \
		  "s_nop 0" \
		  : [p00] "=&v" (state[0].x), \
		    [p01] "=&v" (state[0].y), \
			[p10] "=&v" (state[1].x), \
		    [p11] "=&v" (state[1].y), \
			[p20] "=&v" (state[2].x), \
			[p21] "=&v" (state[2].y), \
			[p30] "=&v" (state[3].x), \
			[p31] "=&v" (state[3].y) \
		  : [p00] "v" (state[0].x), \
		    [p00] "v" (state[0].x), \
		    [p10] "v" (state[1].x), \
		    [p11] "v" (state[1].y), \
			[p20] "v" (state[2].x), \
			[p21] "v" (state[2].y), \
			[p30] "v" (state[3].x), \
			[p31] "v" (state[3].y));

#define shflldpp(state) \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:12\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:12\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:4\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (state[1].x), \
		    [p11] "=&v" (state[1].y), \
			[p20] "=&v" (state[2].x), \
			[p21] "=&v" (state[2].y), \
			[p30] "=&v" (state[3].x), \
			[p31] "=&v" (state[3].y) \
		  : [p10] "v" (state[1].x), \
		    [p11] "v" (state[1].y), \
			[p20] "v" (state[2].x), \
			[p21] "v" (state[2].y), \
			[p30] "v" (state[3].x), \
			[p31] "v" (state[3].y));

#define shflrdpp(state)  \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:4\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:12\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:12\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (state[1].x), \
		    [p11] "=&v" (state[1].y), \
			[p20] "=&v" (state[2].x), \
			[p21] "=&v" (state[2].y), \
			[p30] "=&v" (state[3].x), \
			[p31] "=&v" (state[3].y) \
		  : [p10] "v" (state[1].x), \
		    [p11] "v" (state[1].y), \
			[p20] "v" (state[2].x), \
			[p21] "v" (state[2].y), \
			[p30] "v" (state[3].x), \
			[p31] "v" (state[3].y)); 

// pad counts 4 entries each hash team of 4
#define round_lyra_4way_sw(state)   \
    pull_state(state); \
	cipher_G_macro(state); \
	push_state(state); \
	shflldpp(state); \
	cipher_G_macro(state);\
	push_state(state); \
	shflrdpp(state);

#define xorrot_one_dpp(modify, src) \
	uint2 p0 = (src[0]); \
	uint2 p1 = (src[1]); \
	uint2 p2 = (src[2]); \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:4\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:4\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (p0.x), \
		    [p11] "=&v" (p0.y), \
			[p20] "=&v" (p1.x), \
			[p21] "=&v" (p1.y), \
			[p30] "=&v" (p2.x), \
			[p31] "=&v" (p2.y) \
		  : [p10] "v" (p0.x), \
		    [p11] "v" (p0.y), \
			[p20] "v" (p1.x), \
			[p21] "v" (p1.y), \
			[p30] "v" (p2.x), \
			[p31] "v" (p2.y)); \
	if (get_local_id(0) != 0) { \
		modify[0] ^= (p0); \
		modify[1] ^= (p1); \
		modify[2] ^= (p2); \
	} else { \
		modify[0] ^= (p2); \
		modify[1] ^= (p0); \
		modify[2] ^= (p1); \
    }


#define make_hyper_one_macro(state, bigMat) do { \
	uint src = 0; \
	uint dst = HYPERMATRIX_COUNT * 2 - STATE_BLOCK_COUNT; \
	for (int loop = 0; loop < LYRA_ROUNDS; loop++) \
	{ \
		int row = LOCAL_LINEAR; \
		si[row] = bigMat[src]; state[row] ^= si[row]; \
		round_lyra_4way_sw(state); \
		si[row] = si[row] ^= state[row]; bigMat[dst] = si[row]; \
		dst -= STATE_BLOCK_COUNT; \
	} \
} while (0);

#define make_next_hyper_macro(matin, matrw, matout, state, bigMat) do { \
	uint hyc = HYPERMATRIX_COUNT * matin;  \
	uint hymod = HYPERMATRIX_COUNT * matrw;  \
	uint hydst = HYPERMATRIX_COUNT * matout + HYPERMATRIX_COUNT - STATE_BLOCK_COUNT; \
	for (int i = 0; i < LYRA_ROUNDS; i++) \
	{ \
	    int row = LOCAL_LINEAR; \
		si[row] = bigMat[hyc]; \
			sII[row] = bigMat[hymod]; \
			state[row] ^= si[row] + sII[row]; \
		round_lyra_4way_sw(state); \
		si[row] ^= state[row]; \
			bigMat[hydst] = si[row]; \
		xorrot_one_dpp(sII, state); \
		bigMat[hymod] = sII[row]; \
		hyc += STATE_BLOCK_COUNT; \
		hymod += STATE_BLOCK_COUNT; \
		hydst -= STATE_BLOCK_COUNT; \
	} \
} while (0);

uint broadcast_zero(uint s) {
	uint p1 = s;
	uint p2 = s;
	uint p3 = s;
	__asm (
		  "s_nop 0\n"
		  "v_mov_b32_dpp  %[p1], %[p1] row_ror:4\n"
		  "v_mov_b32_dpp  %[p2], %[p2] row_ror:8\n"
		  "v_mov_b32_dpp  %[p3], %[p3] row_ror:12\n"
		  "s_nop 0\n"
		  : [p1] "=&v" (p1),
		    [p2] "=&v" (p2),
			[p3] "=&v" (p3)
		  : [p1] "0" (p1),
		    [p2] "1" (p2),
			[p3] "2" (p3));
	int row = LOCAL_LINEAR;
	if (row == 0) {
		return s;
	} else if (row == 1) {
		return p1;
	} else if (row == 2) {
		return p2;
	} else { // row == 3
		return p3;
	}
}

#define hyper_xor_dpp_macro_backup( matin, matrw, matout, state, bigMat) do { \
	uint3 hyoff = (uint3)(matin* HYPERMATRIX_COUNT, matrw* HYPERMATRIX_COUNT, matout* HYPERMATRIX_COUNT); \
	uint hyc = HYPERMATRIX_COUNT * matin; \
	uint hymod = HYPERMATRIX_COUNT * matrw; \
	uint hydst = HYPERMATRIX_COUNT * matout; \
	for (int i = 0; i < LYRA_ROUNDS; i++) \
	{ \
	    int row = LOCAL_LINEAR; \
		si[row] = bigMat[hyc]; \
			sII[row] = bigMat[hymod]; \
		si[row] += sII[row]; \
			state[row] ^= si[row]; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		bigMat[hymod] = sII[row]; \
				bigMat[hydst] ^= state[row]; \
		hyc += STATE_BLOCK_COUNT; \
		hymod += STATE_BLOCK_COUNT; \
		hydst += STATE_BLOCK_COUNT; \
	} \
} while (0);

#define hyper_xor_dpp_macro( matin, matrw, matout, state, bigMat) do { \
	uint3 hyoff = (uint3)(matin* HYPERMATRIX_COUNT, matrw* HYPERMATRIX_COUNT, matout* HYPERMATRIX_COUNT); \
	uint hyc = HYPERMATRIX_COUNT * matin; \
	uint hymod = HYPERMATRIX_COUNT * matrw; \
	uint hydst = HYPERMATRIX_COUNT * matout; \
	for (int i = 0; i < LYRA_ROUNDS; i++) \
	{ \
	    for (int row = 0; row < 3; row++)  { \
			si[row] = bigMat[hyc + row * REG_ROW_COUNT]; \
			sII[row] = bigMat[hymod + row * REG_ROW_COUNT]; \
		} \
		for (int row = 0; row < 3; row++)  { \
			si[row] += sII[row]; \
			state[row] ^= si[row]; \
		} \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		for (int row = 0; row < 3; row++) { \
				bigMat[hymod + row * REG_ROW_COUNT] = sII[row]; \
				bigMat[hydst + row * REG_ROW_COUNT] ^= state[row]; \
			} \
		hyc += STATE_BLOCK_COUNT; \
		hymod += STATE_BLOCK_COUNT; \
		hydst += STATE_BLOCK_COUNT; \
	} \
} while (0);
