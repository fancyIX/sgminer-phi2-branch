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
#define ROTR64_24(x2) as_ulong(amd_bytealign(x2.s10, x2, 3))
#define ROTR64_16(x2) as_ulong(amd_bytealign(x2.s10, x2, 2))
#define ROTR64_63(x2) as_ulong(amd_bitalign(x2, x2.s10, 31))


/// lyra2 algo  ///////////////////////////////////////////////////////////
#define HASH_SIZE (256 / 8) // size in bytes of an hash in/out
#define SLOT (get_global_id(1) - get_global_offset(1))
#define LOCAL_LINEAR (get_local_id(1) * 4 + get_local_id(0))
#define REG_ROW_COUNT (4 * 8) // ideally all happen at the same clock
#define STATE_BLOCK_COUNT (1 * REG_ROW_COUNT)  // very close instructions
#define LYRA_ROUNDS 8
#define HYPERMATRIX_COUNT (LYRA_ROUNDS * STATE_BLOCK_COUNT)

#define cipher_G_macro(s) \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = SWAP32(s[3]); \
	s[2] += s[3]; s[1] ^= s[2]; ss1 = as_uint2(s[1]); s[1] = ROTR64_24(ss1); \
	s[0] += s[1]; s[3] ^= s[0]; ss3 = as_uint2(s[3]); s[3] = ROTR64_16(ss3); \
	s[2] += s[3]; s[1] ^= s[2]; ss1 = as_uint2(s[1]); s[1] = ROTR64_63(ss1);

#define shflldpp(state) \
	s1 = as_uint2(state[1]); \
	s2 = as_uint2(state[2]); \
	s3 = as_uint2(state[3]); \
	__asm ( \
	      "s_nop 1\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] quad_perm:[1,2,3,0]\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] quad_perm:[1,2,3,0]\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] quad_perm:[2,3,0,1]\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] quad_perm:[2,3,0,1]\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] quad_perm:[3,0,1,2]\n" \
		  "s_nop 1" \
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

#define shflrdpp(state) \
	s1 = as_uint2(state[1]); \
	s2 = as_uint2(state[2]); \
	s3 = as_uint2(state[3]); \
	__asm ( \
	      "s_nop 1\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] quad_perm:[3,0,1,2]\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] quad_perm:[2,3,0,1]\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] quad_perm:[2,3,0,1]\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] quad_perm:[1,2,3,0]\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] quad_perm:[1,2,3,0]\n" \
		  "s_nop 1" \
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

#define round_lyra_4way_dpp(state)   \
	cipher_G_macro(state); \
	shflldpp(state); \
	cipher_G_macro(state);\
	shflrdpp(state);


#define write_state(notepad, state, row, col) \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].x = state[0]; \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].y = state[1]; \
  halfpad[8 * row + 1 * col] = state[2];

#define state_xor_modify(modify, row, col, state, notepad) \
  if (modify == row) { \
    state[0] ^= notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].x; \
    state[1] ^= notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].y; } \
  if (modify == row) state[2] ^= halfpad[8 * row + 1 * col];

#define state_xor(state, notepad, row, col) \
  si[0] = notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].x; \
  si[1] = notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].y; \
  si[2] = halfpad[8 * row + 1 * col]; \
  state[0] ^= si[0]; state[1] ^= si[1]; state[2] ^= si[2];

#define xor_state(state, notepad, row, col) \
  si[0] ^= state[0]; si[1] ^= state[1]; si[2] ^= state[2]; \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].x = si[0]; \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].y = si[1]; \
  halfpad[8 * row + 1 * col] = si[2];

#define state_xor_plus(state, notepad, matin, colin, matrw, colrw) \
   si[0] = notepad[HYPERMATRIX_COUNT * matin + STATE_BLOCK_COUNT * colin + 0 * REG_ROW_COUNT].x; \
   si[1] = notepad[HYPERMATRIX_COUNT * matin + STATE_BLOCK_COUNT * colin + 0 * REG_ROW_COUNT].y; \
   si[2] = halfpad[8 * matin + 1 * colin]; \
   sII[0] = notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * colrw + 0 * REG_ROW_COUNT].x; \
   sII[1] = notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * colrw + 0 * REG_ROW_COUNT].y; \
   sII[2] = halfpad[8 * matrw + 1 * colrw]; \
   state[0] ^= si[0] + sII[0]; state[1] ^= si[1] + sII[1]; state[2] ^= si[2] + sII[2];


