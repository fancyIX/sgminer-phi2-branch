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

#define ROTR64(x2, y) as_ulong(y < 32 ? (y % 8 == 0 ? (((amd_bytealign(x2.s10, x2, y / 8)))) : (((amd_bitalign(x2.s10, x2, y))))) : (((amd_bitalign(x2, x2.s10, (y - 32))))))


/// lyra2 algo  ///////////////////////////////////////////////////////////
#define HASH_SIZE (256 / 8) // size in bytes of an hash in/out
#define SLOT (get_global_id(1) - get_global_offset(1))
#define LOCAL_LINEAR (get_local_id(0) & 3)
#define REG_ROW_COUNT (1) // ideally all happen at the same clock
#define STATE_BLOCK_COUNT (1 * REG_ROW_COUNT)  // very close instructions
#define LYRA_ROUNDS 8
#define HYPERMATRIX_COUNT (LYRA_ROUNDS * STATE_BLOCK_COUNT)

// Usually just #define G(a,b,c,d)...; I have no time to read the Lyra paper
// but that looks like some kind of block cipher I guess.
#define cipher_G_macro(s) \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = SWAP32(s[3]); \
	s[2] += s[3]; s[1] ^= s[2]; s1 = as_uint2(s[1]); s[1] = ROTR64(s1, 24); \
	s[0] += s[1]; s[3] ^= s[0]; s3 = as_uint2(s[3]); s[3] = ROTR64(s3, 16); \
	s[2] += s[3]; s[1] ^= s[2]; s1 = as_uint2(s[1]); s[1] = ROTR64(s1, 63);

#define pull_state(state) \
    s0 = as_uint2(cstate); \
	s1 = as_uint2(cstate); \
	s2 = as_uint2(cstate); \
	s3 = as_uint2(cstate); \
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
		  : [p00] "=&v" (s0.x), \
		    [p01] "=&v" (s0.y), \
			[p10] "=&v" (s1.x), \
		    [p11] "=&v" (s1.y), \
			[p20] "=&v" (s2.x), \
			[p21] "=&v" (s2.y), \
			[p30] "=&v" (s3.x), \
			[p31] "=&v" (s3.y) \
		  : [p00] "0" (s0.x), \
		    [p00] "1" (s0.y), \
		    [p10] "2" (s1.x), \
		    [p11] "3" (s1.y), \
			[p20] "4" (s2.x), \
			[p21] "5" (s2.y), \
			[p30] "6" (s3.x), \
			[p31] "7" (s3.y)); \
	state[0] = as_ulong(s0); \
	state[1] = as_ulong(s1); \
	state[2] = as_ulong(s2); \
	state[3] = as_ulong(s3);

#define shflldpp(state) \
	s1 = as_uint2(state[1]); \
	s2 = as_uint2(state[2]); \
	s3 = as_uint2(state[3]); \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:12\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:12\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:4\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (s1.x), \
		    [p11] "=&v" (s1.y), \
			[p20] "=&v" (s2.x), \
			[p21] "=&v" (s2.y), \
			[p30] "=&v" (s3.x), \
			[p31] "=&v" (s3.y) \
		  : [p10] "0" (s1.x), \
		    [p11] "1" (s1.y), \
			[p20] "2" (s2.x), \
			[p21] "3" (s2.y), \
			[p30] "4" (s3.x), \
			[p31] "5" (s3.y)); \
	state[1] = as_ulong(s1); \
	state[2] = as_ulong(s2); \
	state[3] = as_ulong(s3);

#define shflrdpp(state)  \
	s1 = as_uint2(state[1]); \
	s2 = as_uint2(state[2]); \
	s3 = as_uint2(state[3]); \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:4\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:12\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:12\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (s1.x), \
		    [p11] "=&v" (s1.y), \
			[p20] "=&v" (s2.x), \
			[p21] "=&v" (s2.y), \
			[p30] "=&v" (s3.x), \
			[p31] "=&v" (s3.y) \
		  : [p10] "0" (s1.x), \
		    [p11] "1" (s1.y), \
			[p20] "2" (s2.x), \
			[p21] "3" (s2.y), \
			[p30] "4" (s3.x), \
			[p31] "5" (s3.y)); \
	state[1] = as_ulong(s1); \
	state[2] = as_ulong(s2); \
	state[3] = as_ulong(s3);

