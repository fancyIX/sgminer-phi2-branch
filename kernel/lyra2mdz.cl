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

#define rotr64(x, n) ((n) < 32 ? (amd_bitalign((uint)((x) >> 32), (uint)(x), (uint)(n)) | ((ulong)amd_bitalign((uint)(x), (uint)((x) >> 32), (uint)(n)) << 32)) : (amd_bitalign((uint)(x), (uint)((x) >> 32), (uint)(n) - 32) | ((ulong)amd_bitalign((uint)((x) >> 32), (uint)(x), (uint)(n) - 32) << 32)))

#define Gfunc(a,b,c,d) \
{ \
    a += b;  \
    d ^= a; \
    ttr = rotr64(d, 32); \
    d = ttr; \
 \
    c += d;  \
    b ^= c; \
    ttr = rotr64(b, 24); \
    b = ttr; \
 \
    a += b;  \
    d ^= a; \
    ttr = rotr64(d, 16); \
    d = ttr; \
 \
    c += d; \
    b ^= c; \
    ttr = rotr64(b, 63); \
    b = ttr; \
}

#define roundLyra(state) \
{ \
     Gfunc(state[0].x, state[2].x, state[4].x, state[6].x); \
     Gfunc(state[0].y, state[2].y, state[4].y, state[6].y); \
     Gfunc(state[1].x, state[3].x, state[5].x, state[7].x); \
     Gfunc(state[1].y, state[3].y, state[5].y, state[7].y); \
 \
     Gfunc(state[0].x, state[2].y, state[5].x, state[7].y); \
     Gfunc(state[0].y, state[3].x, state[5].y, state[6].x); \
     Gfunc(state[1].x, state[3].y, state[4].x, state[6].y); \
     Gfunc(state[1].y, state[2].x, state[4].y, state[7].x); \
}


#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable

ulong ROTR64(const ulong x2, const uint y)
{
	uint2 x = as_uint2(x2);
	if(y < 32) return(as_ulong(amd_bitalign(x.s10, x, y)));
	else return(as_ulong(amd_bitalign(x, x.s10, (y - 32))));
}


/// lyra2 algo  ///////////////////////////////////////////////////////////
#define HASH_SIZE (256 / 8) // size in bytes of an hash in/out
#define SLOT (get_global_id(1) - get_global_offset(1))
#define LOCAL_LINEAR (get_local_id(1) * get_local_size(0) + get_local_id(0))
#define REG_ROW_COUNT (4 * get_local_size(1)) // ideally all happen at the same clock
#define STATE_BLOCK_COUNT (3 * REG_ROW_COUNT)  // very close instructions
#define LYRA_ROUNDS 8
#define HYPERMATRIX_COUNT (LYRA_ROUNDS * STATE_BLOCK_COUNT)

// Usually just #define G(a,b,c,d)...; I have no time to read the Lyra paper
// but that looks like some kind of block cipher I guess.



#define cipher_G_m(s) \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = SWAP32(s[3]); \
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 24); \
	s[0] += s[1]; s[3] ^= s[0]; s[3] = ROTR64(s[3], 16); \
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 63);

void cipher_G(ulong *s) {
	s[0] += s[1]; s[3] ^= s[0]; s[3] = SWAP32(s[3]);
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 24);
	s[0] += s[1]; s[3] ^= s[0]; s[3] = ROTR64(s[3], 16);
	s[2] += s[3]; s[1] ^= s[2]; s[1] = ROTR64(s[1], 63);
}

void shflldpp(ulong *state) {
	uint2 s1 = as_uint2(state[1]);
	uint2 s2 = as_uint2(state[2]);
	uint2 s3 = as_uint2(state[3]);
	__asm (
	      "s_nop 0\n"
		  "v_mov_b32_dpp  %[dp10], %[p10] quad_perm:[1,2,3,0]\n"
	      "v_mov_b32_dpp  %[dp11], %[p11] quad_perm:[1,2,3,0]\n"
		  "v_mov_b32_dpp  %[dp20], %[p20] quad_perm:[2,3,0,1]\n"
		  "v_mov_b32_dpp  %[dp21], %[p21] quad_perm:[2,3,0,1]\n"
		  "v_mov_b32_dpp  %[dp30], %[p30] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp31], %[p31] quad_perm:[3,0,1,2]\n"
		  "s_nop 0"
		  : [dp10] "=&v" (s1.x),
		    [dp11] "=&v" (s1.y),
			[dp20] "=&v" (s2.x),
			[dp21] "=&v" (s2.y),
			[dp30] "=&v" (s3.x),
			[dp31] "=&v" (s3.y)
		  : [p10] "0" (s1.x),
		    [p11] "1" (s1.y),
			[p20] "2" (s2.x),
			[p21] "3" (s2.y),
			[p30] "4" (s3.x),
			[p31] "5" (s3.y));
	state[1] = as_ulong(s1);
	state[2] = as_ulong(s2);
	state[3] = as_ulong(s3);
}

