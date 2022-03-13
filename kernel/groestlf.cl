/*
 * Groestl implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2021  fancyIX
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
 * @author   fancyIX
 */

#define PRAGMA(X) _Pragma(#X)
#define PRAGMA_UNROLL PRAGMA(unroll)
#define PRAGMA_NOUNROLL PRAGMA(nounroll)

inline uint __byte_perm(uint a, uint b, uint p) {
    uint ret;
    __asm__("v_perm_b32 %0, %1, %2, %3;" : "=&v"(ret) : "v"(b), "v"(a), "s"(p));
    return ret;
}

#define G256_Mul2(regs) \
{ \
    uint tmp = regs[7]; \
    regs[7] = regs[6]; \
    regs[6] = regs[5]; \
    regs[5] = regs[4]; \
    regs[4] = regs[3] ^ tmp; \
    regs[3] = regs[2] ^ tmp; \
    regs[2] = regs[1]; \
    regs[1] = regs[0] ^ tmp; \
    regs[0] = tmp; \
}

#define G256_AddRoundConstantQ_quad(x7, x6, x5, x4, x3, x2, x1, x0, rnd) \
{ \
    x0 = ~x0; \
    x1 = ~x1; \
    x2 = ~x2; \
    x3 = ~x3; \
    x4 = ~x4; \
    x5 = ~x5; \
    x6 = ~x6; \
    x7 = ~x7; \
 \
    uint andmask = -((get_local_id(0) & 3) == 3) & 0xFFFF0000U; \
 \
    x0 ^= ((- (rnd & 0x01)    ) & andmask); \
    x1 ^= ((-((rnd & 0x02)>>1)) & andmask); \
    x2 ^= ((-((rnd & 0x04)>>2)) & andmask); \
    x3 ^= ((-((rnd & 0x08)>>3)) & andmask); \
 \
    x4 ^= (0xAAAA0000 & andmask); \
    x5 ^= (0xCCCC0000 & andmask); \
    x6 ^= (0xF0F00000 & andmask); \
    x7 ^= (0xFF000000 & andmask); \
}

#define G256_AddRoundConstantP_quad(x7, x6, x5, x4, x3, x2, x1, x0, rnd) \
{ \
    if ((get_local_id(0) & 3) == 0) { \
      int andmask = 0xFFFF; \
   \
      x0 ^= ((- (rnd & 0x01)    ) & andmask); \
      x1 ^= ((-((rnd & 0x02)>>1)) & andmask); \
      x2 ^= ((-((rnd & 0x04)>>2)) & andmask); \
      x3 ^= ((-((rnd & 0x08)>>3)) & andmask); \
   \
      x4 ^= 0xAAAAU; \
      x5 ^= 0xCCCCU; \
      x6 ^= 0xF0F0U; \
      x7 ^= 0xFF00U; \
    } \
}

#define G16mul_quad(xx3, xx2, xx1, xx0, yy3, yy2, yy1, yy0) \
{ \
    uint tt0,tt1,tt2; \
     \
    tt0 = ((xx2 ^ xx0) ^ (xx3 ^ xx1)) & ((yy2 ^ yy0) ^ (yy3 ^ yy1)); \
    tt1 = ((xx2 ^ xx0) & (yy2 ^ yy0)) ^ tt0; \
    tt2 = ((xx3 ^ xx1) & (yy3 ^ yy1)) ^ tt0 ^ tt1; \
 \
    tt0 = (xx2^xx3) & (yy2^yy3); \
    xx3 = (xx3 & yy3) ^ tt0 ^ tt1; \
    xx2 = (xx2 & yy2) ^ tt0 ^ tt2; \
 \
    tt0 = (xx0^xx1) & (yy0^yy1); \
    xx1 = (xx1 & yy1) ^ tt0 ^ tt1; \
    xx0 = (xx0 & yy0) ^ tt0 ^ tt2; \
}

