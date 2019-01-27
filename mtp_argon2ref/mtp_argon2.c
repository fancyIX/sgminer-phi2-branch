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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
//#include "merkletree\sha.h"
//#include "merkletree\merkletree.h"
#include "mtp_argon2ref/mtp_argon2.h"
#include "mtp_argon2ref/mtp_encoding.h"
#include "mtp_argon2ref/mtp_core.h"

const char *mtp_argon2_type2string(mtp_argon2_type type, int uppercase) {
    switch (type) {
        case MTPArgon2_d:
            return uppercase ? "MTPArgon2d" : "mtp_argon2d";
        case MTPArgon2_i:
            return uppercase ? "MTPArgon2i" : "mtp_argon2i";
        case MTPArgon2_id:
            return uppercase ? "MTPArgon2id" : "mtp_argon2id";
    }

    return NULL;
}

int mtp_argon2_ctx(mtp_argon2_context *context, mtp_argon2_type type) {
    /* 1. Validate all inputs */
    int result = mtp_validate_inputs(context);
    uint32_t memory_argon_blocks, segment_length;
    mtp_argon2_instance_t instance;

    if (MTP_ARGON2_OK != result) {
        return result;
    }

    if (MTPArgon2_d != type && MTPArgon2_i != type && MTPArgon2_id != type) {
        return MTP_ARGON2_INCORRECT_TYPE;
    }

    /* 2. Align memory size */
    /* Minimum memory_argon_blocks = 8L argon_blocks, where L is the number of lanes */
    memory_argon_blocks = context->m_cost;

    if (memory_argon_blocks < 2 * MTP_ARGON2_SYNC_POINTS * context->lanes) {
        memory_argon_blocks = 2 * MTP_ARGON2_SYNC_POINTS * context->lanes;
    }

    segment_length = memory_argon_blocks / (context->lanes * MTP_ARGON2_SYNC_POINTS);
    /* Ensure that all segments have equal length */
    memory_argon_blocks = segment_length * (context->lanes * MTP_ARGON2_SYNC_POINTS);

    instance.version = context->version;
    instance.memory = NULL;
    instance.passes = context->t_cost;
    instance.memory_argon_blocks = memory_argon_blocks;
    instance.segment_length = segment_length;
    instance.lane_length = segment_length * MTP_ARGON2_SYNC_POINTS;
    instance.lanes = context->lanes;
    instance.threads = context->threads;
    instance.type = type;

    /* 3. Initialization: Hashing inputs, allocating memory, filling first
     * argon_blocks
     */
    result = mtp_initialize(&instance, context);
 
    if (MTP_ARGON2_OK != result) {
        return result;
    }

    /* 4. Filling memory */
    result = mtp_fill_memory_argon_blocks(&instance);
 
    if (MTP_ARGON2_OK != result) {
        return result;
    }
    /* 5. Finalization */
	mtp_finalize(context, &instance);
 
    return MTP_ARGON2_OK;
}

