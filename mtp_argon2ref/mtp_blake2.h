/*
 * MTPArgon2 reference source code package - reference C implementations
 *
 * Copyright 2015
 * Daniel Dinu, Dmitry Khovratovich, Jean-Philippe Aumasson, and Samuel Neves
 *
 * You may use this work under the terms of a Creative Commons CC0 1.0 
 * License/Waiver or the Apache Public License 2.0, at your option. The terms of
 * these licenses can be found at:
 *
 * - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 * - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0
 *
 * You should have received a copy of both of these licenses along with this
 * software. If not, they may be obtained at the above URLs.
 */

#ifndef PORTABLE_MTP_BLAKE2_H
#define PORTABLE_MTP_BLAKE2_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#if defined(__cplusplus)
extern "C" {
#endif

enum amtp_blake2b_constant {
    amtp_blake2b_argon_blockBYTES = 128,
    amtp_blake2b_OUTBYTES = 64,
    amtp_blake2b_KEYBYTES = 64,
    amtp_blake2b_SALTBYTES = 16,
    amtp_blake2b_PERSONALBYTES = 16
};

#pragma pack(push, 1)
typedef struct __amtp_blake2b_param {
    uint8_t digest_length;                   /* 1 */
    uint8_t key_length;                      /* 2 */
    uint8_t fanout;                          /* 3 */
    uint8_t depth;                           /* 4 */
    uint32_t leaf_length;                    /* 8 */
    uint64_t node_offset;                    /* 16 */
    uint8_t node_depth;                      /* 17 */
    uint8_t inner_length;                    /* 18 */
    uint8_t reserved[14];                    /* 32 */
    uint8_t salt[amtp_blake2b_SALTBYTES];         /* 48 */
    uint8_t personal[amtp_blake2b_PERSONALBYTES]; /* 64 */
} amtp_blake2b_param;
#pragma pack(pop)

typedef struct __amtp_blake2b_state {
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[amtp_blake2b_argon_blockBYTES];
    unsigned buflen;
    unsigned outlen;
    uint8_t last_node;
} amtp_blake2b_state;

/* Ensure param structs have not been wrongly padded */
/* Poor man's static_assert */
enum {
    mtp_blake2_size_check_0 = 1 / !!(CHAR_BIT == 8),
    mtp_blake2_size_check_2 =
        1 / !!(sizeof(amtp_blake2b_param) == sizeof(uint64_t) * CHAR_BIT)
};

/* Streaming API */
int amtp_blake2b_init(amtp_blake2b_state *S, size_t outlen);
int amtp_blake2b_init_key(amtp_blake2b_state *S, size_t outlen, const void *key,
                     size_t keylen);
int amtp_blake2b_init_param(amtp_blake2b_state *S, const amtp_blake2b_param *P);
int amtp_blake2b_update(amtp_blake2b_state *S, const void *in, size_t inlen);
int amtp_blake2b_final(amtp_blake2b_state *S, void *out, size_t outlen);

int amtp_blake2b_update_test(amtp_blake2b_state *S, const void *in, size_t inlen);
int amtp_blake2b_final_test(amtp_blake2b_state *S, void *out, size_t outlen);


int amtp_blake2b4rounds_update(amtp_blake2b_state *S, const void *in, size_t inlen);
int amtp_blake2b4rounds_final(amtp_blake2b_state *S, void *out, size_t outlen);

int amtp_blake2b_long(void * pout, size_t outlen, const void * in, size_t inlen);


int amtp_blake2b_long2(void * pout, size_t outlen, const void * in, size_t inlen);
/* Simple API */
int mtp_blake2b(void *out, size_t outlen, const void *in, size_t inlen,
            const void *key, size_t keylen);



#if defined(__cplusplus)
}
#endif

#endif