#define make_hyper_one_macro(state, notepad) do { \
    { \
		state_xor(state, notepad, 0, 0); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 7); \
		state_xor(state, notepad, 0, 1); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 6); \
		state_xor(state, notepad, 0, 2); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 5); \
		state_xor(state, notepad, 0, 3); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 4); \
		state_xor(state, notepad, 0, 4); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 3); \
		state_xor(state, notepad, 0, 5); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 2); \
		state_xor(state, notepad, 0, 6); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 1); \
		state_xor(state, notepad, 0, 7); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, 1, 0); \
	} \
} while (0);

#define make_next_hyper_macro(matin, matrw, matout, state, notepad) do { \
	{ \
		state_xor_plus(state, notepad, matin, 0, matrw, 0); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 7); \
		xorrot_one_dpp(sII, state); \
		write_state(notepad, sII, matrw, 0); \
		state_xor_plus(state, notepad, matin, 1, matrw, 1); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 6); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 1); \
		state_xor_plus(state, notepad, matin, 2, matrw, 2); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 5); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 2); \
		state_xor_plus(state, notepad, matin, 3, matrw, 3); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 4); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 3); \
		state_xor_plus(state, notepad, matin, 4, matrw, 4); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 3); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 4); \
		state_xor_plus(state, notepad, matin, 5, matrw, 5); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 2); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 5); \
		state_xor_plus(state, notepad, matin, 6, matrw, 6); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 1); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 6); \
		state_xor_plus(state, notepad, matin, 7, matrw, 7); \
		round_lyra_4way_dpp(state); \
		xor_state(state, notepad, matout, 0); \
		xorrot_one_dpp(sII, state); \
        write_state(notepad, sII, matrw, 7); \
	} \
} while (0);

#define xorrot_one_dpp(dst, src) \
	s0 = as_uint2(src[0]); \
	s1 = as_uint2(src[1]); \
	s2 = as_uint2(src[2]); \
	__asm ( \
	      "s_nop 1\n" \
		  "v_mov_b32_dpp  %[p10], %[p10] quad_perm:[3,0,1,2]\n" \
	      "v_mov_b32_dpp  %[p11], %[p11] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p20], %[p20] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p21], %[p21] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p30], %[p30] quad_perm:[3,0,1,2]\n" \
		  "v_mov_b32_dpp  %[p31], %[p31] quad_perm:[3,0,1,2]\n" \
		  "s_nop 1" \
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
	if ((get_local_id(0) & 3) == 1) dst[0] ^= as_ulong(s0); \
	if ((get_local_id(0) & 3) == 1) dst[1] ^= as_ulong(s1); \
	if ((get_local_id(0) & 3) == 1) dst[2] ^= as_ulong(s2); \
	if ((get_local_id(0) & 3) == 2) dst[0] ^= as_ulong(s0); \
	if ((get_local_id(0) & 3) == 2) dst[1] ^= as_ulong(s1); \
	if ((get_local_id(0) & 3) == 2) dst[2] ^= as_ulong(s2); \
	if ((get_local_id(0) & 3) == 3) dst[0] ^= as_ulong(s0); \
	if ((get_local_id(0) & 3) == 3) dst[1] ^= as_ulong(s1); \
	if ((get_local_id(0) & 3) == 3) dst[2] ^= as_ulong(s2); \
	if ((get_local_id(0) & 3) == 0) dst[0] ^= as_ulong(s2); \
	if ((get_local_id(0) & 3) == 0) dst[1] ^= as_ulong(s0); \
	if ((get_local_id(0) & 3) == 0) dst[2] ^= as_ulong(s1);

#define broadcast_zero(s) \
	p0 = (s[0] & 7); \
	__asm ( \
		  "ds_swizzle_b32  %[p], %[p] offset:0x8000\n" \
		  "s_waitcnt lgkmcnt(0)" \
		  : [p] "=&v" (p0) \
		  : [p] "0" (p0)); \
	modify = p0;

#define real_matrw_read(sII, notepad, matrw, off) \
		sII[0] = notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * off + 0 * REG_ROW_COUNT].x; \
		sII[1] = notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * off + 0 * REG_ROW_COUNT].y; \
		if (matrw == 0) sII[2] = halfpad[8 * 0 + 1 * off]; \
		if (matrw == 1) sII[2] = halfpad[8 * 1 + 1 * off]; \
		if (matrw == 2) sII[2] = halfpad[8 * 2 + 1 * off]; \
		if (matrw == 3) sII[2] = halfpad[8 * 3 + 1 * off]; \
		if (matrw == 4) sII[2] = halfpad[8 * 4 + 1 * off]; \
		if (matrw == 5) sII[2] = halfpad[8 * 5 + 1 * off]; \
		if (matrw == 6) sII[2] = halfpad[8 * 6 + 1 * off]; \
		if (matrw == 7) sII[2] = halfpad[8 * 7 + 1 * off];