int mtp_argon2_ctx_from_mtp(mtp_argon2_context *context, mtp_argon2_instance_t *instance) {

//	printf("1. Validate all inputs \n");
	/* 1. Validate all inputs */
	int result = mtp_validate_inputs(context);
	uint32_t memory_argon_blocks, segment_length;
	//mtp_argon2_instance_t instance;

	if (MTP_ARGON2_OK != result) {
		return result;
	}

//	printf("2. Align memory size \n");
	/* 2. Align memory size */
	/* Minimum memory_argon_blocks = 8L argon_blocks, where L is the number of lanes */
	memory_argon_blocks = context->m_cost;

	if (memory_argon_blocks < 2 * MTP_ARGON2_SYNC_POINTS * context->lanes) {
		memory_argon_blocks = 2 * MTP_ARGON2_SYNC_POINTS * context->lanes;
	}

	segment_length = memory_argon_blocks / (context->lanes * MTP_ARGON2_SYNC_POINTS);
	/* Ensure that all segments have equal length */
	memory_argon_blocks = segment_length * (context->lanes * MTP_ARGON2_SYNC_POINTS);

	instance->version = context->version;
	instance->memory = NULL;
	instance->passes = context->t_cost;
	instance->memory_argon_blocks = memory_argon_blocks;
	instance->segment_length = segment_length;
	instance->lane_length = segment_length * MTP_ARGON2_SYNC_POINTS;
	instance->lanes = context->lanes;
	instance->threads = context->threads;
	instance->type = MTPArgon2_d;

//	printf("3. Initializatio n: Hashing inputs, allocating memory, filling first argon_blocks\n");
	/* 3. Initialization: Hashing inputs, allocating memory, filling first argon_blocks */
	result = mtp_initialize(instance, context);

	if (MTP_ARGON2_OK != result) {
		printf("error code = %d\n", result);
		return result;
	}


	/* 4. Filling memory */
/*
	result = fill_memory_argon_blocks_mtp(instance);

	if (MTP_ARGON2_OK != result) {
		return result;
	}
*/
	/* 5. Finalization */
	//finalize(context, &instance);

	return MTP_ARGON2_OK;
}




int mtp_argon2_hash(const uint32_t t_cost, const uint32_t m_cost,
                const uint32_t parallelism, const void *pwd,
                const size_t pwdlen, const void *salt, const size_t saltlen,
                void *hash, const size_t hashlen, char *encoded,
                const size_t encodedlen, mtp_argon2_type type,
                const uint32_t version){

    mtp_argon2_context context;
    int result;
    uint8_t *out;

    if (pwdlen > MTP_ARGON2_MAX_PWD_LENGTH) {
        return MTP_ARGON2_PWD_TOO_LONG;
    }

    if (saltlen > MTP_ARGON2_MAX_SALT_LENGTH) {
        return MTP_ARGON2_SALT_TOO_LONG;
    }

    if (hashlen > MTP_ARGON2_MAX_OUTLEN) {
        return MTP_ARGON2_OUTPUT_TOO_LONG;
    }

    if (hashlen < MTP_ARGON2_MIN_OUTLEN) {
        return MTP_ARGON2_OUTPUT_TOO_SHORT;
    }

    out = (uint8_t*)malloc(hashlen);
    if (!out) {
        return MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
    }

    context.out = (uint8_t *)out;
    context.outlen = (uint32_t)hashlen;
    context.pwd = CONST_CAST(uint8_t *)pwd;
    context.pwdlen = (uint32_t)pwdlen;
    context.salt = CONST_CAST(uint8_t *)salt;
    context.saltlen = (uint32_t)saltlen;
    context.secret = NULL;
    context.secretlen = 0;
    context.ad = NULL;
    context.adlen = 0;
    context.t_cost = t_cost;
    context.m_cost = m_cost;
    context.lanes = parallelism;
    context.threads = parallelism;
    context.allocate_cbk = NULL;
    context.free_cbk = NULL;
    context.flags = MTP_ARGON2_DEFAULT_FLAGS;
    context.version = version;

    result = mtp_argon2_ctx(&context, type);

    if (result != MTP_ARGON2_OK) {
        mtp_clear_internal_memory(out, hashlen);
        free(out);
        return result;
    }

    /* if raw hash requested, write it */
    if (hash) {
        memcpy(hash, out, hashlen);
    }

    /* if encoding requested, write it */
    if (encoded && encodedlen) {
        if (mtp_encode_string(encoded, encodedlen, &context, type) != MTP_ARGON2_OK) {
            mtp_clear_internal_memory(out, hashlen); /* wipe buffers if error */
            mtp_clear_internal_memory(encoded, encodedlen);
            free(out);
            return MTP_ARGON2_ENCODING_FAIL;
        }
    }
    mtp_clear_internal_memory(out, hashlen);
    free(out);

    return MTP_ARGON2_OK;
}