// pad counts 4 entries each hash team of 4
#define round_lyra_4way_sw(state)   \
    pull_state(state); \
	cipher_G_macro(state); \
	shflldpp(state); \
	cipher_G_macro(state);\
	shflrdpp(state); \
	if (LOCAL_LINEAR == 0) cstate = state[0]; \
	if (LOCAL_LINEAR == 1) cstate = state[1]; \
	if (LOCAL_LINEAR == 2) cstate = state[2]; \
	if (LOCAL_LINEAR == 3) cstate = state[3];

#define xorrot_one_dpp(sII, state) \
	s0 = as_uint2(state[0]); \
	s1 = as_uint2(state[1]); \
	s2 = as_uint2(state[2]); \
	__asm ( \
	      "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] row_ror:4\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] row_ror:4\n" \
		  "s_nop 0" \
		  : [p10] "=&v" (s0.x), \
		    [p11] "=&v" (s0.y), \
			[p20] "=&v" (s1.x), \
			[p21] "=&v" (s1.y), \
			[p30] "=&v" (s2.x), \
			[p31] "=&v" (s2.y) \
		  : [p10] "0" (s0.x), \
		    [p11] "1" (s0.y), \
			[p20] "2" (s1.x), \
			[p21] "3" (s1.y), \
			[p30] "4" (s2.x), \
			[p31] "5" (s2.y)); \
	if ((get_local_id(1) & 3) == 1 || (get_local_id(1) & 3) == 2 || (get_local_id(1) & 3) == 3) if (LOCAL_LINEAR == 0) sII ^= as_ulong(s0); \
	if ((get_local_id(1) & 3) == 1 || (get_local_id(1) & 3) == 2 || (get_local_id(1) & 3) == 3) if (LOCAL_LINEAR == 1) sII ^= as_ulong(s1); \
	if ((get_local_id(1) & 3) == 1 || (get_local_id(1) & 3) == 2 || (get_local_id(1) & 3) == 3) if (LOCAL_LINEAR == 2) sII ^= as_ulong(s2); \
	if ((get_local_id(1) & 3) == 0 ) if (LOCAL_LINEAR == 0) sII ^= as_ulong(s2); \
	if ((get_local_id(1) & 3) == 0 ) if (LOCAL_LINEAR == 1) sII ^= as_ulong(s0); \
	if ((get_local_id(1) & 3) == 0 ) if (LOCAL_LINEAR == 2) sII ^= as_ulong(s1); \

#define make_hyper_one_macro(state, bigMat) do { \
    { \
		si = bigMat[0]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[15] = si; \
		si = bigMat[1]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[14] = si; \
		si = bigMat[2]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[13] = si; \
		si = bigMat[3]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[12] = si; \
		si = bigMat[4]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[11] = si; \
		si = bigMat[5]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[10] = si; \
		si = bigMat[6]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[9] = si; \
		si = bigMat[7]; if (LOCAL_LINEAR != 3) cstate ^= si; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8] = si; \
	} \
} while (0);

#define make_next_hyper_macro(matin, matrw, matout, state, bigMat) do { \
	{ \
	    si = bigMat[8 * matin]; sII = bigMat[8 * matrw]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 7] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw] = sII; \
		si = bigMat[8 * matin + 1]; sII = bigMat[8 * matrw + 1]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 6] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 1] = sII; \
		si = bigMat[8 * matin + 2]; sII = bigMat[8 * matrw + 2]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 5] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 2] = sII; \
		si = bigMat[8 * matin + 3]; sII = bigMat[8 * matrw + 3]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 4] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 3] = sII; \
		si = bigMat[8 * matin + 4]; sII = bigMat[8 * matrw + 4]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 3] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 4] = sII; \
		si = bigMat[8 * matin + 5]; sII = bigMat[8 * matrw + 5]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 2] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 5] = sII; \
		si = bigMat[8 * matin + 6]; sII = bigMat[8 * matrw + 6]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout + 1] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 6] = sII; \
		si = bigMat[8 * matin + 7]; sII = bigMat[8 * matrw + 7]; if (LOCAL_LINEAR != 3) cstate ^= si + sII; \
		round_lyra_4way_sw(state); \
		if (LOCAL_LINEAR != 3) si ^= cstate; bigMat[8 * matout] = si; \
		xorrot_one_dpp(sII, state); \
        bigMat[8 * matrw + 7] = sII; \
	} \
} while (0);