#define G256_inv_quad(x7, x6, x5, x4, x3, x2, x1, x0) \
{ \
    uint t0,t1,t2,t3,t4,t5,t6,a,b; \
 \
    t3 = x7; \
    t2 = x6; \
    t1 = x5; \
    t0 = x4; \
 \
    G16mul_quad(t3, t2, t1, t0, x3, x2, x1, x0); \
 \
    a = (x4 ^ x0); \
    t0 ^= a; \
    t2 ^= (x7 ^ x3) ^ (x5 ^ x1);  \
    t1 ^= (x5 ^ x1) ^ a; \
    t3 ^= (x6 ^ x2) ^ a; \
 \
    b = t0 ^ t1; \
    t4 = (t2 ^ t3) & b; \
    a = t4 ^ t3 ^ t1; \
    t5 = (t3 & t1) ^ a; \
    t6 = (t2 & t0) ^ a ^ (t2 ^ t0); \
 \
    t4 = (t5 ^ t6) & b; \
    t1 = (t6 & t1) ^ t4; \
    t0 = (t5 & t0) ^ t4; \
 \
    t4 = (t5 ^ t6) & (t2^t3); \
    t3 = (t6 & t3) ^ t4; \
    t2 = (t5 & t2) ^ t4; \
 \
    G16mul_quad(x3, x2, x1, x0, t1, t0, t3, t2); \
 \
    G16mul_quad(x7, x6, x5, x4, t1, t0, t3, t2); \
}

#define transAtoX_quad(x0, x1, x2, x3, x4, x5, x6, x7) \
{ \
    uint t0, t1; \
    t0 = x0 ^ x1 ^ x2; \
    t1 = x5 ^ x6; \
    x2 = t0 ^ t1 ^ x7; \
    x6 = t0 ^ x3 ^ x6; \
    x3 = x0 ^ x1 ^ x3 ^ x4 ^ x7;     \
    x4 = x0 ^ x4 ^ t1; \
    x2 = t0 ^ t1 ^ x7; \
    x1 = x0 ^ x1 ^ t1; \
    x7 = x0 ^ t1 ^ x7; \
    x5 = x0 ^ t1; \
}

#define transXtoA_quad(x0, x1, x2, x3, x4, x5, x6, x7) \
{ \
    uint t0,t2,t3,t5; \
 \
    x1 ^= x4; \
    t0 = x1 ^ x6; \
    x1 ^= x5; \
 \
    t2 = x0 ^ x2; \
    x2 = x3 ^ x5; \
    t2 ^= x2 ^ x6; \
    x2 ^= x7; \
    t3 = x4 ^ x2 ^ x6; \
 \
    t5 = x0 ^ x6; \
    x4 = x3 ^ x7; \
    x0 = x3 ^ x5; \
 \
    x6 = t0;     \
    x3 = t2; \
    x7 = t3;     \
    x5 = t5;     \
}

#define sbox_quad(r) \
{ \
    transAtoX_quad(r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]); \
 \
    G256_inv_quad(r[2], r[4], r[1], r[7], r[3], r[0], r[5], r[6]); \
 \
    transXtoA_quad(r[7], r[1], r[4], r[2], r[6], r[5], r[0], r[3]); \
     \
    r[0] = ~r[0]; \
    r[1] = ~r[1]; \
    r[5] = ~r[5]; \
    r[6] = ~r[6]; \
}

#define G256_ShiftBytesP_quad(x7, x6, x5, x4, x3, x2, x1, x0) \
{ \
    uint t0,t1; \
 \
    int tpos = get_local_id(0) & 0x03; \
    int shift1 = tpos << 1; \
    int shift2 = shift1+1 + ((tpos == 3)<<2); \
 \
    t0 = __byte_perm(x0, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x0, 0, 0x03020302)>>shift2; \
    x0 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x1, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x1, 0, 0x03020302)>>shift2; \
    x1 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x2, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x2, 0, 0x03020302)>>shift2; \
    x2 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x3, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x3, 0, 0x03020302)>>shift2; \
    x3 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x4, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x4, 0, 0x03020302)>>shift2; \
    x4 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x5, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x5, 0, 0x03020302)>>shift2; \
    x5 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x6, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x6, 0, 0x03020302)>>shift2; \
    x6 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x7, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x7, 0, 0x03020302)>>shift2; \
    x7 = __byte_perm(t0, t1, 0x05040100); \
}

