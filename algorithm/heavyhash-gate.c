#include "algorithm.h"
#include "config.h"
#include "miner.h"

#include "heavyhash-gate.h"
#include "keccak_tiny.h"

#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>

#define bswap_32(x) ((((x) << 24) & 0xff000000u) | (((x) << 8) & 0x00ff0000u) \
	| (((x) >> 8) & 0x0000ff00u) | (((x) >> 24) & 0x000000ffu))

static inline void mm128_bswap32_80( void *d, void *s )
{
  ( (uint32_t*)d )[ 0] = bswap_32( ( (uint32_t*)s )[ 0] );
  ( (uint32_t*)d )[ 1] = bswap_32( ( (uint32_t*)s )[ 1] );
  ( (uint32_t*)d )[ 2] = bswap_32( ( (uint32_t*)s )[ 2] );
  ( (uint32_t*)d )[ 3] = bswap_32( ( (uint32_t*)s )[ 3] );
  ( (uint32_t*)d )[ 4] = bswap_32( ( (uint32_t*)s )[ 4] );
  ( (uint32_t*)d )[ 5] = bswap_32( ( (uint32_t*)s )[ 5] );
  ( (uint32_t*)d )[ 6] = bswap_32( ( (uint32_t*)s )[ 6] );
  ( (uint32_t*)d )[ 7] = bswap_32( ( (uint32_t*)s )[ 7] );
  ( (uint32_t*)d )[ 8] = bswap_32( ( (uint32_t*)s )[ 8] );
  ( (uint32_t*)d )[ 9] = bswap_32( ( (uint32_t*)s )[ 9] );
  ( (uint32_t*)d )[10] = bswap_32( ( (uint32_t*)s )[10] );
  ( (uint32_t*)d )[11] = bswap_32( ( (uint32_t*)s )[11] );
  ( (uint32_t*)d )[12] = bswap_32( ( (uint32_t*)s )[12] );
  ( (uint32_t*)d )[13] = bswap_32( ( (uint32_t*)s )[13] );
  ( (uint32_t*)d )[14] = bswap_32( ( (uint32_t*)s )[14] );
  ( (uint32_t*)d )[15] = bswap_32( ( (uint32_t*)s )[15] );
  ( (uint32_t*)d )[16] = bswap_32( ( (uint32_t*)s )[16] );
  ( (uint32_t*)d )[17] = bswap_32( ( (uint32_t*)s )[17] );
  ( (uint32_t*)d )[18] = bswap_32( ( (uint32_t*)s )[18] );
  ( (uint32_t*)d )[19] = bswap_32( ( (uint32_t*)s )[19] );
}

#define EPS 1e-9

uint64_t le64dec(const void *pp)
{
    const uint8_t *p = (uint8_t const *)pp;
    return ((uint64_t)(p[0]) | ((uint64_t)(p[1]) << 8) |
            ((uint64_t)(p[2]) << 16) | ((uint64_t)(p[3]) << 24)) |
            ((uint64_t)(p[4]) << 32) | ((uint64_t)(p[5]) << 40) |
            ((uint64_t)(p[6]) << 48) | ((uint64_t)(p[7]) << 56);
}

