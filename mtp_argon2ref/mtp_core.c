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

/*For memory wiping*/
#ifdef _MSC_VER
#include <windows.h>
#include <winbase.h> /* For SecureZeroMemory */
#endif
#if defined __STDC_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif
#define VC_GE_2005(version) (version >= 1400)

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mtp_argon2ref/mtp_core.h"
#include "mtp_argon2ref/mtp_thread.h"
#include "mtp_argon2ref/mtp_blake2.h"
#include "mtp_argon2ref/mtp_blake2-impl.h"

#ifdef GENKAT
#include "genkat.h"
#endif

#if defined(__clang__)
#if __has_attribute(optnone)
#define NOT_OPTIMIZED __attribute__((optnone))
#endif
#elif defined(__GNUC__)
#define GCC_VERSION                                                            \
    (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if GCC_VERSION >= 40400
#define NOT_OPTIMIZED __attribute__((optimize("O0")))
#endif
#endif
#ifndef NOT_OPTIMIZED
#define NOT_OPTIMIZED
#endif

/***************Instance and Position constructors**********/
void init_argon_block_value(argon_block *b, uint8_t in) { memset(b->v, in, sizeof(b->v)); }

void copy_argon_block(argon_block *dst, const argon_block *src) {
    memcpy(dst->v, src->v, sizeof(uint64_t) * MTP_ARGON2_QWORDS_IN_argon_block);
}

void xor_argon_block(argon_block *dst, const argon_block *src) {
    int i;
    for (i = 0; i < MTP_ARGON2_QWORDS_IN_argon_block; ++i) {
        dst->v[i] ^= src->v[i];
    }
}

static void load_argon_block(argon_block *dst, const void *input) {
    unsigned i;
    for (i = 0; i < MTP_ARGON2_QWORDS_IN_argon_block; ++i) {
        dst->v[i] = mtp_load64((const uint8_t *)input + i * sizeof(dst->v[i]));
    }
}

void store_argon_block(void *output, const argon_block *src) {
    unsigned i;
    for (i = 0; i < MTP_ARGON2_QWORDS_IN_argon_block; ++i) {
        mtp_store64((uint8_t *)output + i * sizeof(src->v[i]), src->v[i]);
    }
}

/***************Memory functions*****************/

int mtp_allocate_memory(const mtp_argon2_context *context, uint8_t **memory,
                    size_t num, size_t size) {
    size_t memory_size = num*size;
    if (memory == NULL) {
        return MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
    }

    /* 1. Check for multiplication overflow */
    if (size != 0 && memory_size / size != num) {
        return MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
    }

    /* 2. Try to allocate with appropriate allocator */
    if (context->allocate_cbk) {
        (context->allocate_cbk)(memory, memory_size);
    } else {
		*memory = (uint8_t*)malloc(128 * 8 * 2 * 4 * 2);
//        *memory = malloc(memory_size);
    }

    if (*memory == NULL) {
        return MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
    }

    return MTP_ARGON2_OK;
}

void mtp_free_memory(const mtp_argon2_context *context, uint8_t *memory,
                 size_t num, size_t size) {
    size_t memory_size = num*size;
    clear_internal_memory(memory, memory_size);
    if (context->free_cbk) {
        (context->free_cbk)(memory, memory_size);
    } else {
        free(memory);
    }
}

void NOT_OPTIMIZED mtp_secure_wipe_memory(void *v, size_t n) {
#if defined(_MSC_VER) && VC_GE_2005(_MSC_VER)
    SecureZeroMemory(v, n);
#elif defined memset_s
    memset_s(v, n, 0, n);
#elif defined(__OpenBSD__)
    explicit_bzero(v, n);
#else
    static void *(*const volatile memset_sec)(void *, int, size_t) = &memset;
    memset_sec(v, 0, n);
#endif
}

/* Memory clear flag defaults to true. */
int MTP_FLAG_clear_internal_memory = 1;
void mtp_clear_internal_memory(void *v, size_t n) {
  if (MTP_FLAG_clear_internal_memory && v) {
    mtp_secure_wipe_memory(v, n);
  }
}

void mtp_finalize(const mtp_argon2_context *context, mtp_argon2_instance_t *instance) {
    if (context != NULL && instance != NULL) {
        argon_block argon_blockhash;
        uint32_t l;

        copy_argon_block(&argon_blockhash, instance->memory + instance->lane_length - 1);

        /* XOR the last argon_blocks */
        for (l = 1; l < instance->lanes; ++l) {
            uint32_t last_argon_block_in_lane =
                l * instance->lane_length + (instance->lane_length - 1);
            xor_argon_block(&argon_blockhash, instance->memory + last_argon_block_in_lane);
        }

		
        /* Hash the result */
        {
            uint8_t argon_blockhash_bytes[MTP_ARGON2_argon_block_SIZE];
            store_argon_block(argon_blockhash_bytes, &argon_blockhash);
            amtp_blake2b_long(context->out, context->outlen, argon_blockhash_bytes,MTP_ARGON2_argon_block_SIZE);
            /* clear argon_blockhash and argon_blockhash_bytes */
            clear_internal_memory(argon_blockhash.v, MTP_ARGON2_argon_block_SIZE);
            clear_internal_memory(argon_blockhash_bytes, MTP_ARGON2_argon_block_SIZE);
        }

#ifdef GENKAT
        print_tag(context->out, context->outlen);
#endif

        mtp_free_memory(context, (uint8_t *)instance->memory,
                    instance->memory_argon_blocks, sizeof(argon_block));
    }
}

uint32_t mtp_index_alpha(const mtp_argon2_instance_t *instance,
                     const mtp_argon2_position_t *position, uint32_t pseudo_rand,
                     int same_lane) {
    /*
     * Pass 0:
     *      This lane : all already finished segments plus already constructed
     * argon_blocks in this segment
     *      Other lanes : all already finished segments
     * Pass 1+:
     *      This lane : (SYNC_POINTS - 1) last segments plus already constructed
     * argon_blocks in this segment
     *      Other lanes : (SYNC_POINTS - 1) last segments
     */
    uint32_t reference_area_size;
    uint64_t relative_position;
    uint32_t start_position, absolute_position;

    if (0 == position->pass) {
        /* First pass */
        if (0 == position->slice) {
            /* First slice */
            reference_area_size =
                position->index - 1; /* all but the previous */
        } else {
            if (same_lane) {
                /* The same lane => add current segment */
                reference_area_size =
                    position->slice * instance->segment_length +
                    position->index - 1;
            } else {
                reference_area_size =
                    position->slice * instance->segment_length +
                    ((position->index == 0) ? (-1) : 0);
            }
        }
    } else {
        /* Second pass */
        if (same_lane) {
            reference_area_size = instance->lane_length -
                                  instance->segment_length + position->index -
                                  1;
        } else {
            reference_area_size = instance->lane_length -
                                  instance->segment_length +
                                  ((position->index == 0) ? (-1) : 0);
        }
    }

    /* 1.2.4. Mapping pseudo_rand to 0..<reference_area_size-1> and produce
     * relative position */
    relative_position = pseudo_rand;
    relative_position = relative_position * relative_position >> 32;
    relative_position = reference_area_size - 1 -
                        (reference_area_size * relative_position >> 32);

    /* 1.2.5 Computing starting position */
    start_position = 0;

    if (0 != position->pass) {
        start_position = (position->slice == MTP_ARGON2_SYNC_POINTS - 1)
                             ? 0
                             : (position->slice + 1) * instance->segment_length;
    }

    /* 1.2.6. Computing absolute position */
    absolute_position = (start_position + relative_position) %
                        instance->lane_length; /* absolute position */
    return absolute_position;
}

/* Single-threaded version for p=1 case */
static int mtp_fill_memory_argon_blocks_st(mtp_argon2_instance_t *instance) {
    uint32_t r, s, l;

    for (r = 0; r < instance->passes; ++r) {
        for (s = 0; s < MTP_ARGON2_SYNC_POINTS; ++s) {
            for (l = 0; l< instance->lanes; ++l) {
                mtp_argon2_position_t position = {r, l, (uint8_t)s, 0};
				
                mtp_fill_segment(instance, position);
            }
        }
#ifdef GENKAT
        internal_kat(instance, r); /* Print all memory argon_blocks */
#endif
    }
    return MTP_ARGON2_OK;
}

#if !defined(MTP_ARGON2_NO_THREADS)

#ifdef _WIN32
static unsigned __stdcall mtp_fill_segment_thr(void *thread_data)
#else
static void *mtp_fill_segment_thr(void *thread_data)
#endif
{
    mtp_argon2_thread_data *my_data = (mtp_argon2_thread_data *)thread_data;
    mtp_fill_segment(my_data->instance_ptr, my_data->pos);
    mtp_argon2_thread_exit();
    return 0;
}

/* Multi-threaded version for p > 1 case */
static int mtp_fill_memory_argon_blocks_mt(mtp_argon2_instance_t *instance) {
    uint32_t r, s;
    mtp_argon2_thread_handle_t *thread = NULL;
    mtp_argon2_thread_data *thr_data = NULL;
    int rc = MTP_ARGON2_OK;

    /* 1. Allocating space for threads */
    thread = (mtp_argon2_thread_handle_t *)calloc(instance->lanes, sizeof(mtp_argon2_thread_handle_t));
    if (thread == NULL) {
        rc = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
        goto fail;
    }

    thr_data = (mtp_argon2_thread_data *)calloc(instance->lanes, sizeof(mtp_argon2_thread_data));
    if (thr_data == NULL) {
        rc = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
        goto fail;
    }

    for (r = 0; r < instance->passes; ++r) {
        for (s = 0; s < MTP_ARGON2_SYNC_POINTS; ++s) {
            uint32_t l;

            /* 2. Calling threads */
            for (l = 0; l < instance->lanes; ++l) {
                mtp_argon2_position_t position;

                /* 2.1 Join a thread if limit is exceeded */
                if (l >= instance->threads) {
                    if (mtp_argon2_thread_join(thread[l - instance->threads])) {
                        rc = MTP_ARGON2_THREAD_FAIL;
                        goto fail;
                    }
                }

                /* 2.2 Create thread */
                position.pass = r;
                position.lane = l;
                position.slice = (uint8_t)s;
                position.index = 0;
                thr_data[l].instance_ptr =
                    instance; /* preparing the thread input */
                memcpy(&(thr_data[l].pos), &position,
                       sizeof(mtp_argon2_position_t));
                if (mtp_argon2_thread_create(&thread[l], &mtp_fill_segment_thr,
                                         (void *)&thr_data[l])) {
                    rc = MTP_ARGON2_THREAD_FAIL;
                    goto fail;
                }

                /* mtp_fill_segment(instance, position); */
                /*Non-thread equivalent of the lines above */
            }

            /* 3. Joining remaining threads */
            for (l = instance->lanes - instance->threads; l < instance->lanes;
                 ++l) {
                if (mtp_argon2_thread_join(thread[l])) {
                    rc = MTP_ARGON2_THREAD_FAIL;
                    goto fail;
                }
            }
        }

#ifdef GENKAT
        internal_kat(instance, r); /* Print all memory argon_blocks */
#endif
    }

fail:
    if (thread != NULL) {
        free(thread);
    }
    if (thr_data != NULL) {
        free(thr_data);
    }
    return rc;
}

#endif /* MTP_ARGON2_NO_THREADS */

int mtp_fill_memory_argon_blocks(mtp_argon2_instance_t *instance) {
	if (instance == NULL || instance->lanes == 0) {
	    return MTP_ARGON2_INCORRECT_PARAMETER;
    }

#if defined(MTP_ARGON2_NO_THREADS)
    return mtp_fill_memory_argon_blocks_st(instance);
#else
    return instance->threads == 1 ?
			mtp_fill_memory_argon_blocks_st(instance) : mtp_fill_memory_argon_blocks_mt(instance);
#endif
}

int mtp_fill_memory_argon_blocks_mtp(mtp_argon2_instance_t *instance) {
	uint32_t r, s;
	mtp_argon2_thread_handle_t *thread = NULL;
	mtp_argon2_thread_data *thr_data = NULL;
	int rc = MTP_ARGON2_OK;

	if (instance == NULL || instance->lanes == 0) {
		rc = MTP_ARGON2_THREAD_FAIL;
		goto fail;
	}

	/* 1. Allocating space for threads */
	thread = (mtp_argon2_thread_handle_t *)calloc(instance->lanes, sizeof(mtp_argon2_thread_handle_t));
	if (thread == NULL) {
		rc = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
		goto fail;
	}

	thr_data = (mtp_argon2_thread_data *)calloc(instance->lanes, sizeof(mtp_argon2_thread_data));
	if (thr_data == NULL) {
		rc = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
		goto fail;
	}

	for (r = 0; r < instance->passes; ++r) {
		for (s = 0; s < MTP_ARGON2_SYNC_POINTS; ++s) {
			uint32_t l;

			/* 2. Calling threads */
			for (l = 0; l < instance->lanes; ++l) {
				mtp_argon2_position_t position;

				/* 2.1 Join a thread if limit is exceeded */
				if (l >= instance->threads) {
					if (mtp_argon2_thread_join(thread[l - instance->threads])) {
						rc = MTP_ARGON2_THREAD_FAIL;
						goto fail;
					}
				}

				/* 2.2 Create thread */
				position.pass = r;
				position.lane = l;
				position.slice = (uint8_t)s;
				position.index = 0;
				thr_data[l].instance_ptr = instance; /* preparing the thread input */
				memcpy(&(thr_data[l].pos), &position, sizeof(mtp_argon2_position_t));
				if (mtp_argon2_thread_create(&thread[l], &mtp_fill_segment_thr, (void *)&thr_data[l])) {
					rc = MTP_ARGON2_THREAD_FAIL;
					goto fail;
				}

				/* mtp_fill_segment(instance, position); */
				/*Non-thread equivalent of the lines above */
			}

			/* 3. Joining remaining threads */
			for (l = instance->lanes - instance->threads; l < instance->lanes; ++l) {
				if (mtp_argon2_thread_join(thread[l])) {
					rc = MTP_ARGON2_THREAD_FAIL;
					goto fail;
				}
			}
		}
	}
	// fail to fill argon_blocks with mtp_argon2d
fail:
	if (thread != NULL) {
		free(thread);
	}
	if (thr_data != NULL) {
		free(thr_data);
	}
	return rc;
}

int mtp_validate_inputs(const mtp_argon2_context *context) {
    if (NULL == context) {
        return MTP_ARGON2_INCORRECT_PARAMETER;
    }

    if (NULL == context->out) {
        return MTP_ARGON2_OUTPUT_PTR_NULL;
    }

    /* Validate output length */
    if (MTP_ARGON2_MIN_OUTLEN > context->outlen) {
        return MTP_ARGON2_OUTPUT_TOO_SHORT;
    }

    if (MTP_ARGON2_MAX_OUTLEN < context->outlen) {
        return MTP_ARGON2_OUTPUT_TOO_LONG;
    }

    /* Validate password (required param) */
    if (NULL == context->pwd) {
        if (0 != context->pwdlen) {
            return MTP_ARGON2_PWD_PTR_MISMATCH;
        }
    }

    if (MTP_ARGON2_MIN_PWD_LENGTH > context->pwdlen) {
      return MTP_ARGON2_PWD_TOO_SHORT;
    }

    if (MTP_ARGON2_MAX_PWD_LENGTH < context->pwdlen) {
        return MTP_ARGON2_PWD_TOO_LONG;
    }

    /* Validate salt (required param) */
    if (NULL == context->salt) {
        if (0 != context->saltlen) {
            return MTP_ARGON2_SALT_PTR_MISMATCH;
        }
    }

    if (MTP_ARGON2_MIN_SALT_LENGTH > context->saltlen) {
        return MTP_ARGON2_SALT_TOO_SHORT;
    }

    if (MTP_ARGON2_MAX_SALT_LENGTH < context->saltlen) {
        return MTP_ARGON2_SALT_TOO_LONG;
    }

    /* Validate secret (optional param) */
    if (NULL == context->secret) {
        if (0 != context->secretlen) {
            return MTP_ARGON2_SECRET_PTR_MISMATCH;
        }
    } else {
        if (MTP_ARGON2_MIN_SECRET > context->secretlen) {
            return MTP_ARGON2_SECRET_TOO_SHORT;
        }
        if (MTP_ARGON2_MAX_SECRET < context->secretlen) {
            return MTP_ARGON2_SECRET_TOO_LONG;
        }
    }

    /* Validate associated data (optional param) */
    if (NULL == context->ad) {
        if (0 != context->adlen) {
            return MTP_ARGON2_AD_PTR_MISMATCH;
        }
    } else {
        if (MTP_ARGON2_MIN_AD_LENGTH > context->adlen) {
            return MTP_ARGON2_AD_TOO_SHORT;
        }
        if (MTP_ARGON2_MAX_AD_LENGTH < context->adlen) {
            return MTP_ARGON2_AD_TOO_LONG;
        }
    }

    /* Validate memory cost */
    if (MTP_ARGON2_MIN_MEMORY > context->m_cost) {
        return MTP_ARGON2_MEMORY_TOO_LITTLE;
    }

    if (MTP_ARGON2_MAX_MEMORY < context->m_cost) {
        return MTP_ARGON2_MEMORY_TOO_MUCH;
    }

    if (context->m_cost < 8 * context->lanes) {
        return MTP_ARGON2_MEMORY_TOO_LITTLE;
    }

    /* Validate time cost */
    if (MTP_ARGON2_MIN_TIME > context->t_cost) {
        return MTP_ARGON2_TIME_TOO_SMALL;
    }

    if (MTP_ARGON2_MAX_TIME < context->t_cost) {
        return MTP_ARGON2_TIME_TOO_LARGE;
    }

    /* Validate lanes */
    if (MTP_ARGON2_MIN_LANES > context->lanes) {
        return MTP_ARGON2_LANES_TOO_FEW;
    }

    if (MTP_ARGON2_MAX_LANES < context->lanes) {
        return MTP_ARGON2_LANES_TOO_MANY;
    }

    /* Validate threads */
    if (MTP_ARGON2_MIN_THREADS > context->threads) {
        return MTP_ARGON2_THREADS_TOO_FEW;
    }

    if (MTP_ARGON2_MAX_THREADS < context->threads) {
        return MTP_ARGON2_THREADS_TOO_MANY;
    }

    if (NULL != context->allocate_cbk && NULL == context->free_cbk) {
        return MTP_ARGON2_FREE_MEMORY_CBK_NULL;
    }

    if (NULL == context->allocate_cbk && NULL != context->free_cbk) {
        return MTP_ARGON2_ALLOCATE_MEMORY_CBK_NULL;
    }

    return MTP_ARGON2_OK;
}

void mtp_fill_first_argon_blocks(uint8_t *argon_blockhash, const mtp_argon2_instance_t *instance) {
    uint32_t l;
    /* Make the first and second argon_block in each lane as G(H0||0||i) or
       G(H0||1||i) */
    uint8_t argon_blockhash_bytes[MTP_ARGON2_argon_block_SIZE];
    for (l = 0; l < instance->lanes; ++l) {

        mtp_store32(argon_blockhash + MTP_ARGON2_PREHASH_DIGEST_LENGTH, 0);
        mtp_store32(argon_blockhash + MTP_ARGON2_PREHASH_DIGEST_LENGTH + 4, l);

        amtp_blake2b_long(argon_blockhash_bytes, MTP_ARGON2_argon_block_SIZE, argon_blockhash,
                     MTP_ARGON2_PREHASH_SEED_LENGTH);

    //    load_argon_block(&instance->memory[l * instance->lane_length + 0],
    //               argon_blockhash_bytes);

		load_argon_block(&instance->memory[l * 2 + 0],
			argon_blockhash_bytes);

        mtp_store32(argon_blockhash + MTP_ARGON2_PREHASH_DIGEST_LENGTH, 1);


        amtp_blake2b_long(argon_blockhash_bytes, MTP_ARGON2_argon_block_SIZE, argon_blockhash,
                     MTP_ARGON2_PREHASH_SEED_LENGTH);
//        load_argon_block(&instance->memory[l * instance->lane_length + 1],
//                   argon_blockhash_bytes);
		load_argon_block(&instance->memory[l * 2 + 1],
			argon_blockhash_bytes);
    }
    clear_internal_memory(argon_blockhash_bytes, MTP_ARGON2_argon_block_SIZE);

}

void mtp_initial_hash(uint8_t *argon_blockhash, mtp_argon2_context *context,
                  mtp_argon2_type type) {
    amtp_blake2b_state BlakeHash;
    uint8_t value[sizeof(uint32_t)];

    if (NULL == context || NULL == argon_blockhash) {
        return;
    }

    amtp_blake2b_init(&BlakeHash, MTP_ARGON2_PREHASH_DIGEST_LENGTH);

    mtp_store32(&value, context->lanes);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, context->outlen);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, context->m_cost);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, context->t_cost);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, context->version);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, (uint32_t)type);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    mtp_store32(&value, context->pwdlen);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    if (context->pwd != NULL) {
        amtp_blake2b_update(&BlakeHash, (const uint8_t *)context->pwd,
                       context->pwdlen);

        if (context->flags & MTP_ARGON2_FLAG_CLEAR_PASSWORD) {
            mtp_secure_wipe_memory(context->pwd, context->pwdlen);
            context->pwdlen = 0;
        }
    }



    mtp_store32(&value, context->saltlen);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    if (context->salt != NULL) {
        amtp_blake2b_update(&BlakeHash, (const uint8_t *)context->salt,
                       context->saltlen);
    }


    mtp_store32(&value, context->secretlen);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    if (context->secret != NULL) {
        amtp_blake2b_update(&BlakeHash, (const uint8_t *)context->secret,
                       context->secretlen);

        if (context->flags & MTP_ARGON2_FLAG_CLEAR_SECRET) {
            mtp_secure_wipe_memory(context->secret, context->secretlen);
            context->secretlen = 0;
        }
    }

    mtp_store32(&value, context->adlen);
    amtp_blake2b_update(&BlakeHash, (const uint8_t *)&value, sizeof(value));

    if (context->ad != NULL) {
        amtp_blake2b_update(&BlakeHash, (const uint8_t *)context->ad,
                       context->adlen);
    }

    amtp_blake2b_final(&BlakeHash, argon_blockhash, MTP_ARGON2_PREHASH_DIGEST_LENGTH);

}