#define real_matrw_write(sII, notepad, matrw, off) \
		notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * off + 0 * REG_ROW_COUNT].x = sII[0]; \
		notepad[HYPERMATRIX_COUNT * matrw + STATE_BLOCK_COUNT * off + 0 * REG_ROW_COUNT].y = sII[1]; \
		if (matrw == 0) halfpad[8 * 0 + 1 * off] = sII[2]; \
		if (matrw == 1) halfpad[8 * 1 + 1 * off] = sII[2]; \
		if (matrw == 2) halfpad[8 * 2 + 1 * off] = sII[2]; \
		if (matrw == 3) halfpad[8 * 3 + 1 * off] = sII[2]; \
		if (matrw == 4) halfpad[8 * 4 + 1 * off] = sII[2]; \
		if (matrw == 5) halfpad[8 * 5 + 1 * off] = sII[2]; \
		if (matrw == 6) halfpad[8 * 6 + 1 * off] = sII[2]; \
		if (matrw == 7) halfpad[8 * 7 + 1 * off] = sII[2];

#define state_xor_plus_modify(state, notepad, matin, colin, matrw, colrw) \
   si[0] = notepad[HYPERMATRIX_COUNT * matin + STATE_BLOCK_COUNT * colin + 0 * REG_ROW_COUNT].x; \
   si[1] = notepad[HYPERMATRIX_COUNT * matin + STATE_BLOCK_COUNT * colin + 0 * REG_ROW_COUNT].y; \
   si[2] = halfpad[8 * matin + 1 * colin]; \
   real_matrw_read(sII, notepad, matrw, colrw); \
   state[0] ^= si[0] + sII[0]; \
   state[1] ^= si[1] + sII[1]; \
   state[2] ^= si[2] + sII[2];

#define xor_state_modify(state, notepad, row, col) \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].x ^= state[0]; \
  notepad[HYPERMATRIX_COUNT * row + STATE_BLOCK_COUNT * col + 0 * REG_ROW_COUNT].y ^= state[1]; \
  halfpad[8 * row + 1 * col] ^= state[2];

#define hyper_xor_dpp_macro( matin, matrw, matout, state, notepad) do { \
    { \
		state_xor_plus_modify(state, notepad, matin, 0, matrw, 0); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 0); xor_state_modify(state, notepad, matout, 0); \
		state_xor_plus_modify(state, notepad, matin, 1, matrw, 1); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 1); xor_state_modify(state, notepad, matout, 1); \
		state_xor_plus_modify(state, notepad, matin, 2, matrw, 2); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 2); xor_state_modify(state, notepad, matout, 2); \
		state_xor_plus_modify(state, notepad, matin, 3, matrw, 3); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 3); xor_state_modify(state, notepad, matout, 3); \
		state_xor_plus_modify(state, notepad, matin, 4, matrw, 4); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 4); xor_state_modify(state, notepad, matout, 4); \
		state_xor_plus_modify(state, notepad, matin, 5, matrw, 5); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 5); xor_state_modify(state, notepad, matout, 5); \
		state_xor_plus_modify(state, notepad, matin, 6, matrw, 6); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 6); xor_state_modify(state, notepad, matout, 6); \
		state_xor_plus_modify(state, notepad, matin, 7, matrw, 7); \
		round_lyra_4way_dpp(state); \
		xorrot_one_dpp(sII, state); \
		real_matrw_write(sII, notepad, matrw, 7); xor_state_modify(state, notepad, matout, 7); \
	} \
} while (0);