static inline uint64_t rotl64(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static inline uint64_t xoshiro_gen(struct xoshiro_state *state) {
    const uint64_t result = rotl64(state->s[0] + state->s[3], 23) + state->s[0];

    const uint64_t t = state->s[1] << 17;

    state->s[2] ^= state->s[0];
    state->s[3] ^= state->s[1];
    state->s[1] ^= state->s[2];
    state->s[0] ^= state->s[3];

    state->s[2] ^= t;

    state->s[3] = rotl64(state->s[3], 45);

    return result;
}

static int compute_rank(const uint8_t A[64][64])
{
    double B[64][64];
    for (int i = 0; i < 64; ++i){
        for(int j = 0; j < 64; ++j){
            B[i][j] = A[i][j];
        }
    }

    int rank = 0;
    bool row_selected[64] = {};

    for (int i = 0; i < 64; ++i) {
        int j;
        for (j = 0; j < 64; ++j) {
            if (!row_selected[j] && fabs(B[j][i]) > EPS)
                break;
        }
        if (j != 64) {
            ++rank;
            row_selected[j] = true;
            for (int p = i + 1; p < 64; ++p)
                B[j][p] /= B[j][i];
            for (int k = 0; k < 64; ++k) {
                if (k != j && fabs(B[k][i]) > EPS) {
                    for (int p = i + 1; p < 64; ++p)
                        B[k][p] -= B[j][p] * B[k][i];
                }
            }
        }
    }
    return rank;
}

static inline bool is_full_rank(const uint8_t matrix[64][64])
{
    return compute_rank(matrix) == 64;
}

void generate_matrix(uint8_t matrix[64][64], struct xoshiro_state *state) {
    do {
        for (int i = 0; i < 64; ++i) {
            for (int j = 0; j < 64; j += 16) {
                uint64_t value = xoshiro_gen(state);
                for (int shift = 0; shift < 16; ++shift) {
                    matrix[i][j + shift] = (value >> (4*shift)) & 0xF;
                }
            }
        }
    } while (!is_full_rank(matrix));
}

void heavyhash(const uint8_t matrix[64][64], uint8_t* pdata, size_t pdata_len, uint8_t* output)
{
    uint8_t hash_first[32] __attribute__((aligned(64)));
    uint8_t hash_second[32] __attribute__((aligned(64)));
    uint8_t hash_xored[32] __attribute__((aligned(64)));

    uint16_t vector[64] __attribute__((aligned(64)));
    uint16_t product[64] __attribute__((aligned(64)));

    kt_sha3_256((uint8_t*) hash_first, 32, pdata, pdata_len);

    for (int i = 0; i < 32; ++i) {
        vector[2*i] = (hash_first[i] >> 4);
        vector[2*i+1] = hash_first[i] & 0xF;
    }

    for (int i = 0; i < 64; ++i) {
        uint16_t sum = 0;
        for (int j = 0; j < 64; ++j) {
            sum += matrix[i][j] * vector[j];
        }
        product[i] = (sum >> 10);
    }

    for (int i = 0; i < 32; ++i) {
        hash_second[i] = (product[2*i] << 4) | (product[2*i+1]);
    }

    for (int i = 0; i < 32; ++i) {
        hash_xored[i] = hash_first[i] ^ hash_second[i];
    }
    kt_sha3_256(output, 32, hash_xored, 32);
}

static const uint32_t diff1targ = 0x0000ffff;

int heavyhash_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	uint32_t data[20], ohash[8];

	be32enc_vect(data, (const uint32_t *)pdata, 19);
    
    uint32_t seed[8];

    uint8_t matrix[64][64];
    struct xoshiro_state state;

    kt_sha3_256((uint8_t *)seed, 32, (uint8_t *)(data+1), 32);

    for (int i = 0; i < 4; ++i) {
        state.s[i] = le64dec(seed + 2*i);
    }

    generate_matrix(matrix, &state);

    data[19] = htobe32(nonce);
	heavyhash(matrix, (uint8_t *)data, 80, (uint8_t *)ohash);
    
	tmp_hash7 = be32toh(ohash[7]);

	if (tmp_hash7 > diff1targ)
		return -1;
	if (tmp_hash7 > Htarg)
		return 0;
	return 1;
}

void heavyhash_regenhash(struct work *work)
{
        uint32_t data[20];
        uint32_t *nonce = (uint32_t *)(work->data + 76);
        uint32_t *ohash = (uint32_t *)(work->hash);

        be32enc_vect(data, (const uint32_t *)work->data, 19);
    
    uint32_t seed[8];

    uint8_t matrix[64][64];
    struct xoshiro_state state;

    kt_sha3_256((uint8_t *)seed, 32, (uint8_t*) (data+1), 32);

    for (int i = 0; i < 4; ++i) {
        state.s[i] = le64dec(seed + 2*i);
    }

    generate_matrix(matrix, &state);

    data[19] = htobe32(*nonce);
	heavyhash(matrix, (uint8_t *)data, 80, (uint8_t *)ohash);
}

bool scanhash_heavyhash(struct thr_info *thr, const unsigned char *pmidstate,
		     unsigned char *pdata, unsigned char *phash1,
		     unsigned char *phash, const unsigned char *ptarget,
		     uint32_t max_nonce, uint32_t *last_nonce, uint32_t n)
{
    bool ret = false;

    uint32_t edata[20] __attribute__((aligned(64)));
    uint32_t hash[8] __attribute__((aligned(64)));
    uint32_t seed[8] __attribute__((aligned(64)));

    const uint32_t first_nonce = ((uint32_t *)pdata)[19];

    uint8_t matrix[64][64] __attribute__((aligned(64)));
    struct xoshiro_state state;

    mm128_bswap32_80( edata, pdata );

    kt_sha3_256((uint8_t *) seed, 32, (uint8_t *) (edata+1), 32);

    for (int i = 0; i < 4; ++i) {
        state.s[i] = le64dec(seed + 2*i);
    }

    generate_matrix(matrix, &state);

    do
    {
        edata[19] = n;
		heavyhash(matrix, (uint8_t *)edata, 80, (uint8_t *)hash);
        /*if ( unlikely( valid_hash( hash, ptarget )) )
        {
            pdata[19] = bswap_32(n);
            ret = true;
            *last_nonce = n;
            break;
        }*/
        n++;
    } while ( n < *last_nonce && ! (thr->work_restart) );

    pdata[19] = n;
    return ret;
}