#define G256_ShiftBytesQ_quad(x7, x6, x5, x4, x3, x2, x1, x0) \
{ \
    uint t0,t1; \
 \
    int tpos = get_local_id(0) & 0x03; \
    int shift1 = (1-(tpos>>1)) + ((tpos & 0x01)<<2); \
    int shift2 = shift1+2 + ((tpos == 1)<<2); \
 \
    t0 = __byte_perm(x0, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x0, 0, 0x03020302)>>shift2; \
    x0 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x1, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x1, 0, 0x03020302)>>shift2; \
    x1 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x2, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x2, 0, 0x03020302)>>shift2; \
    x2 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x3, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x3, 0, 0x03020302)>>shift2; \
    x3 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x4, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x4, 0, 0x03020302)>>shift2; \
    x4 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x5, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x5, 0, 0x03020302)>>shift2; \
    x5 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x6, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x6, 0, 0x03020302)>>shift2; \
    x6 = __byte_perm(t0, t1, 0x05040100); \
 \
    t0 = __byte_perm(x7, 0, 0x01000100)>>shift1; \
    t1 = __byte_perm(x7, 0, 0x03020302)>>shift2; \
    x7 = __byte_perm(t0, t1, 0x05040100); \
}

#define __shfl_sync(ret, v, u) { \
    if ((u & 0x3) == 0) { \
      ret = v; \
    } \
    if ((u & 0x3) == 1) { \
      __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[1,2,3,0]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=&v" (ret) \
          : [a0] "v" (v)); \
    } \
    if ((u & 0x3) == 2) { \
      __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[2,3,0,1]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=&v" (ret) \
          : [a0] "v" (v)); \
    } \
    if ((u & 0x3) == 3) { \
      __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[3,0,1,2]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=&v" (ret) \
          : [a0] "v" (v)); \
    } \
}

#define SHIFT64_16(hi, lo)    __byte_perm(lo, hi, 0x05040302)
#define A_GR(ret, r, v, u)             __shfl_sync(ret, r[v], u)
#define S_GR(ret, r, idx, l)            {uint a0; uint a1; A_GR(a0, r, idx, (l+1)); A_GR(a1, r, idx, l); ret = SHIFT64_16( a0, a1);}

#define DOUBLE_ODD(ret, r, i, bc)        {uint s0; uint s1; S_GR(s0, r, i, (bc)); A_GR(s1, r, i, (bc) + 1); ret = ( s0 ^ s1 );}
#define DOUBLE_EVEN(ret, r, i, bc)        {uint s0; uint s1; S_GR(s0, r, i, (bc)); A_GR(s1, r, i, (bc)    ); ret = ( s0 ^ s1 );}

#define SINGLE_ODD(ret, r, i, bc)        { S_GR(ret, r, i, (bc)) }
#define SINGLE_EVEN(ret, r, i, bc)        { A_GR(ret, r, i, (bc)) }

#define G256_MixFunction_quad(r) \
{ \
 \
    uint b[8]; \
 \
PRAGMA_UNROLL \
    for(int i=0;i<8;i++) { \
        uint ret0; \
        uint ret1; \
        DOUBLE_ODD(ret0, r, i, 1); \
        DOUBLE_EVEN(ret1, r, i, 3) \
        b[i] = ret0 ^ ret1; \
    } \
    G256_Mul2(b); \
PRAGMA_UNROLL \
    for(int i=0;i<8;i++) { \
        uint ret0; \
        uint ret1; \
        uint ret2; \
        DOUBLE_ODD(ret0, r, i, 3); \
        DOUBLE_ODD(ret1, r, i, 4); \
        SINGLE_ODD(ret2, r, i, 6); \
        b[i] = b[i] ^ ret0 ^ ret1 ^ ret2; \
    } \
    G256_Mul2(b); \
PRAGMA_UNROLL \
    for(int i=0;i<8;i++) { \
        uint ret0; \
        uint ret1; \
        uint ret2; \
        DOUBLE_EVEN(ret0, r, i, 2); \
        DOUBLE_EVEN(ret1, r, i, 3); \
        SINGLE_EVEN(ret2, r, i, 5); \
        r[i] = b[i] ^ ret0 ^ ret1 ^ ret2; \
    } \
}

#define groestl512_perm_P_quad(r) \
{ \
    for(int idx=0;idx<14;idx++) \
    { \
        G256_AddRoundConstantP_quad(r[7], r[6], r[5], r[4], r[3], r[2], r[1], r[0], idx); \
        sbox_quad(r); \
        G256_ShiftBytesP_quad(r[7], r[6], r[5], r[4], r[3], r[2], r[1], r[0]); \
        G256_MixFunction_quad(r); \
    } \
}

#define groestl512_perm_Q_quad(r) \
{     \
    for(int idx=0;idx<14;idx++) \
    { \
        G256_AddRoundConstantQ_quad(r[7], r[6], r[5], r[4], r[3], r[2], r[1], r[0], idx); \
        sbox_quad(r); \
        G256_ShiftBytesQ_quad(r[7], r[6], r[5], r[4], r[3], r[2], r[1], r[0]); \
        G256_MixFunction_quad(r); \
    } \
}

#define groestl512_progressMessage_quad(state, message) \
{ \
PRAGMA_UNROLL \
    for(int u=0;u<8;u++) state[u] = message[u]; \
 \
    if ((get_local_id(0) & 0x03) == 3) state[ 1] ^= 0x00008000; \
    groestl512_perm_P_quad(state); \
    if ((get_local_id(0) & 0x03) == 3) state[ 1] ^= 0x00008000; \
    groestl512_perm_Q_quad(message); \
PRAGMA_UNROLL \
    for(int u=0;u<8;u++) state[u] ^= message[u]; \
PRAGMA_UNROLL \
    for(int u=0;u<8;u++) message[u] = state[u]; \
    groestl512_perm_P_quad(message); \
PRAGMA_UNROLL \
    for(int u=0;u<8;u++) state[u] ^= message[u]; \
}



#define merge8_GR(z,x,y) { \
	z=__byte_perm(x, y, 0x05010400); \
}