#define broadcast_zero(s) \
    p0 = (s[0] & 7); \
	p1 = (s[0] & 7); \
	p2 = (s[0] & 7); \
	p3 = (s[0] & 7); \
	__asm ( \
		  "s_nop 0\n" \
		  "v_mov_b32_dpp  %[p1], %[p1] row_ror:4\n" \
		  "v_mov_b32_dpp  %[p2], %[p2] row_ror:8\n" \
		  "v_mov_b32_dpp  %[p3], %[p3] row_ror:12\n" \
		  "s_nop 0\n" \
		  : [p1] "=&v" (p1), \
		    [p2] "=&v" (p2), \
			[p3] "=&v" (p3) \
		  : [p1] "0" (p1), \
		    [p2] "1" (p2), \
			[p3] "2" (p3)); \
	if ((get_local_id(1) & 3) == 1) modify = p1; \
	if ((get_local_id(1) & 3) == 2) modify = p2; \
	if ((get_local_id(1) & 3) == 3) modify = p3; \
	if ((get_local_id(1) & 3) == 0) modify = p0;

#define real_matrw_read(sII, bigMat, matrw, off) \
		if (matrw == 0) sII = bigMat[8 * 0 + off];  \
		if (matrw == 1) sII = bigMat[8 * 1 + off]; \
		if (matrw == 2) sII = bigMat[8 * 2 + off]; \
		if (matrw == 3) sII = bigMat[8 * 3 + off]; \
		if (matrw == 4) sII = bigMat[8 * 4 + off]; \
		if (matrw == 5) sII = bigMat[8 * 5 + off]; \
		if (matrw == 6) sII = bigMat[8 * 6 + off]; \
		if (matrw == 7) sII = bigMat[8 * 7 + off];

#define real_matrw_write(sII, bigMat, matrw, off) \
		if (matrw == 0) bigMat[8 * 0 + off] = sII; \
		if (matrw == 1) bigMat[8 * 1 + off] = sII; \
		if (matrw == 2) bigMat[8 * 2 + off] = sII; \
		if (matrw == 3) bigMat[8 * 3 + off] = sII; \
		if (matrw == 4) bigMat[8 * 4 + off] = sII; \
		if (matrw == 5) bigMat[8 * 5 + off] = sII; \
		if (matrw == 6) bigMat[8 * 6 + off] = sII; \
		if (matrw == 7) bigMat[8 * 7 + off] = sII;

#define hyper_xor_dpp_macro( matin, matrw, matout, state, bigMat) do { \
    { \
		si = bigMat[8 * matin + 0]; real_matrw_read(sII, bigMat, matrw, 0); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 0); bigMat[8 * matout + 0] ^= cstate; \
		si = bigMat[8 * matin + 1]; real_matrw_read(sII, bigMat, matrw, 1); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 1); bigMat[8 * matout + 1] ^= cstate; \
		si = bigMat[8 * matin + 2]; real_matrw_read(sII, bigMat, matrw, 2); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 2); bigMat[8 * matout + 2] ^= cstate; \
		si = bigMat[8 * matin + 3]; real_matrw_read(sII, bigMat, matrw, 3); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 3); bigMat[8 * matout + 3] ^= cstate; \
		si = bigMat[8 * matin + 4]; real_matrw_read(sII, bigMat, matrw, 4); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 4); bigMat[8 * matout + 4] ^= cstate; \
		si = bigMat[8 * matin + 5]; real_matrw_read(sII, bigMat, matrw, 5); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 5); bigMat[8 * matout + 5] ^= cstate; \
		si = bigMat[8 * matin + 6]; real_matrw_read(sII, bigMat, matrw, 6); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 6); bigMat[8 * matout + 6] ^= cstate; \
		si = bigMat[8 * matin + 7]; real_matrw_read(sII, bigMat, matrw, 7); if (LOCAL_LINEAR != 3) cstate ^= sII + si; \
		round_lyra_4way_sw(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, bigMat, matrw, 7); bigMat[8 * matout + 7] ^= cstate; \
	} \
} while (0);