int mtp_argon2i_hash_encoded(const uint32_t t_cost, const uint32_t m_cost,
                         const uint32_t parallelism, const void *pwd,
                         const size_t pwdlen, const void *salt,
                         const size_t saltlen, const size_t hashlen,
                         char *encoded, const size_t encodedlen) {

    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       NULL, hashlen, encoded, encodedlen, MTPArgon2_i,
                       MTP_ARGON2_VERSION_NUMBER);
}

int mtp_argon2i_hash_raw(const uint32_t t_cost, const uint32_t m_cost,
                     const uint32_t parallelism, const void *pwd,
                     const size_t pwdlen, const void *salt,
                     const size_t saltlen, void *hash, const size_t hashlen) {

    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       hash, hashlen, NULL, 0, MTPArgon2_i, MTP_ARGON2_VERSION_NUMBER);
}

int mtp_argon2d_hash_encoded(const uint32_t t_cost, const uint32_t m_cost,
                         const uint32_t parallelism, const void *pwd,
                         const size_t pwdlen, const void *salt,
                         const size_t saltlen, const size_t hashlen,
                         char *encoded, const size_t encodedlen) {

    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       NULL, hashlen, encoded, encodedlen, MTPArgon2_d,
                       MTP_ARGON2_VERSION_NUMBER);
}

int mtp_argon2d_hash_raw(const uint32_t t_cost, const uint32_t m_cost,
                     const uint32_t parallelism, const void *pwd,
                     const size_t pwdlen, const void *salt,
                     const size_t saltlen, void *hash, const size_t hashlen) {

    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       hash, hashlen, NULL, 0, MTPArgon2_d, MTP_ARGON2_VERSION_NUMBER);
}

int mtp_argon2id_hash_encoded(const uint32_t t_cost, const uint32_t m_cost,
                          const uint32_t parallelism, const void *pwd,
                          const size_t pwdlen, const void *salt,
                          const size_t saltlen, const size_t hashlen,
                          char *encoded, const size_t encodedlen) {

    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       NULL, hashlen, encoded, encodedlen, MTPArgon2_id,
                       MTP_ARGON2_VERSION_NUMBER);
}

int mtp_argon2id_hash_raw(const uint32_t t_cost, const uint32_t m_cost,
                      const uint32_t parallelism, const void *pwd,
                      const size_t pwdlen, const void *salt,
                      const size_t saltlen, void *hash, const size_t hashlen) {
    return mtp_argon2_hash(t_cost, m_cost, parallelism, pwd, pwdlen, salt, saltlen,
                       hash, hashlen, NULL, 0, MTPArgon2_id,
                       MTP_ARGON2_VERSION_NUMBER);
}

static int mtp_argon2_compare(const uint8_t *b1, const uint8_t *b2, size_t len) {
    size_t i;
    uint8_t d = 0U;

    for (i = 0U; i < len; i++) {
        d |= b1[i] ^ b2[i];
    }
    return (int)((1 & ((d - 1) >> 8)) - 1);
}

