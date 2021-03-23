/*
 * GroestlCoin kernel implementation.
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

#ifndef GROESTLCOIN_CL
#define GROESTLCOIN_CL

#ifdef __gfx900__
#else

#define SWAP32(a)    (as_uint(as_uchar4(a).wzyx))

#include "groestlf.cl"


__attribute__((reqd_work_group_size(WORKSIZE, 1, 1)))
__kernel void search(__global unsigned char* block, __global uint* output, __global uint* pTarget)
{
  uint gid = get_global_id(0);
  uint nounce = gid >> 2;
  uint thread = gid - get_global_offset(0);

  uint paddedInput[8] = {0};

#pragma unroll 8
  for(int k=0;k<8;k++) {
    uint idx = 4 * k + (get_local_id(0) & 0x3);
    if (idx < 20) {
      paddedInput[k] = ((__global uint *) (block))[idx];
    }
    if (idx == 20) {
      paddedInput[k] = 0x80;
    }
    if (idx == 31) {
      paddedInput[k] = 0x01000000;
    }
  }

  if ((get_local_id(0) % 4) == 3)
    paddedInput[4] = SWAP32(nounce);  // 4*4+3 = 19

  uint msgBitsliced[8];
  to_bitslice_quad(paddedInput, msgBitsliced);

  uint state[8];
  for (int round=0; round<2; round++)
  {
    groestl512_progressMessage_quad(state, msgBitsliced);

    if (round < 1)
    {
      // Verkettung zweier Runden inclusive Padding.
      msgBitsliced[ 0] = __byte_perm(state[ 0], 0x00800100, 0x04030401 + ((get_local_id(0)%4)==3)*0x02000000);
      msgBitsliced[ 1] = __byte_perm(state[ 1], 0x00800100, 0x04030401);
      msgBitsliced[ 2] = __byte_perm(state[ 2], 0x00800100, 0x04030401);
      msgBitsliced[ 3] = __byte_perm(state[ 3], 0x00800100, 0x04030401);
      msgBitsliced[ 4] = __byte_perm(state[ 4], 0x00800100, 0x04030401);
      msgBitsliced[ 5] = __byte_perm(state[ 5], 0x00800100, 0x04030401);
      msgBitsliced[ 6] = __byte_perm(state[ 6], 0x00800100, 0x04030401);
      msgBitsliced[ 7] = __byte_perm(state[ 7], 0x00800100, 0x04030401 + ((get_local_id(0)%4)==0)*0x00000100);
    }
  }

  // Nur der erste von jeweils 4 Threads bekommt das Ergebns-Hash
  uint out_state[16];
  from_bitslice_quad(state, out_state);

  if ((get_local_id(0) & 0x3) == 0)
  {
    int i, position = -1;
    bool rc = true;

    #pragma unroll 8
    for (i = 7; i >= 0; i--) {
      if (out_state[i] > pTarget[i]) {
        if(position < i) {
          position = i;
          rc = false;
        }
        }
        if (out_state[i] < pTarget[i]) {
        if(position < i) {
          position = i;
          rc = true;
        }
        }
    }

    if(rc)
      output[output[0xFF]++] = (nounce);
  }

  barrier(CLK_GLOBAL_MEM_FENCE);
}

#endif
#endif // GROESTLCOIN_CL