int mtp_initialize(mtp_argon2_instance_t *instance, mtp_argon2_context *context) {
    uint8_t argon_blockhash[MTP_ARGON2_PREHASH_SEED_LENGTH];
    int result = MTP_ARGON2_OK;

    if (instance == NULL || context == NULL)
        return MTP_ARGON2_INCORRECT_PARAMETER;
    instance->context_ptr = context;

    /* 1. Memory allocation */
    result = mtp_allocate_memory(context, (uint8_t **)&(instance->memory),
                             instance->memory_argon_blocks, sizeof(argon_block));
    if (result != MTP_ARGON2_OK) {
        return result;
    }

    /* 2. Initial hashing */
    /* H_0 + 8 extra bytes to produce the first argon_blocks */
    /* uint8_t argon_blockhash[MTP_ARGON2_PREHASH_SEED_LENGTH]; */
    /* Hashing all inputs */
    mtp_initial_hash(argon_blockhash, context, instance->type);
    /* Zeroing 8 extra bytes */
    clear_internal_memory(argon_blockhash + MTP_ARGON2_PREHASH_DIGEST_LENGTH,
                          MTP_ARGON2_PREHASH_SEED_LENGTH -
                              MTP_ARGON2_PREHASH_DIGEST_LENGTH);

	for (int i = 0; i < MTP_ARGON2_PREHASH_SEED_LENGTH/4; i++) 
		instance->argon_block_header[i] = ((uint32_t*)argon_blockhash)[i];

#ifdef GENKAT
    initial_kat(argon_blockhash, context, instance->type);
#endif

    /* 3. Creating first argon_blocks, we always have at least two argon_blocks in a slice
     */
    mtp_fill_first_argon_blocks(argon_blockhash, instance);
    /* Clearing the hash */
    clear_internal_memory(argon_blockhash, MTP_ARGON2_PREHASH_SEED_LENGTH);

    return MTP_ARGON2_OK;
}