#define SWAP8_GR(x,y) { \
	x=__byte_perm(x, y, 0x05040100); \
	y=__byte_perm(x, y, 0x07060302); \
}

#define SWAP4_GR(x,y) { \
	t = (y<<4); \
	t = (x ^ t); \
	t = 0xf0f0f0f0UL & t; \
	x = (x ^ t); \
	t=  t>>4; \
	y=  y ^ t; \
}

#define SWAP2_GR(x,y) { \
	t = (y<<2); \
	t = (x ^ t); \
	t = 0xccccccccUL & t; \
	x = (x ^ t); \
	t=  t>>2; \
	y=  y ^ t; \
}

#define SWAP1_GR(x,y) { \
	t = (y+y); \
	t = (x ^ t); \
	t = 0xaaaaaaaaUL & t; \
	x = (x ^ t); \
	t=  t>>1; \
	y=  y ^ t; \
}

#define to_bitslice_quad(input, output) \
{ \
	uint other[8]; \
	uint d[8]; \
	uint t; \
 \
PRAGMA_UNROLL \
	for (int i = 0; i < 8; i++) { \
    __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[0,2,1,3]\n" \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d1], %[d0] quad_perm:[1,2,3,0]\n" \
          "v_mov_b32_dpp  %[d0], %[d0] quad_perm:[0,0,2,2]\n" \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d1], %[d1] quad_perm:[0,0,2,2]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=v" (input[i]), \
            [d1] "=v" (other[i]) \
          : [a0] "0" (input[i])); \
		if (get_local_id(0) & 1) { \
      __asm__("v_perm_b32 %0, %1, %2, %3;" : "=v"(input[i]) : "0"(0), "v"(input[i]), "s"(0x01000302)); \
      __asm__("v_perm_b32 %0, %1, %2, %3;" : "=v"(other[i]) : "0"(0), "v"(other[i]), "s"(0x01000302)); \
		} \
	} \
 \
	merge8_GR(d[0], input[0], input[4]); \
	merge8_GR(d[1], other[0], other[4]); \
	merge8_GR(d[2], input[1], input[5]); \
	merge8_GR(d[3], other[1], other[5]); \
	merge8_GR(d[4], input[2], input[6]); \
	merge8_GR(d[5], other[2], other[6]); \
	merge8_GR(d[6], input[3], input[7]); \
	merge8_GR(d[7], other[3], other[7]); \
 \
	SWAP1_GR(d[0], d[1]); \
	SWAP1_GR(d[2], d[3]); \
	SWAP1_GR(d[4], d[5]); \
	SWAP1_GR(d[6], d[7]); \
 \
	SWAP2_GR(d[0], d[2]); \
	SWAP2_GR(d[1], d[3]); \
	SWAP2_GR(d[4], d[6]); \
	SWAP2_GR(d[5], d[7]); \
 \
	SWAP4_GR(d[0], d[4]); \
	SWAP4_GR(d[1], d[5]); \
	SWAP4_GR(d[2], d[6]); \
	SWAP4_GR(d[3], d[7]); \
 \
	output[0] = d[0]; \
	output[1] = d[1]; \
	output[2] = d[2]; \
	output[3] = d[3]; \
	output[4] = d[4]; \
	output[5] = d[5]; \
	output[6] = d[6]; \
	output[7] = d[7]; \
}

#define from_bitslice_quad(input, output) \
{ \
	uint d[8]; \
	uint t; \
 \
	d[0] = __byte_perm(input[0], input[4], 0x07050301); \
	d[1] = __byte_perm(input[1], input[5], 0x07050301); \
	d[2] = __byte_perm(input[2], input[6], 0x07050301); \
	d[3] = __byte_perm(input[3], input[7], 0x07050301); \
 \
	SWAP1_GR(d[0], d[1]); \
	SWAP1_GR(d[2], d[3]); \
 \
	SWAP2_GR(d[0], d[2]); \
	SWAP2_GR(d[1], d[3]); \
 \
	t = __byte_perm(d[0], d[2], 0x05040100); \
	d[2] = __byte_perm(d[0], d[2], 0x07060302); \
	d[0] = t; \
 \
	t = __byte_perm(d[1], d[3], 0x05040100); \
	d[3] = __byte_perm(d[1], d[3], 0x07060302); \
	d[1] = t; \
 \
	SWAP4_GR(d[0], d[2]); \
	SWAP4_GR(d[1], d[3]); \
 \
	output[0] = d[0]; \
	output[2] = d[1]; \
	output[4] = d[0] >> 16; \
	output[6] = d[1] >> 16; \
	output[8] = d[2]; \
	output[10] = d[3]; \
	output[12] = d[2] >> 16; \
	output[14] = d[3] >> 16; \
 \
PRAGMA_UNROLL \
	for (int i = 0; i < 16; i+=2) { \
		if (get_local_id(0) & 1) output[i] = __byte_perm(output[i], 0, 0x01000302); \
    uint temp; \
    __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[1,2,3,0]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=&v" (temp) \
          : [a0] "v" ((int)output[i])); \
		output[i] = __byte_perm(output[i], temp, 0x07060100); \
    __asm ( \
          "s_nop 0\n" \
          "s_nop 0\n" \
          "v_mov_b32_dpp  %[d0], %[a0] quad_perm:[2,3,0,1]\n" \
          "s_nop 0\n" \
          "s_nop 0" \
          : [d0] "=&v" (output[i+1]) \
          : [a0] "v" ((int)output[i])); \
		if (get_local_id(0) & 3) output[i] = output[i+1] = 0; \
	} \
}