int mtp_argon2_verify(const char *encoded, const void *pwd, const size_t pwdlen,
                  mtp_argon2_type type) {

    mtp_argon2_context ctx;
    uint8_t *desired_result = NULL;

    int ret = MTP_ARGON2_OK;

    size_t encoded_len;
    uint32_t max_field_len;

    if (pwdlen > MTP_ARGON2_MAX_PWD_LENGTH) {
        return MTP_ARGON2_PWD_TOO_LONG;
    }

    if (encoded == NULL) {
        return MTP_ARGON2_DECODING_FAIL;
    }

    encoded_len = strlen(encoded);
    if (encoded_len > UINT32_MAX) {
        return MTP_ARGON2_DECODING_FAIL;
    }

    /* No field can be longer than the encoded length */
    max_field_len = (uint32_t)encoded_len;

    ctx.saltlen = max_field_len;
    ctx.outlen = max_field_len;

    ctx.salt = (uint8_t*)malloc(ctx.saltlen);
    ctx.out = (uint8_t*)malloc(ctx.outlen);
    if (!ctx.salt || !ctx.out) {
        ret = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
        goto fail;
    }

    ctx.pwd = (uint8_t *)pwd;
    ctx.pwdlen = (uint32_t)pwdlen;

    ret = mtp_decode_string(&ctx, encoded, type);
    if (ret != MTP_ARGON2_OK) {
        goto fail;
    }

    /* Set aside the desired result, and get a new buffer. */
    desired_result = ctx.out;
    ctx.out = (uint8_t*)malloc(ctx.outlen);
    if (!ctx.out) {
        ret = MTP_ARGON2_MEMORY_ALLOCATION_ERROR;
        goto fail;
    }

    ret = mtp_argon2_verify_ctx(&ctx, (char *)desired_result, type);
    if (ret != MTP_ARGON2_OK) {
        goto fail;
    }

fail:
    free(ctx.salt);
    free(ctx.out);
    free(desired_result);

    return ret;
}

int mtp_argon2i_verify(const char *encoded, const void *pwd, const size_t pwdlen) {

    return mtp_argon2_verify(encoded, pwd, pwdlen, MTPArgon2_i);
}

int mtp_argon2d_verify(const char *encoded, const void *pwd, const size_t pwdlen) {

    return mtp_argon2_verify(encoded, pwd, pwdlen, MTPArgon2_d);
}

int mtp_argon2id_verify(const char *encoded, const void *pwd, const size_t pwdlen) {

    return mtp_argon2_verify(encoded, pwd, pwdlen, MTPArgon2_id);
}

int mtp_argon2d_ctx(mtp_argon2_context *context) {
    return mtp_argon2_ctx(context, MTPArgon2_d);
}
/*
int mtp_argon2d_mtp_ctx(mtp_argon2_context *context) {
	return mtp_argon2_mtp_ctx(context, MTPArgon2_d);
}
*/

int mtp_argon2i_ctx(mtp_argon2_context *context) {
    return mtp_argon2_ctx(context, MTPArgon2_i);
}

int mtp_argon2id_ctx(mtp_argon2_context *context) {
    return mtp_argon2_ctx(context, MTPArgon2_id);
}

int mtp_argon2_verify_ctx(mtp_argon2_context *context, const char *hash,
                      mtp_argon2_type type) {
    int ret = mtp_argon2_ctx(context, type);
    if (ret != MTP_ARGON2_OK) {
        return ret;
    }

    if (mtp_argon2_compare((uint8_t *)hash, context->out, context->outlen)) {
        return MTP_ARGON2_VERIFY_MISMATCH;
    }

    return MTP_ARGON2_OK;
}

int mtp_argon2d_verify_ctx(mtp_argon2_context *context, const char *hash) {
    return mtp_argon2_verify_ctx(context, hash, MTPArgon2_d);
}

int mtp_argon2i_verify_ctx(mtp_argon2_context *context, const char *hash) {
    return mtp_argon2_verify_ctx(context, hash, MTPArgon2_i);
}

int mtp_argon2id_verify_ctx(mtp_argon2_context *context, const char *hash) {
    return mtp_argon2_verify_ctx(context, hash, MTPArgon2_id);
}

