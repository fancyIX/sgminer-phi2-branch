#define stateNum (3)
#define rowNum (8)
#define GID (get_global_id(0) - get_global_offset(0))
#define GSZ (get_global_size(0))
#define DMatrix(i, j, k) (Matrix[GID + GSZ * (((j) + stateNum * (i)) * rowNum + (k))])

#pragma OPENCL EXTENSION cl_amd_media_ops : enable
#pragma OPENCL EXTENSION cl_amd_media_ops2 : enable

inline ulong ROTR64(const ulong x2, const uint y)
{
	uint2 x = as_uint2(x2);
	if(y < 32) return(as_ulong(amd_bitalign(x.s10, x, y)));
	else return(as_ulong(amd_bitalign(x, x.s10, (y - 32))));
}

#define Gfunc(a, b, c, d)                                                      \
    {                                                                          \
        a += b;                                                                \
        d ^= a;                                                                \
        ttr = ROTR64(d, 32);                                                   \
        d = ttr;                                                               \
                                                                               \
        c += d;                                                                \
        b ^= c;                                                                \
        ttr = ROTR64(b, 24);                                                   \
        b = ttr;                                                               \
                                                                               \
        a += b;                                                                \
        d ^= a;                                                                \
        ttr = ROTR64(d, 16);                                                   \
        d = ttr;                                                               \
                                                                               \
        c += d;                                                                \
        b ^= c;                                                                \
        ttr = ROTR64(b, 63);                                                   \
        b = ttr;                                                               \
    }

inline void roundLyra(ulong2 *state) {
    ulong ttr;
    Gfunc(state[0].x, state[2].x, state[4].x, state[6].x);
    Gfunc(state[0].y, state[2].y, state[4].y, state[6].y);
    Gfunc(state[1].x, state[3].x, state[5].x, state[7].x);
    Gfunc(state[1].y, state[3].y, state[5].y, state[7].y);
    Gfunc(state[0].x, state[2].y, state[5].x, state[7].y);
    Gfunc(state[0].y, state[3].x, state[5].y, state[6].x);
    Gfunc(state[1].x, state[3].y, state[4].x, state[6].y);
    Gfunc(state[1].y, state[2].x, state[4].y, state[7].x);
}

inline void reduceDuplexRowSetup(int rowIn, int rowInOut, int rowOut, ulong4 *state, __global ulong4 *Matrix) {
    ulong4 temp[3];
    ulong4 temp2[3];
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        #pragma unroll
		for (int j = 0; j < 3; j++) {
            temp[j] = DMatrix(i, j, rowIn);
            temp2[j] = DMatrix(i, j, rowInOut);
            state[j] ^= temp[j] + temp2[j];
        }
		roundLyra((ulong2 *) state);
        #pragma unroll
		for (int j = 0; j < 3; j++) {
            temp[j] ^= state[j];
            DMatrix((7 - i), j, rowOut) = temp[j];
        }
        temp[0] = (ulong4) (state[2].w, state[0].x, state[0].y, state[0].z);
        temp[1] = (ulong4) (state[0].w, state[1].x, state[1].y, state[1].z);
        temp[2] = (ulong4) (state[1].w, state[2].x, state[2].y, state[2].z);
		DMatrix(i, 0, rowInOut) = temp2[0] ^ temp[0];
		DMatrix(i, 1, rowInOut) = temp2[1] ^ temp[1];
		DMatrix(i, 2, rowInOut) = temp2[2] ^ temp[2];
       
	}
}

inline void reduceDuplexRow(int rowIn, int rowInOut, int rowOut, ulong4 *state, __global ulong4 *Matrix) {
	 ulong4 temp[3];
     ulong4 temp2[3];
     #pragma unroll
     for (int i = 0; i < 8; i++) {
         #pragma unroll
        for (int j = 0; j < 3; j++) {
            temp[j] = DMatrix(i, j, rowIn);
            temp2[j] = DMatrix(i, j, rowInOut);
            temp[j] += temp2[j];
            state[j] ^= temp[j];
        }
        roundLyra((ulong2 *) state);
        temp[0] = (ulong4) (state[2].w, state[0].x, state[0].y, state[0].z);
        temp[1] = (ulong4) (state[0].w, state[1].x, state[1].y, state[1].z);
        temp[2] = (ulong4) (state[1].w, state[2].x, state[2].y, state[2].z);
        DMatrix(i, 0, rowInOut) = temp2[0] ^ temp[0];
        DMatrix(i, 1, rowInOut) = temp2[1] ^ temp[1];
        DMatrix(i, 2, rowInOut) = temp2[2] ^ temp[2];
        #pragma unroll
        for (int j = 0; j < 3; j++) {
            DMatrix(i, j, rowOut) ^= state[j];
        }
	}
}

inline void absorbblock(int in, ulong4 *state, __global ulong4 *Matrix) {
	state[0] ^= DMatrix(0, 0, in);
	state[1] ^= DMatrix(0, 1, in);
	state[2] ^= DMatrix(0, 2, in);
}