void shflrdpp(ulong *state) {
	uint2 s1 = as_uint2(state[1]);
	uint2 s2 = as_uint2(state[2]);
	uint2 s3 = as_uint2(state[3]);
	__asm (
	      "s_nop 0\n"
		  "v_mov_b32_dpp  %[dp10], %[p10] quad_perm:[3,0,1,2]\n"
	      "v_mov_b32_dpp  %[dp11], %[p11] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp20], %[p20] quad_perm:[2,3,0,1]\n"
		  "v_mov_b32_dpp  %[dp21], %[p21] quad_perm:[2,3,0,1]\n"
		  "v_mov_b32_dpp  %[dp30], %[p30] quad_perm:[1,2,3,0]\n"
		  "v_mov_b32_dpp  %[dp31], %[p31] quad_perm:[1,2,3,0]\n"
		  "s_nop 0"
		  : [dp10] "=&v" (s1.x),
		    [dp11] "=&v" (s1.y),
			[dp20] "=&v" (s2.x),
			[dp21] "=&v" (s2.y),
			[dp30] "=&v" (s3.x),
			[dp31] "=&v" (s3.y)
		  : [p10] "0" (s1.x),
		    [p11] "1" (s1.y),
			[p20] "2" (s2.x),
			[p21] "3" (s2.y),
			[p30] "4" (s3.x),
			[p31] "5" (s3.y));
	state[1] = as_ulong(s1);
	state[2] = as_ulong(s2);
	state[3] = as_ulong(s3);
}


// pad counts 4 entries each hash team of 4
#define round_lyra_4way_sw(state, pad) do {  \
	cipher_G(state);  \
	shflldpp(state); \
	cipher_G(state); \
	shflrdpp(state); \
} while (0);

void xorrot_one_dpp(ulong *modify, __local ulong *groupPad, ulong *src) {
	uint2 p0 = as_uint2(src[0]);
	uint2 p1 = as_uint2(src[1]);
	uint2 p2 = as_uint2(src[2]);
	__asm (
	      "s_nop 0\n"
		  "v_mov_b32_dpp  %[dp10], %[p10] quad_perm:[3,0,1,2]\n"
	      "v_mov_b32_dpp  %[dp11], %[p11] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp20], %[p20] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp21], %[p21] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp30], %[p30] quad_perm:[3,0,1,2]\n"
		  "v_mov_b32_dpp  %[dp31], %[p31] quad_perm:[3,0,1,2]\n"
		  "s_nop 0"
		  : [dp10] "=&v" (p0.x),
		    [dp11] "=&v" (p0.y),
			[dp20] "=&v" (p1.x),
			[dp21] "=&v" (p1.y),
			[dp30] "=&v" (p2.x),
			[dp31] "=&v" (p2.y)
		  : [p10] "0" (p0.x),
		    [p11] "1" (p0.y),
			[p20] "2" (p1.x),
			[p21] "3" (p1.y),
			[p30] "4" (p2.x),
			[p31] "5" (p2.y));
	if (get_local_id(0) != 0) {
		modify[0] ^= as_ulong(p0);
		modify[1] ^= as_ulong(p1);
		modify[2] ^= as_ulong(p2);
	} else {
		modify[0] ^= as_ulong(p2);
		modify[1] ^= as_ulong(p0);
		modify[2] ^= as_ulong(p1);
    }
}


uint broadcast_zero(uint s) {
	uint p = s;
	__asm (
		  "ds_swizzle_b32  %[dp], %[p] offset:0x8000\n"
		  "s_waitcnt lgkmcnt(0)"
		  : [dp] "=&v" (p)
		  : [p] "0" (p));
	return p;
}

/** Legacy kernel: "reduce duplex f". What it really does:
init hypermatrix[1] from [0], starting at bigMat, already offset per hash
inverting cols. We init hyper index 1 and we have only 1 to mangle. */
void make_hyper_one(ulong *state, __local ulong *xchange, __local ulong *bigMat) {
	ulong si[3];
	uint src = 0;
	uint dst = HYPERMATRIX_COUNT * 2 - STATE_BLOCK_COUNT;
	for (int loop = 0; loop < LYRA_ROUNDS; loop++)
	{
		for (int row = 0; row < 3; row++) {
			si[row] = bigMat[src];
			state[row] ^= si[row];
			src += REG_ROW_COUNT; // read sequentially huge chunks of memory!
		}
		round_lyra_4way_sw(state, xchange);
		for (int row = 0; row < 3; row++) {
			si[row] ^= state[row];
			bigMat[dst + row * REG_ROW_COUNT] = si[row]; // zigzag. Less nice.
		} // legacy kernel interleave xyzw for each row of matrix so
		// going ahead or back is no difference for them but it is for us
		// (me and my chip) because we keep the columns packed.
		dst -= STATE_BLOCK_COUNT;
	}
}