const char *mtp_argon2_error_message(int error_code) {
    switch (error_code) {
    case MTP_ARGON2_OK:
        return "OK";
    case MTP_ARGON2_OUTPUT_PTR_NULL:
        return "Output pointer is NULL";
    case MTP_ARGON2_OUTPUT_TOO_SHORT:
        return "Output is too short";
    case MTP_ARGON2_OUTPUT_TOO_LONG:
        return "Output is too long";
    case MTP_ARGON2_PWD_TOO_SHORT:
        return "Password is too short";
    case MTP_ARGON2_PWD_TOO_LONG:
        return "Password is too long";
    case MTP_ARGON2_SALT_TOO_SHORT:
        return "Salt is too short";
    case MTP_ARGON2_SALT_TOO_LONG:
        return "Salt is too long";
    case MTP_ARGON2_AD_TOO_SHORT:
        return "Associated data is too short";
    case MTP_ARGON2_AD_TOO_LONG:
        return "Associated data is too long";
    case MTP_ARGON2_SECRET_TOO_SHORT:
        return "Secret is too short";
    case MTP_ARGON2_SECRET_TOO_LONG:
        return "Secret is too long";
    case MTP_ARGON2_TIME_TOO_SMALL:
        return "Time cost is too small";
    case MTP_ARGON2_TIME_TOO_LARGE:
        return "Time cost is too large";
    case MTP_ARGON2_MEMORY_TOO_LITTLE:
        return "Memory cost is too small";
    case MTP_ARGON2_MEMORY_TOO_MUCH:
        return "Memory cost is too large";
    case MTP_ARGON2_LANES_TOO_FEW:
        return "Too few lanes";
    case MTP_ARGON2_LANES_TOO_MANY:
        return "Too many lanes";
    case MTP_ARGON2_PWD_PTR_MISMATCH:
        return "Password pointer is NULL, but password length is not 0";
    case MTP_ARGON2_SALT_PTR_MISMATCH:
        return "Salt pointer is NULL, but salt length is not 0";
    case MTP_ARGON2_SECRET_PTR_MISMATCH:
        return "Secret pointer is NULL, but secret length is not 0";
    case MTP_ARGON2_AD_PTR_MISMATCH:
        return "Associated data pointer is NULL, but ad length is not 0";
    case MTP_ARGON2_MEMORY_ALLOCATION_ERROR:
        return "Memory allocation error";
    case MTP_ARGON2_FREE_MEMORY_CBK_NULL:
        return "The free memory callback is NULL";
    case MTP_ARGON2_ALLOCATE_MEMORY_CBK_NULL:
        return "The allocate memory callback is NULL";
    case MTP_ARGON2_INCORRECT_PARAMETER:
        return "MTPArgon2_Context context is NULL";
    case MTP_ARGON2_INCORRECT_TYPE:
        return "There is no such version of MTPArgon2";
    case MTP_ARGON2_OUT_PTR_MISMATCH:
        return "Output pointer mismatch";
    case MTP_ARGON2_THREADS_TOO_FEW:
        return "Not enough threads";
    case MTP_ARGON2_THREADS_TOO_MANY:
        return "Too many threads";
    case MTP_ARGON2_MISSING_ARGS:
        return "Missing arguments";
    case MTP_ARGON2_ENCODING_FAIL:
        return "Encoding failed";
    case MTP_ARGON2_DECODING_FAIL:
        return "Decoding failed";
    case MTP_ARGON2_THREAD_FAIL:
        return "Threading failure";
    case MTP_ARGON2_DECODING_LENGTH_FAIL:
        return "Some of encoded parameters are too long or too short";
    case MTP_ARGON2_VERIFY_MISMATCH:
        return "The password does not match the supplied hash";
    default:
        return "Unknown error code";
    }
}

size_t mtp_argon2_encodedlen(uint32_t t_cost, uint32_t m_cost, uint32_t parallelism,
                         uint32_t saltlen, uint32_t hashlen, mtp_argon2_type type) {
  return strlen("$$v=$m=,t=,p=$$") + strlen(mtp_argon2_type2string(type, 0)) +
         mtp_numlen(t_cost) + mtp_numlen(m_cost) + mtp_numlen(parallelism) +
         mtp_b64len(saltlen) + mtp_b64len(hashlen) + mtp_numlen(MTP_ARGON2_VERSION_NUMBER) + 1;
}