/** Legacy kernel: reduce duplex row (from) setup.
I rather think of their rows as my hyper matrix.
There are two we can use now. The first we read.
The last we modify (and we created it only a few ticks before!
So maybe LDS here as well? To be benchmarked). */
void make_next_hyper(uint matin, uint matrw, uint matout,
                     ulong *state, __local ulong *groupPad, __local ulong *bigMat) {
	ulong si[3], sII[3];
	uint hyc = HYPERMATRIX_COUNT * matin; // hyper constant
	uint hymod = HYPERMATRIX_COUNT * matrw; // hyper modify
	uint hydst = HYPERMATRIX_COUNT * matout + HYPERMATRIX_COUNT - STATE_BLOCK_COUNT;
	for (int i = 0; i < LYRA_ROUNDS; i++)
	{
		for (int row = 0; row < 3; row++)  {
			si[row] = bigMat[hyc + row * REG_ROW_COUNT];
			sII[row] = bigMat[hymod + row * REG_ROW_COUNT];
			state[row] ^= si[row] + sII[row];
		}
		round_lyra_4way_sw(state, groupPad + get_local_id(1) * 4);
		for (int row = 0; row < 3; row++) {
			si[row] ^= state[row];
			bigMat[hydst + row * REG_ROW_COUNT] = si[row];
		}
		// A nice surprise there! Before continuing, xor your mini-matrix'' by state.
		// But there's a quirk! Your s''[i] is to be xorred with s[i-1].
		// Or with s[i+11] in modulo arithmetic.
		// Private miners again shuffle those with the ISA instruction which provides
		// far more performance (1 op instead of 4, and shuffle masks are constants
		// whereas LDS isn't). So, we send forward 1 our state, rows interleaved.
		xorrot_one_dpp(sII, groupPad, state);
		for(uint cp = 0; cp < 3; cp++) bigMat[hymod + cp * REG_ROW_COUNT] = sII[cp];
		hyc += STATE_BLOCK_COUNT;
		hymod += STATE_BLOCK_COUNT;
		hydst -= STATE_BLOCK_COUNT;
	}
}


/** Legacy: reduce duplex row function? IDK.
What it does: XOR huge chunks of memory (now fully parallel and packed)!
The difference wrt building hyper matrices is
- We don't invert rows anymore so we start and walk similarly for all matrix.
- When the two matrices being modified are the same we just assign. */
void hyper_xor(uint matin, uint matrw, uint matout,
               ulong *state, __local ulong *groupPad, __local ulong *bigMat) {
	ulong si[3], sII[3];
	uint3 hyoff = (uint3)(matin* HYPERMATRIX_COUNT, matrw* HYPERMATRIX_COUNT, matout* HYPERMATRIX_COUNT);
	uint hyc = HYPERMATRIX_COUNT * matin;
	uint hymod = HYPERMATRIX_COUNT * matrw;
	uint hydst = HYPERMATRIX_COUNT * matout;
	for (int i = 0; i < LYRA_ROUNDS; i++)
	{
		for (int row = 0; row < 3; row++)  {
			si[row] = bigMat[hyc + row * REG_ROW_COUNT];
			sII[row] = bigMat[hymod + row * REG_ROW_COUNT];
		}
		for (int row = 0; row < 3; row++)  {
			si[row] += sII[row];
			state[row] ^= si[row];
		}
		round_lyra_4way_sw(state, groupPad + get_local_id(1) * 4);
		xorrot_one_dpp(sII, groupPad, state);
		// Oh noes! An 'if' inside a loop!
		// That's particularly bad: it's a 'dynamic' (or 'varying') branch
		// which means it's potentially divergent and it's basically random.
		// Every hash goes this or that way and if we could have 4-element
		// SIMD lanes we would have little problem but we have this.
		// Don't worry; we're going at memory speed anyway.
		// BTW this has both different sources and different destinations so
		// no other way to do it but just diverge.
		if (matrw != matout) {
			for (int row = 0; row < 3; row++) {
				bigMat[hymod + row * REG_ROW_COUNT] = sII[row];
				bigMat[hydst + row * REG_ROW_COUNT] ^= state[row];
			}
		}
		else {
			for (int row = 0; row < 3; row++) {
				sII[row] ^= state[row];
			    bigMat[hymod + row * REG_ROW_COUNT] = sII[row];
			}
		}
		hyc += STATE_BLOCK_COUNT;
		hymod += STATE_BLOCK_COUNT;
		hydst += STATE_BLOCK_COUNT;
	}
}



static constant ulong initial_lyra2[2][4] = {
	{ 0x6a09e667f3bcc908UL, 0xbb67ae8584caa73bUL, 0x3c6ef372fe94f82bUL, 0xa54ff53a5f1d36f1UL },
	{ 0x510e527fade682d1UL, 0x9b05688c2b3e6c1fUL, 0x1f83d9abfb41bd6bUL, 0x5be0cd19137e2179UL }
};

static constant ulong mid_mix[2][4] = {
	{ 0x20,0x20,0x20,0x01 },
	{ 0x04,0x04,0x80,0x0100000000000000 }
};
