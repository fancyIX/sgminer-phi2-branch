/*
 * Copyright 2014 sgminer developers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or (at
 * your option) any later version.  See COPYING for more details.
 */

#include "algorithm.h"
#include "sph/sph_sha2.h"
#include "sph/sph_cubehash.h"
#include "ocl.h"
#include "ocl/build_kernel.h"

#include "algorithm/scrypt.h"
#include "algorithm/animecoin.h"
#include "algorithm/inkcoin.h"
#include "algorithm/quarkcoin.h"
#include "algorithm/qubitcoin.h"
#include "algorithm/sifcoin.h"
#include "algorithm/darkcoin.h"
#include "algorithm/myriadcoin-groestl.h"
#include "algorithm/fuguecoin.h"
#include "algorithm/groestlcoin.h"
#include "algorithm/twecoin.h"
#include "algorithm/marucoin.h"
#include "algorithm/maxcoin.h"
#include "algorithm/talkcoin.h"
#include "algorithm/bitblock.h"
#include "algorithm/x14.h"
#include "algorithm/fresh.h"
#include "algorithm/whirlcoin.h"
#include "algorithm/neoscrypt.h"
#include "algorithm/whirlpoolx.h"
#include "algorithm/lyra2re.h"
#include "algorithm/lyra2rev2.h"
#include "algorithm/lyra2rev3.h"
#include "algorithm/lyra2Z.h"
#include "algorithm/lyra2Zz.h"
#include "algorithm/pluck.h"
#include "algorithm/yescrypt.h"
#include "algorithm/credits.h"
#include "algorithm/blake256.h"
#include "algorithm/blakecoin.h"
#include "algorithm/ethash.h"
#include "algorithm/sia.h"
#include "algorithm/decred.h"
#include "algorithm/pascal.h"
#include "algorithm/lbry.h"
#include "algorithm/sibcoin.h"
#include "algorithm/phi.h"
#include "algorithm/phi2.h"
#include "algorithm/allium.h"
#include "algorithm/lyra2h.h"
#include "algorithm/x22i.h"
#include "algorithm/x25x.h"
#include "algorithm/argon2d/argon2d.h"
#include "algorithm/mtp_algo.h"

#include "compat.h"

#include <inttypes.h>
#include <string.h>

const char *algorithm_type_str[] = {
  "mtp",
  "mtp_vega",
  "Unknown",
  "Allium",
  "Credits",
  "Scrypt",
  "NScrypt",
  "Pascal",
  "X11",
  "X13",
  "X14",
  "X15",
  "X22i"
  "X25x"
  "Keccak",
  "Quarkcoin",
  "Twecoin",
  "Fugue256",
  "NIST",
  "Fresh",
  "Whirlcoin",
  "Neoscrypt",
  "WhirlpoolX",
  "Lyra2RE",
  "Lyra2REV2",
  "Lyra2REV3",
  "Lyra2Z",
  "Lyra2Zz",
  "Lyra2h",
  "Pluck",
  "Yescrypt",
  "Yescrypt-multi",
  "Blakecoin",
  "Blake",
  "Sia",
  "Decred",
  "Vanilla",
  "Ethash",
  "Lbry",
  "Phi1612",
  "Phi2",
  "Sibcoin",
  "Argon2d"
};

void sha256(const unsigned char *message, unsigned int len, unsigned char *digest)
{
  sph_sha256_context ctx_sha2;

  sph_sha256_init(&ctx_sha2);
  sph_sha256(&ctx_sha2, message, len);
  sph_sha256_close(&ctx_sha2, (void*)digest);
}

void gen_hash(const unsigned char *data, unsigned int len, unsigned char *hash)
{
  unsigned char hash1[32];
  sph_sha256_context ctx_sha2;

  sph_sha256_init(&ctx_sha2);
  sph_sha256(&ctx_sha2, data, len);
  sph_sha256_close(&ctx_sha2, hash1);
  sph_sha256(&ctx_sha2, hash1, 32);
  sph_sha256_close(&ctx_sha2, hash);
}

void sha256d_midstate(struct work *work)
{
  unsigned char data[64];
  uint32_t *data32 = (uint32_t *)data;
  sph_sha256_context ctx;

  flip64(data32, work->data);
  sph_sha256_init(&ctx);
  sph_sha256(&ctx, data, 64);
  memcpy(work->midstate, ctx.val, 32);
  endian_flip32(work->midstate, work->midstate);
}

#define CL_SET_BLKARG(blkvar) status |= clSetKernelArg(*kernel, num++, sizeof(uint), (void *)&blk->blkvar)
#define CL_SET_VARG(args, var) status |= clSetKernelArg(*kernel, num++, args * sizeof(uint), (void *)var)
#define CL_SET_ARG_N(n, var) do { status |= clSetKernelArg(*kernel, n, sizeof(var), (void *)&var); } while (0)
#define CL_SET_ARG_0(var) CL_SET_ARG_N(0, var)
#define CL_SET_ARG(var) CL_SET_ARG_N(num++, var)
#define CL_NEXTKERNEL_SET_ARG_N(n, var) do { kernel++; CL_SET_ARG_N(n, var); } while (0)
#define CL_NEXTKERNEL_SET_ARG_0(var) CL_NEXTKERNEL_SET_ARG_N(0, var)
#define CL_NEXTKERNEL_SET_ARG(var) CL_NEXTKERNEL_SET_ARG_N(num++, var)

static void append_scrypt_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
  char buf[255];
  sprintf(buf, " -D LOOKUP_GAP=%d -D CONCURRENT_THREADS=%u -D NFACTOR=%d",
    cgpu->lookup_gap, (unsigned int)cgpu->thread_concurrency, algorithm->nfactor);
  strcat(data->compiler_options, buf);

  sprintf(buf, "lg%utc%unf%u", cgpu->lookup_gap, (unsigned int)cgpu->thread_concurrency, algorithm->nfactor);
  strcat(data->binary_filename, buf);
}

static void append_ethash_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
#ifdef WIN32
  strcat(data->compiler_options, " -DWINDOWS");
#endif
}

static void append_neoscrypt_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
  char buf[255];
  sprintf(buf, " %s-D MAX_GLOBAL_THREADS=%lu ",
    ((cgpu->lookup_gap > 0) ? " -D LOOKUP_GAP=2 " : ""), (unsigned long)cgpu->thread_concurrency);
  strcat(data->compiler_options, buf);

  sprintf(buf, "%stc%lu", ((cgpu->lookup_gap > 0) ? "lg" : ""), (unsigned long)cgpu->thread_concurrency);
  strcat(data->binary_filename, buf);
}

static void append_blake256_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
  char buf[255];
  sprintf(buf, " -D LOOKUP_GAP=%d -D MAX_GLOBAL_THREADS=%lu ",
    cgpu->lookup_gap, (unsigned long)cgpu->thread_concurrency);
  strcat(data->compiler_options, buf);

  sprintf(buf, "tc%lu", (unsigned long)cgpu->thread_concurrency);
  strcat(data->binary_filename, buf);
}

static void append_x11_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
  char buf[255];
  sprintf(buf, " -D SPH_COMPACT_BLAKE_64=%d -D SPH_LUFFA_PARALLEL=%d -D SPH_KECCAK_UNROLL=%u ",
    ((opt_blake_compact) ? 1 : 0), ((opt_luffa_parallel) ? 1 : 0), (unsigned int)opt_keccak_unroll);
  strcat(data->compiler_options, buf);

  sprintf(buf, "ku%u%s%s", (unsigned int)opt_keccak_unroll, ((opt_blake_compact) ? "bc" : ""), ((opt_luffa_parallel) ? "lp" : ""));
  strcat(data->binary_filename, buf);
}


static void append_x13_compiler_options(struct _build_kernel_data *data, struct cgpu_info *cgpu, struct _algorithm_t *algorithm)
{
  char buf[255];

  append_x11_compiler_options(data, cgpu, algorithm);

  sprintf(buf, " -D SPH_HAMSI_EXPAND_BIG=%d -D SPH_HAMSI_SHORT=%d ",
    (unsigned int)opt_hamsi_expand_big, ((opt_hamsi_short) ? 1 : 0));
  strcat(data->compiler_options, buf);

  sprintf(buf, "big%u%s", (unsigned int)opt_hamsi_expand_big, ((opt_hamsi_short) ? "hs" : ""));
  strcat(data->binary_filename, buf);
}

static cl_int queue_argon2d_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{

    cl_kernel *kernel;
    cl_uint le_target;
    cl_int status = 0;
    unsigned int num = 0;

    le_target = (cl_uint)le32toh(((uint32_t *)blk->work->target)[7]);

    flip80(clState->cldata, blk->work->data);

    status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

    if (status != CL_SUCCESS) {
        applog(LOG_ERR, "EnqueueWriteBuffer failed", status);
        exit(1);
    }

    // init - search
    clSetKernelArg(clState->kernel, 0, sizeof(clState->padbuffer8), (void *)&clState->padbuffer8);
    clSetKernelArg(clState->kernel, 1, sizeof(clState->CLbuffer0), (void *)&clState->CLbuffer0);
    clSetKernelArg(clState->kernel, 2, sizeof(uint), &(blk->nonce));

    //fill - search 1
    size_t bufferSize = 32 * 8 * 1 * sizeof(cl_uint) * 2;
    uint32_t passes = 2;
    uint32_t lanes = 8;
    uint32_t segment_blocks = 15;

    clSetKernelArg(clState->extra_kernels[0], 0, sizeof(clState->padbuffer8), (void *)&clState->padbuffer8);
    clSetKernelArg(clState->extra_kernels[0], 1, sizeof(uint), &passes);
    clSetKernelArg(clState->extra_kernels[0], 2, sizeof(uint), &lanes);
    clSetKernelArg(clState->extra_kernels[0], 3, sizeof(uint), &segment_blocks);

    // // final - serach 2
    size_t smem = 129 * sizeof(cl_ulong) * 8 + 18 * sizeof(cl_ulong) * 8;
    clSetKernelArg(clState->extra_kernels[1], 0, sizeof(clState->padbuffer8), (void *)&clState->padbuffer8);
    clSetKernelArg(clState->extra_kernels[1], 1, sizeof(clState->outputBuffer), (void *)&clState->outputBuffer);
    clSetKernelArg(clState->extra_kernels[1], 2, sizeof(uint), &(blk->nonce));
    clSetKernelArg(clState->extra_kernels[1], 3, sizeof(cl_uint), (void*)&le_target);


    return status;
}

static cl_int queue_scrypt_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  unsigned char *midstate = blk->work->midstate;
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_uint le_target;
  cl_int status = 0;

  le_target = *(cl_uint *)(blk->work->device_target + 28);
  memcpy(clState->cldata, blk->work->data, 80);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_VARG(4, &midstate[0]);
  CL_SET_VARG(4, &midstate[16]);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_pascal_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip196(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 196, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);
  CL_SET_ARG(blk->work->midstate);

  return status;
}

static cl_int queue_neoscrypt_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_uint le_target;
  cl_int status = 0;

  /* This looks like a unnecessary double cast, but to make sure, that
   * the target's most significant entry is adressed as a 32-bit value
   * and not accidently by something else the double cast seems wise.
   * The compiler will get rid of it anyway. */
  le_target = (cl_uint)le32toh(((uint32_t *)blk->work->/*device_*/target)[7]);
  memcpy(clState->cldata, blk->work->data, 80);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_credits_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_ulong le_target;
  cl_int status = 0;


    // le_target = (*(cl_uint *)(blk->work->device_target + 24));
  le_target = (cl_ulong)le64toh(((uint64_t *)blk->work->/*device_*/target)[3]);
  //  le_target = (cl_uint)((uint32_t *)blk->work->target)[6];


  memcpy(clState->cldata, blk->work->data, 168);
//  flip168(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 168, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);
  CL_SET_ARG(blk->work->midstate);

  return status;
}

static cl_int queue_yescrypt_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_uint le_target;
  cl_int status = 0;


//  le_target = (*(cl_uint *)(blk->work->device_target + 28));
  le_target = (cl_uint)le32toh(((uint32_t *)blk->work->/*device_*/target)[7]);
//  le_target = (cl_uint)((uint32_t *)blk->work->target)[7];


//  memcpy(clState->cldata, blk->work->data, 80);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  CL_SET_ARG(clState->buffer2);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_yescrypt_multikernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
//  cl_kernel *kernel = &clState->kernel;
  cl_kernel *kernel;
  unsigned int num = 0;
  cl_uint le_target;
  cl_int status = 0;


  //  le_target = (*(cl_uint *)(blk->work->device_target + 28));
  le_target = (cl_uint)le32toh(((uint32_t *)blk->work->/*device_*/target)[7]);
  memcpy(clState->cldata, blk->work->data, 80);
//  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);
//pbkdf and initial sha
  kernel = &clState->kernel;

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  CL_SET_ARG(clState->buffer2);
  CL_SET_ARG(clState->buffer3);
  CL_SET_ARG(le_target);

//inactive kernel
  num = 0;
  kernel = clState->extra_kernels;
  CL_SET_ARG_N(0,clState->buffer1);
  CL_SET_ARG_N(1,clState->buffer2);
//  CL_SET_ARG_N(3, clState->buffer3);

//mix2_2
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1,clState->buffer1);
  CL_SET_ARG_N(2,clState->buffer2);
  //mix2_2
//inactive kernel
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->buffer1);
  CL_SET_ARG_N(1, clState->buffer2);
  //mix2_2

  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  CL_SET_ARG_N(2, clState->buffer2);

  //inactive kernel
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->buffer1);
  CL_SET_ARG_N(1, clState->buffer2);
  //mix2_2


//pbkdf and finalization
    num=0;
  CL_NEXTKERNEL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->buffer2);
  CL_SET_ARG(clState->buffer3);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_maxcoin_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_int status = 0;

  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);

  return status;
}

static cl_int queue_sph_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_darkcoin_mod_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_allium_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_int status = 0;
  cl_ulong le_target;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;

  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(blk->work->blk.ctx_a);
  CL_SET_ARG(blk->work->blk.ctx_b);
  CL_SET_ARG(blk->work->blk.ctx_c);
  CL_SET_ARG(blk->work->blk.ctx_d);
  CL_SET_ARG(blk->work->blk.ctx_e);
  CL_SET_ARG(blk->work->blk.ctx_f);
  CL_SET_ARG(blk->work->blk.ctx_g);
  CL_SET_ARG(blk->work->blk.ctx_h);
  CL_SET_ARG(blk->work->blk.cty_a);
  CL_SET_ARG(blk->work->blk.cty_b);
  CL_SET_ARG(blk->work->blk.cty_c);

  // keccak - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // lyra2 - search2
  // lyra2_cuda_hash_64 - search2 3 4
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  //CL_SET_ARG(clState->buffer2);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  // cubehash - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lyra2_cuda_hash_64 - search6 7 8
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  //CL_SET_ARG(clState->buffer2);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  // skein - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // groestl - search10
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_phi_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // skein - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // jh - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // gost - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search5
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_phi2_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);

  unsigned int has_roots = flip144(clState->cldata, blk->work->data);
 
  sph_cubehash512_context ctx_cubehash;
	sph_cubehash512_init(&ctx_cubehash);
	sph_cubehash512(&ctx_cubehash, (void*)clState->cldata, 64);

  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, has_roots ? 144 : 80, clState->cldata, 0, NULL, NULL);

  // cubehash512_cuda_hash_80/144 - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(has_roots);
  
  kernel = clState->extra_kernels;
  // lyra2_cuda_hash_64 - search1 2 3
  num = 0;
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer3);
  //CL_SET_ARG(clState->buffer2);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->buffer3);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer3);
  // quark_jh512_cpu_hash_64 - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // phi_filter_cuda - search3
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  CL_SET_ARG(clState->buffer2);
  // streebog_cpu_hash_64 - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // x11_echo512_cpu_hash_64 search5
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  // x11_echo512_cpu_hash_64 search6
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  // phi_merge_cuda search7
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  CL_SET_ARG(clState->buffer2);
  // quark_skein512_cpu_hash_64 search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // phi_final_compress_cuda search9
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_sibcoin_mod_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // gost - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);  
  // luffa - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search11
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}


static cl_int queue_bitblock_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_bitblockold_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // combined echo, hamsi, fugue - shabal - whirlpool - search10
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_x22i_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL,NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shabal - search13
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  // whirlpool - search14
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  CL_SET_ARG_N(1, clState->buffer2);
  // sha512 - search15
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer2);
  CL_SET_ARG_N(1, clState->buffer3);
  // swifftx - search16
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  CL_SET_ARG_N(2, clState->buffer2);
  CL_SET_ARG_N(3, clState->buffer3);
  // haval - search17
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // tiger - search18
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lyra2v2 p1 - search19
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->MidstateBuf);
  // lyra2v2 p2 - search20
  CL_NEXTKERNEL_SET_ARG_0(clState->MidstateBuf);
  // lyra2v2 p3 - search21
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->MidstateBuf);
  // gost - search22
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // sha256 - search23
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_x25x_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL,NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);

  kernel = clState->extra_kernels;
  
  // bmw - search1
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shabal - search13
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // whirlpool - search14
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // sha512 - search15
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // swifftx - search16
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // haval - search17
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // tiger - search18
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lyra2v2 p1 - search19
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->MidstateBuf);
  // lyra2v2 p2 - search20
  CL_NEXTKERNEL_SET_ARG_0(clState->MidstateBuf);
  // lyra2v2 p3 - search21
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  CL_SET_ARG_N(1, clState->MidstateBuf);
  // gost - search22
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // sha256 - search23
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // panama - search24
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lane - search25
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // x25x_shuffle - search26
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // blake2s - search27
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_marucoin_mod_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_marucoin_mod_old_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // combined echo, hamsi, fugue - search10
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_talkcoin_mod_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // groestl - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // jh - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search4
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_x14_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search10
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // hamsi - search11
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // fugue - search12
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shabal - search13
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_x14_old_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // keccak - search5
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // luffa - search6
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // shavite - search8
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // simd - search9
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // combined echo, hamsi, fugue - shabal - search10
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_fresh_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // shavite 1 - search
  kernel = &clState->kernel;
  num = 0;
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  // smid 1 - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // shavite 2 - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // smid 2 - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // echo - search4
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_whirlcoin_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  //clbuffer, hashes
  kernel = &clState->kernel;
  CL_SET_ARG_N(0, clState->CLbuffer0);
  CL_SET_ARG_N(1, clState->padbuffer8);

  kernel = clState->extra_kernels;
  CL_SET_ARG_N(0, clState->padbuffer8);

  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);

  //hashes, output, target
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->outputBuffer);
  CL_SET_ARG_N(2, le_target);

  return status;
}

static cl_int queue_whirlpoolx_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  uint64_t midblock[8], key[8] = { 0 }, tmp[8] = { 0 };
  cl_ulong le_target;
  cl_int status;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);

  memcpy(midblock, clState->cldata, 64);

  // midblock = n, key = h
  for (int i = 0; i < 10; ++i) {
    tmp[0] = WHIRLPOOL_ROUND_CONSTANTS[i];
    whirlpool_round(key, tmp);
    tmp[0] = 0;
    whirlpool_round(midblock, tmp);

    for (int x = 0; x < 8; ++x) {
      midblock[x] ^= key[x];
    }
  }

  for (int i = 0; i < 8; ++i) {
    midblock[i] ^= ((uint64_t *)(clState->cldata))[i];
  }

  status = clSetKernelArg(clState->kernel, 0, sizeof(cl_ulong8), (cl_ulong8 *)&midblock);
  status |= clSetKernelArg(clState->kernel, 1, sizeof(cl_ulong), (void *)(((uint64_t *)clState->cldata) + 8));
  status |= clSetKernelArg(clState->kernel, 2, sizeof(cl_ulong), (void *)(((uint64_t *)clState->cldata) + 9));
  status |= clSetKernelArg(clState->kernel, 3, sizeof(cl_mem), (void *)&clState->outputBuffer);
  status |= clSetKernelArg(clState->kernel, 4, sizeof(cl_ulong), (void *)&le_target);

  return status;
}

static cl_int queue_lyra2re_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_int status = 0;
  cl_ulong le_target;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;

  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(blk->work->blk.ctx_a);
  CL_SET_ARG(blk->work->blk.ctx_b);
  CL_SET_ARG(blk->work->blk.ctx_c);
  CL_SET_ARG(blk->work->blk.ctx_d);
  CL_SET_ARG(blk->work->blk.ctx_e);
  CL_SET_ARG(blk->work->blk.ctx_f);
  CL_SET_ARG(blk->work->blk.ctx_g);
  CL_SET_ARG(blk->work->blk.ctx_h);
  CL_SET_ARG(blk->work->blk.cty_a);
  CL_SET_ARG(blk->work->blk.cty_b);
  CL_SET_ARG(blk->work->blk.cty_c);

  // bmw - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // groestl - search2
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // skein - search3
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // jh - search4
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_lyra2rev2_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_int status = 0;
  cl_ulong le_target;

  //  le_target = *(cl_uint *)(blk->work->device_target + 28);
  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  //  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(blk->work->blk.ctx_a);
  CL_SET_ARG(blk->work->blk.ctx_b);
  CL_SET_ARG(blk->work->blk.ctx_c);
  CL_SET_ARG(blk->work->blk.ctx_d);
  CL_SET_ARG(blk->work->blk.ctx_e);
  CL_SET_ARG(blk->work->blk.ctx_f);
  CL_SET_ARG(blk->work->blk.ctx_g);
  CL_SET_ARG(blk->work->blk.ctx_h);
  CL_SET_ARG(blk->work->blk.cty_a);
  CL_SET_ARG(blk->work->blk.cty_b);
  CL_SET_ARG(blk->work->blk.cty_c);

  // keccak - search1
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search2
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lyra - search3,4,5
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  // skein -search6
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // cubehash - search7
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // bmw - search8
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_lyra2rev3_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel;
  unsigned int num;
  cl_int status = 0;
  cl_ulong le_target;

  //  le_target = *(cl_uint *)(blk->work->device_target + 28);
  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  // blake - search
  kernel = &clState->kernel;
  num = 0;
  //  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(blk->work->blk.ctx_a);
  CL_SET_ARG(blk->work->blk.ctx_b);
  CL_SET_ARG(blk->work->blk.ctx_c);
  CL_SET_ARG(blk->work->blk.ctx_d);
  CL_SET_ARG(blk->work->blk.ctx_e);
  CL_SET_ARG(blk->work->blk.ctx_f);
  CL_SET_ARG(blk->work->blk.ctx_g);
  CL_SET_ARG(blk->work->blk.ctx_h);
  CL_SET_ARG(blk->work->blk.cty_a);
  CL_SET_ARG(blk->work->blk.cty_b);
  CL_SET_ARG(blk->work->blk.cty_c);

  // lyra2rev3 - search1,2,3
  kernel = clState->extra_kernels;
  num = 0;
  CL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  // cubehash - search4
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->padbuffer8);
  // lyra - search5,6,7
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_0(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG_N(0, clState->padbuffer8);
  CL_SET_ARG_N(1, clState->buffer1);
  // bmw - search8
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_lyra2z_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
	cl_kernel *kernel;
	unsigned int num;
	cl_int status = 0;
	cl_ulong le_target;

	//  le_target = *(cl_uint *)(blk->work->device_target + 28);
	le_target = *(cl_ulong *)(blk->work->device_target + 24);
	flip80(clState->cldata, blk->work->data);
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

	// blake - search
	kernel = &clState->kernel;
	num = 0;
	//  CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(blk->work->blk.ctx_a);
	CL_SET_ARG(blk->work->blk.ctx_b);
	CL_SET_ARG(blk->work->blk.ctx_c);
	CL_SET_ARG(blk->work->blk.ctx_d);
	CL_SET_ARG(blk->work->blk.ctx_e);
	CL_SET_ARG(blk->work->blk.ctx_f);
	CL_SET_ARG(blk->work->blk.ctx_g);
	CL_SET_ARG(blk->work->blk.ctx_h);
	CL_SET_ARG(blk->work->blk.cty_a);
	CL_SET_ARG(blk->work->blk.cty_b);
	CL_SET_ARG(blk->work->blk.cty_c);

	kernel = clState->extra_kernels;
  // lyra2_cuda_hash_64 - search1 2 3
  num = 0;
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  //CL_SET_ARG(clState->buffer2);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
	num = 0;
	//output
	CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(clState->outputBuffer);
	CL_SET_ARG(le_target);
	
	return status;
}

static cl_int queue_lyra2zz_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
	cl_kernel *kernel;
	unsigned int num;
	cl_int status = 0;
	cl_ulong le_target;

	//  le_target = *(cl_uint *)(blk->work->device_target + 28);
	le_target = *(cl_ulong *)(blk->work->device_target + 24);
	flip112(clState->cldata, blk->work->data);
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 112, clState->cldata, 0, NULL, NULL);

	// blake 112 - search
	kernel = &clState->kernel;
	num = 0;
	//  CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(blk->work->blk.ctx_a);
	CL_SET_ARG(blk->work->blk.ctx_b);
	CL_SET_ARG(blk->work->blk.ctx_c);
	CL_SET_ARG(blk->work->blk.ctx_d);
	CL_SET_ARG(blk->work->blk.ctx_e);
	CL_SET_ARG(blk->work->blk.ctx_f);
	CL_SET_ARG(blk->work->blk.ctx_g);
	CL_SET_ARG(blk->work->blk.ctx_h);
	CL_SET_ARG(blk->work->blk.cty_a);
	CL_SET_ARG(blk->work->blk.cty_b);
	CL_SET_ARG(blk->work->blk.cty_c);
  CL_SET_ARG(blk->work->blk.cty_d);
  CL_SET_ARG(blk->work->blk.cty_e);
  CL_SET_ARG(blk->work->blk.cty_f);
  CL_SET_ARG(blk->work->blk.cty_g);
  CL_SET_ARG(blk->work->blk.cty_h);
  CL_SET_ARG(blk->work->blk.cty_i);
  CL_SET_ARG(blk->work->blk.cty_j);
  CL_SET_ARG(blk->work->blk.cty_k);

	kernel = clState->extra_kernels;
  // lyra2_cuda_hash_64 - search1 2 3
  num = 0;
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
  //CL_SET_ARG(clState->buffer2);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->buffer1);
  num = 0;
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->buffer1);
	num = 0;
	//output
	CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(clState->outputBuffer);
	CL_SET_ARG(le_target);
	
	return status;
}

static cl_int queue_lyra2h_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
	cl_kernel *kernel;
	unsigned int num;
	cl_int status = 0;
	cl_ulong le_target;

	//  le_target = *(cl_uint *)(blk->work->device_target + 28);
	le_target = *(cl_ulong *)(blk->work->device_target + 24);
	flip80(clState->cldata, blk->work->data);
	status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

	// blake - search
	kernel = &clState->kernel;
	num = 0;
	//  CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(blk->work->blk.ctx_a);
	CL_SET_ARG(blk->work->blk.ctx_b);
	CL_SET_ARG(blk->work->blk.ctx_c);
	CL_SET_ARG(blk->work->blk.ctx_d);
	CL_SET_ARG(blk->work->blk.ctx_e);
	CL_SET_ARG(blk->work->blk.ctx_f);
	CL_SET_ARG(blk->work->blk.ctx_g);
	CL_SET_ARG(blk->work->blk.ctx_h);
	CL_SET_ARG(blk->work->blk.cty_a);
	CL_SET_ARG(blk->work->blk.cty_b);
	CL_SET_ARG(blk->work->blk.cty_c);
	num = 0;
	// keccak - search1
	kernel = clState->extra_kernels;
	CL_SET_ARG(clState->padbuffer8);
	CL_SET_ARG(clState->buffer1);
	CL_SET_ARG(clState->outputBuffer);
	CL_SET_ARG(le_target);
	return status;
}

static cl_int queue_pluck_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_uint le_target;
  cl_int status = 0;

  le_target = (cl_uint)le32toh(((uint32_t *)blk->work->/*device_*/target)[7]);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_blake_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_int status = 0;
  cl_ulong le_target;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(blk->work->blk.ctx_a);
  CL_SET_ARG(blk->work->blk.ctx_b);
  CL_SET_ARG(blk->work->blk.ctx_c);
  CL_SET_ARG(blk->work->blk.ctx_d);
  CL_SET_ARG(blk->work->blk.ctx_e);
  CL_SET_ARG(blk->work->blk.ctx_f);
  CL_SET_ARG(blk->work->blk.ctx_g);
  CL_SET_ARG(blk->work->blk.ctx_h);

  CL_SET_ARG(blk->work->blk.cty_a);
  CL_SET_ARG(blk->work->blk.cty_b);
  CL_SET_ARG(blk->work->blk.cty_c);

  return status;
}

extern pthread_mutex_t eth_nonce_lock;
extern uint32_t eth_nonce;
static const int eth_future_epochs = 6;
static cl_int queue_ethash_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  struct pool *pool = blk->work->pool;
  eth_dag_t *dag;
  cl_kernel *kernel;
  unsigned int num = 0;
  cl_int status = 0;
  cl_ulong le_target;
  cl_uint Isolate2 = UINT32_MAX;

  dag = &blk->work->thr->cgpu->eth_dag;
  cg_ilock(&dag->lock);
  cg_ilock(&pool->data_lock);
  if (pool->eth_cache.disabled || pool->eth_cache.dag_cache == NULL) {
    cg_iunlock(&pool->data_lock);
    cg_iunlock(&dag->lock);
    cgsleep_ms(200);
    applog(LOG_DEBUG, "THR[%d]: stop ETHASH mining (%d, %p)", blk->work->thr_id, pool->eth_cache.disabled, pool->eth_cache.dag_cache);
    return 1;
  }
  if (dag->current_epoch != blk->work->eth_epoch) {
    applog(LOG_NOTICE, "GPU%d: begin DAG creation...", blk->work->thr->cgpu->device_id);
    cl_ulong CacheSize = EthGetCacheSize(blk->work->eth_epoch);
    cg_ulock(&dag->lock);
    if (dag->dag_buffer == NULL || blk->work->eth_epoch >= dag->max_epoch + 1U) {
      if (dag->dag_buffer != NULL) {
        cg_dlock(&pool->data_lock);
        clReleaseMemObject(dag->dag_buffer);
      }
      else {
        cg_ulock(&pool->data_lock);
        int size = ++pool->eth_cache.nDevs;
        pool->eth_cache.dags = (eth_dag_t **) realloc(pool->eth_cache.dags, sizeof(void*) * size);
        pool->eth_cache.dags[size-1] = dag;
        dag->pool = pool;
        cg_dwlock(&pool->data_lock);
      }
      dag->max_epoch = blk->work->eth_epoch + eth_future_epochs;
      dag->dag_buffer = clCreateBuffer(clState->context, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS, EthGetDAGSize(dag->max_epoch), NULL, &status);
      if (status != CL_SUCCESS) {
        cg_runlock(&pool->data_lock);
        dag->max_epoch = 0;
        dag->dag_buffer = NULL;
        cg_wunlock(&dag->lock);
        applog(LOG_ERR, "Error %d: Creating the DAG buffer failed.", status);
        return status;
      }
    }
    else
      cg_dlock(&pool->data_lock);

    applog(LOG_DEBUG, "DAG being regenerated.");
    cl_mem eth_cache = clCreateBuffer(clState->context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR | CL_MEM_HOST_WRITE_ONLY, CacheSize, pool->eth_cache.dag_cache, &status);
    cg_runlock(&pool->data_lock);
    if (status != CL_SUCCESS) {
      clReleaseMemObject(eth_cache);
      cg_wunlock(&dag->lock);
      applog(LOG_ERR, "Error %d: Creating the ethash cache buffer failed.", status);
      return status;
    }

    // enqueue DAG gen kernel
    kernel = &clState->GenerateDAG;

    cl_uint zero = 0;
    cl_uint CacheSize64 = CacheSize / 64;

    CL_SET_ARG(zero);
    CL_SET_ARG(eth_cache);
    CL_SET_ARG(dag->dag_buffer);
    CL_SET_ARG(CacheSize64);
    CL_SET_ARG(Isolate2);

    cl_ulong DAGSize = EthGetDAGSize(blk->work->eth_epoch);
    size_t DAGItems = (size_t) (DAGSize / 64);
    cgsleep_ms(128 * blk->work->thr->cgpu->device_id); 
    status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->GenerateDAG, 1, NULL, &DAGItems, NULL, 0, NULL, NULL);
    clFinish(clState->commandQueue);

    clReleaseMemObject(eth_cache);
    if (status != CL_SUCCESS) {
      cg_wunlock(&dag->lock);
      applog(LOG_ERR, "Error %d: Setting args for the DAG kernel and/or executing it.", status);
      return status;
    }
    dag->current_epoch = blk->work->eth_epoch;
    cg_dwlock(&dag->lock);
    applog(LOG_NOTICE, "GPU%d: new DAG created", blk->work->thr->cgpu->device_id);
  }
  else {
    cg_dlock(&dag->lock);
    cg_iunlock(&pool->data_lock);
  }

  memcpy(&le_target, blk->work->device_target + 24, 8);
  blk->work->Nonce = (blk->work->Nonce >> 32 << 32) | blk->work->blk.nonce;

  num = 0;
  kernel = &clState->kernel;

  // Not nodes now (64 bytes), but DAG entries (128 bytes)
  cl_ulong DAGSize = EthGetDAGSize(blk->work->eth_epoch);
  cl_ulong ItemsArg = DAGSize / 128;

  // DO NOT flip80.
  status |= clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, CL_FALSE, 0, 32, blk->work->data, 0, NULL, NULL);
  
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(dag->dag_buffer);
  CL_SET_ARG(ItemsArg);
  CL_SET_ARG(blk->work->Nonce);
  CL_SET_ARG(le_target);

  if (status != CL_SUCCESS)
    cg_runlock(&dag->lock);
  return status;
}

void get_argon_block(cl_command_queue Queue, cl_mem block, cl_mem block2, uint8_t* clblock, uint32_t index)
{
	size_t TheSize = 128*sizeof(uint64_t);
	size_t TheOffSet = 128*sizeof(uint64_t)*index;
	size_t Shift = 2 * 1024 * 1024 * 128 * sizeof(uint64_t);
	cl_int status;
if (index < 2 * 1024 * 1024)
	status = clEnqueueReadBuffer(Queue, block, CL_TRUE, TheOffSet, TheSize, clblock, 0, NULL, NULL);
else 
	status = clEnqueueReadBuffer(Queue, block2, CL_TRUE, TheOffSet-Shift, TheSize, clblock, 0, NULL, NULL);
	if (status != CL_SUCCESS) {
		applog(LOG_ERR, "reading %d with writing to CLbuffer0.", status);
	}
}



static cl_int queue_mtp_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
	struct pool *pool = blk->work->pool;
	mtp_cache_t *mtp = &blk->work->thr->cgpu->mtp_buffer.mtp_cache;
	size_t worksize = clState->wsize;
	cl_kernel *kernel;
	unsigned int num = 0;
	cl_int status = 0;
	cl_uint le_target;
	cl_uint HighNonce, Isolate = UINT32_MAX;

	le_target = (cl_uint)le32toh(((uint32_t *)blk->work->/*device_*/target)[7]);
	uint32_t ptarget[8];
	for (int i = 0; i<8; i++) ptarget[i] = le32toh(((uint32_t *)blk->work->/*device_*/target)[i]);

	/////////////////////////////////////////////////////////////////////////////////////////////////////

	memcpy(clState->cldata, blk->work->data, 84);
	uint32_t  endiandata[20];

	for (int k = 0; k < 19; k++)
		endiandata[k] = ((uint32_t*)blk->work->data)[k];

	endiandata[19] = ((uint32_t*)blk->work->data)[20];
	mtp_gpu_t *buffer = &blk->work->thr->cgpu->mtp_buffer;

	//	printf("coming in queue mtp kernel prev_job_id %s job_id %s\n", blk->work->prev_job_id, blk->work->job_id);

	uint32_t test = 1;

	if (buffer->prev_job_id != NULL) {
		test = strcmp(buffer->prev_job_id, pool->swork.job_id);
	}
	//	printf("coming into initialization   test result = %d\n",test);

	if (test != 0)
	{  // do initialization
	   /*
	   printf("*********** INIT MTP**************\n");

	   if (buffer->prev_job_id == NULL) {
	   mtp = (mtp_cache_t*)malloc(sizeof(mtp_cache_t));
	   }
	   */
	   /////////////////////////////////////////////////
		int TED = 0;
		for (int i = 0; i< total_devices; i++)
			if (devices_enabled[i]) TED++;

		if (TED == 0) TED++;

		buffer->nDevs = TED;
		buffer->MaxNonce = 0xFFFFFFFF / TED;
		if (buffer->MaxNonce != 0xFFFFFFFF)
			buffer->StartNonce = (blk->work->thr->id)*buffer->MaxNonce;
		else
			buffer->StartNonce = 0;


		////////////////////////////////////////////////
		if (buffer->prev_job_id != NULL) {

			//		free_memory(&mtp->context, (unsigned char *)mtp->instance.memory, mtp->instance.memory_argon_blocks, sizeof(argon_block));

			free(mtp->instance.memory);
			//			mtp->ordered_tree->Destructor();
			call_MerkleTree_Destructor(mtp->ordered_tree);
			free(mtp->dx);
			//		delete  mtp->ordered_tree;
			clReleaseMemObject(buffer->hblock);
			clReleaseMemObject(buffer->hblock2);
			clReleaseMemObject(buffer->tree);
			clReleaseMemObject(buffer->blockheader);
			clReleaseMemObject(buffer->root);

		}



		////////////
		size_t hbs_half = 2 * 1024 * 1024 * 128 * sizeof(uint64_t);
		uint32_t argon_memcost = 4 * 1024 * 1024;
		size_t hbs = 4 * 1024 * 1024 * 128 * sizeof(uint64_t);
		//			size_t hbs = 4244635648;
		/*
		buffer->hblock		= clCreateBuffer(clState->context, CL_MEM_READ_WRITE , hbs, NULL, &status);
		if (status != CL_SUCCESS) {
		buffer->hblock = NULL;
		applog(LOG_ERR, "Error %d while creating the hblock buffers.", status);
		return status;
		}
		*/
		buffer->hblock = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, hbs_half, NULL, &status);
		if (status != CL_SUCCESS) {
			buffer->hblock = NULL;
			applog(LOG_ERR, "Error %d while creating the hblock buffers.", status);
			return status;
		}
		buffer->hblock2 = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, hbs_half, NULL, &status);
		if (status != CL_SUCCESS) {
			buffer->hblock = NULL;
			applog(LOG_ERR, "Error %d while creating the hblock buffers.", status);
			return status;
		}

		size_t ts = sizeof(uint64_t) * 2 * 1048576 * 4;
		buffer->tree = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, ts, NULL, &status);
		if (status != CL_SUCCESS) {
			buffer->tree = NULL;
			applog(LOG_ERR, "Error %d while creating the tree buffers.", status);
			return status;
		}
		size_t bs = 8 * sizeof(uint32_t);
		buffer->blockheader = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, bs, NULL, &status);
		if (status != CL_SUCCESS) {
			buffer->blockheader = NULL;
			applog(LOG_ERR, "Error %d while creating the blockheader buffers.", status);
			return status;
		}
		size_t rs = 4 * sizeof(uint32_t);
		buffer->root = clCreateBuffer(clState->context, CL_MEM_READ_WRITE, rs, NULL, &status);
		if (status != CL_SUCCESS) {
			buffer->root = NULL;
			applog(LOG_ERR, "Error %d while creating the root buffers.", status);
			return status;
		}


		mtp->dx = (uint8_t*)malloc(MTP_TREE_SIZE);

		mtp->context = init_mtp_argon2d_param((const char*)endiandata);
		mtp_argon2_ctx_from_mtp(&mtp->context, &mtp->instance);
		//// copy first blocks to gpu

		size_t TheSize = 16 * sizeof(uint64_t);

		size_t TheOffSet1 = 16 * sizeof(uint64_t);

for (int i=0;i<4;i++) {
		size_t TheOffSet =i * argon_memcost * sizeof(uint64_t);
		size_t ThatSize = i * 16;
		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 0, TheSize, (unsigned char*)(mtp->instance.memory[0].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 1, TheSize, (unsigned char*)(mtp->instance.memory[1].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 1048576, TheSize, (unsigned char*)(mtp->instance.memory[2].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 1048577, TheSize, (unsigned char*)(mtp->instance.memory[3].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 2097152, TheSize, (unsigned char*)(mtp->instance.memory[4].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 2097153, TheSize, (unsigned char*)(mtp->instance.memory[5].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 3145728, TheSize, (unsigned char*)(mtp->instance.memory[6].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);

		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock, true, TheOffSet + TheOffSet1 * 3145729, TheSize, (unsigned char*)(mtp->instance.memory[7].v + ThatSize), 0, NULL, NULL);
		if (status != CL_SUCCESS)
			applog(LOG_ERR, "problem copying instance to hblock", status);
}
for (int i = 0; i<4; i++) {
	size_t TheOffSet = i * argon_memcost * sizeof(uint64_t);
	size_t ThatSize = (i+4) * 16;
	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 0, TheSize, (unsigned char*)(mtp->instance.memory[0].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 1, TheSize, (unsigned char*)(mtp->instance.memory[1].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 1048576, TheSize, (unsigned char*)(mtp->instance.memory[2].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 1048577, TheSize, (unsigned char*)(mtp->instance.memory[3].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 2097152, TheSize, (unsigned char*)(mtp->instance.memory[4].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 2097153, TheSize, (unsigned char*)(mtp->instance.memory[5].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 3145728, TheSize, (unsigned char*)(mtp->instance.memory[6].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);

	status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->hblock2, true, TheOffSet + TheOffSet1 * 3145729, TheSize, (unsigned char*)(mtp->instance.memory[7].v + ThatSize), 0, NULL, NULL);
	if (status != CL_SUCCESS)
		applog(LOG_ERR, "problem copying instance to hblock2", status);
}


		status |= clEnqueueWriteBuffer(clState->commandQueue, buffer->blockheader, true, 0, 32, (unsigned char*)mtp->instance.argon_block_header, 0, NULL, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating the MTP buffers.", status);
		}
		num = 0;
		kernel = &clState->mtp_0;

		cl_int slice = 0;
		size_t Global = 128;
		size_t Local = 32;
		CL_SET_ARG(buffer->hblock);
		CL_SET_ARG(buffer->hblock2);
		CL_SET_ARG(buffer->blockheader);
		CL_SET_ARG(slice);
		status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_0, 1, NULL, &Global, &Local, 0, NULL, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating the MTP buffers kernel 1.", status);
		}
		num = 0;
		kernel = &clState->mtp_1;

		slice = 1;
		CL_SET_ARG(buffer->hblock);
		CL_SET_ARG(buffer->hblock2);
		CL_SET_ARG(buffer->blockheader);
		CL_SET_ARG(slice);
		status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_1, 1, NULL, &Global, &Local, 0, NULL, NULL);
		//		clFinish(clState->commandQueue);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating the MTP buffers kernel 2.", status);
		}
		num = 0;
		kernel = &clState->mtp_2;
		slice = 2;
		CL_SET_ARG(buffer->hblock);
		CL_SET_ARG(buffer->hblock2);
		CL_SET_ARG(buffer->blockheader);
		CL_SET_ARG(slice);
		status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_2, 1, NULL, &Global, &Local, 0, NULL, NULL);
		//		clFinish(clState->commandQueue);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating the MTP buffers kernel 3.", status);
		}
		num = 0;
		kernel = &clState->mtp_3;
		slice = 3;
		CL_SET_ARG(buffer->hblock);
		CL_SET_ARG(buffer->hblock2);
		CL_SET_ARG(buffer->blockheader);
		CL_SET_ARG(slice);
		status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_3, 1, NULL, &Global, &Local, 0, NULL, NULL);
		//		clFinish(clState->commandQueue);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating the MTP buffers kernel 4.", status);
		}
		num = 0;
		kernel = &clState->mtp_fc;

		slice = 4194304;
		CL_SET_ARG(slice);
		CL_SET_ARG(buffer->hblock);
		CL_SET_ARG(buffer->hblock2);
		CL_SET_ARG(buffer->tree);
		size_t Global2 = 4194304;
		size_t Local2 = 256;
		status |= clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_fc, 1, NULL, &Global2, &Local2, 0, NULL, NULL);
		if (status != CL_SUCCESS) {
			applog(LOG_ERR, "Error %d while creating mtp_fc kernel", status);
		}
		size_t mtp_tree_size = 2 * 1048576 * 4 * sizeof(uint64_t);
		clEnqueueReadBuffer(clState->commandQueue, buffer->tree, CL_TRUE, 0, mtp_tree_size, mtp->dx, 0, NULL, NULL);

		//	mtp->ordered_tree = new MerkleTree(mtp->dx, true);
		mtp->ordered_tree = call_new_MerkleTree(mtp->dx, true);


		buffer->prev_job_id = pool->swork.job_id;

		call_MerkleTree_getRoot(mtp->ordered_tree, mtp->TheMerkleRoot);
		/*
		MerkleTree::Buffer root = mtp->ordered_tree->getRoot();
		std::copy(root.begin(), root.end(), mtp->TheMerkleRoot);
		root.resize(0);
		*/

	}


	/////////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////////////
	//// hashing here
	// DO NOT flip80.
	cl_int status1 = 0;
	status1 = clEnqueueWriteBuffer(clState->commandQueue, buffer->root, CL_TRUE, 0, 4 * sizeof(uint32_t), mtp->TheMerkleRoot, 0, NULL, NULL);
	if (status1 != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d with writing to root buffer.", status1);
	}
	status1 = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 20 * sizeof(uint32_t), (unsigned char*)endiandata, 0, NULL, NULL);
	if (status1 != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d with writing to CLbuffer0.", status1);
	}

	size_t p_global_work_offset = buffer->StartNonce;
	uint32_t rawint = 2 << (blk->work->thr->cgpu->intensity - 1);
	kernel = &clState->mtp_yloop;
	size_t Global2 = rawint; //1048576; //65536;
	size_t Local2 = worksize;
	size_t buffersize = 1024;

	num = 0;
	CL_SET_ARG(clState->CLbuffer0);
	CL_SET_ARG(buffer->hblock);
	CL_SET_ARG(buffer->hblock2);
	CL_SET_ARG(buffer->root);
	CL_SET_ARG(clState->outputBuffer);
	CL_SET_ARG(le_target);
	uint32_t Solution[256];

	status1 = clEnqueueNDRangeKernel(clState->commandQueue, clState->mtp_yloop, 1, &p_global_work_offset, &Global2, &Local2, 0, NULL, NULL);
	if (status1 != CL_SUCCESS) {
		applog(LOG_ERR, "Error %d with kernel mtp_yloop.", status1);
	}

	status1 = clEnqueueReadBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0, buffersize, Solution, 0, NULL, NULL);
	if (status1 != CL_SUCCESS) {
		applog(LOG_ERR, "Error reading Solution.", status1);
	}
	buffer->StartNonce += rawint;
	if (Solution[0xff]) {
		//		uint256 TheUint256Target[1];
		//		TheUint256Target[0] = ((uint256*)ptarget)[0];
		unsigned char mtpHashValue[32];
		argon_blockS nBlockMTP[MTP_L * 2] = { 0 };
		unsigned char nProofMTP[MTP_L * 3 * 353] = { 0 };
		//		printf("MTP Found a Nonce = %08x\n",Solution[0]);



		uint32_t is_sol = mtp_solver_c(0, clState->commandQueue, buffer->hblock, buffer->hblock2, Solution[0],
			&mtp->instance, nBlockMTP, nProofMTP, mtp->TheMerkleRoot, mtpHashValue, mtp->ordered_tree, endiandata, (uint256*)ptarget);
		if (is_sol == 1) {
			memcpy(pool->mtp_cache.mtpPOW.MerkleRoot, mtp->TheMerkleRoot, 16);
			for (int j = 0; j<(MTP_L * 2); j++)
				for (int i = 0; i<128; i++)
					pool->mtp_cache.mtpPOW.nBlockMTP[j][i] = nBlockMTP[j].v[i];

			memcpy(pool->mtp_cache.mtpPOW.nProofMTP, nProofMTP, sizeof(unsigned char)* MTP_L * 3 * 353);
			pool->mtp_cache.mtpPOW.TheNonce = Solution[0];
			((uint32_t*)blk->work->data)[19] = Solution[0];
			memcpy(blk->work->hash, mtpHashValue, 32);
			Solution[0xff] = 1; // avoid duplicate ?
								//			printf("*************************************************************************************Found a solution\n");
		}
		else {
			Solution[0xff] = 0;
			hw_errors++;
			blk->work->thr->cgpu->hw_errors++;
			blk->work->thr->cgpu->drv->hw_error(blk->work->thr);
			status1 = clEnqueueWriteBuffer(clState->commandQueue, clState->outputBuffer, CL_TRUE, 0, buffersize, Solution, 0, NULL, NULL);
		}
	}
	//	clFinish(clState->commandQueue);
	//printf("after mtp_yloop\n");
	//	if (status != CL_SUCCESS)
	//		cg_runlock(&dag->lock);
	return status;
}

static cl_int queue_sia_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->device_target + 24);
  flip80(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 80, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static cl_int queue_decred_kernel(_clState *clState, dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_int status = 0;

  CL_SET_ARG(clState->outputBuffer);
  /* Midstate */
  CL_SET_BLKARG(ctx_a);
  CL_SET_BLKARG(ctx_b);
  CL_SET_BLKARG(ctx_c);
  CL_SET_BLKARG(ctx_d);
  CL_SET_BLKARG(ctx_e);
  CL_SET_BLKARG(ctx_f);
  CL_SET_BLKARG(ctx_g);
  CL_SET_BLKARG(ctx_h);
  /* Last 52 bytes of data (without nonce) */
  CL_SET_BLKARG(cty_a);
  CL_SET_BLKARG(cty_b);
  CL_SET_BLKARG(cty_c);
  CL_SET_BLKARG(cty_d);
  CL_SET_BLKARG(cty_e);
  CL_SET_BLKARG(cty_f);
  CL_SET_BLKARG(cty_g);
  CL_SET_BLKARG(cty_h);
  CL_SET_BLKARG(cty_i);
  CL_SET_BLKARG(cty_j);
  CL_SET_BLKARG(cty_k);
  CL_SET_BLKARG(cty_l);

  return status;
}

static cl_int queue_lbry_kernel(struct __clState *clState, struct _dev_blk_ctx *blk, __maybe_unused cl_uint threads)
{
  cl_kernel *kernel = &clState->kernel;
  unsigned int num = 0;
  cl_ulong le_target;
  cl_int status = 0;

  le_target = *(cl_ulong *)(blk->work->target + 24);
  flip112(clState->cldata, blk->work->data);
  status = clEnqueueWriteBuffer(clState->commandQueue, clState->CLbuffer0, true, 0, 112, clState->cldata, 0, NULL, NULL);

  CL_SET_ARG(clState->CLbuffer0);
  CL_SET_ARG(clState->padbuffer8);
  num = 0;
  kernel = clState->extra_kernels;
  CL_SET_ARG_0(clState->padbuffer8);
  num = 0;
  
  CL_NEXTKERNEL_SET_ARG(clState->padbuffer8);
  CL_SET_ARG(clState->outputBuffer);
  CL_SET_ARG(le_target);

  return status;
}

static algorithm_settings_t algos[] = {
  // kernels starting from this will have difficulty calculated by using litecoin algorithm
#define A_SCRYPT(a) \
  { a, ALGO_SCRYPT, "", 1, 65536, 65536, 0, 0, 0xFF, 0xFFFFFFFFULL, 0x0000ffffUL, 0, -1, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, scrypt_regenhash, NULL, NULL, queue_scrypt_kernel, gen_hash, append_scrypt_compiler_options }
  A_SCRYPT("ckolivas"),
  A_SCRYPT("alexkarnew"),
  A_SCRYPT("alexkarnold"),
  A_SCRYPT("bufius"),
  A_SCRYPT("psw"),
  A_SCRYPT("zuikkis"),
  A_SCRYPT("arebyp"),
#undef A_SCRYPT

#define A_NEOSCRYPT(a) \
  { a, ALGO_NEOSCRYPT, "", 1, 65536, 65536, 0, 0, 0xFF, 0xFFFF000000000000ULL, 0x0000ffffUL, 0, -1, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, neoscrypt_regenhash, NULL, NULL, queue_neoscrypt_kernel, gen_hash, append_neoscrypt_compiler_options }
  A_NEOSCRYPT("neoscrypt"),
#undef A_NEOSCRYPT

#define A_PLUCK(a) \
  { a, ALGO_PLUCK, "", 1, 65536, 65536, 0, 0, 0xFF, 0xFFFF000000000000ULL, 0x0000ffffUL, 0, -1, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, pluck_regenhash, NULL, NULL, queue_pluck_kernel, gen_hash, append_neoscrypt_compiler_options }
  A_PLUCK("pluck"),
#undef A_PLUCK

#define A_CREDITS(a) \
  { a, ALGO_CRE, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFF000000000000ULL, 0x0000ffffUL, 0, -1, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, credits_regenhash, NULL, NULL, queue_credits_kernel, gen_hash, NULL}
  A_CREDITS("credits"),
#undef A_CREDITS

#define A_DECRED(a) \
  { a, ALGO_DECRED, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, decred_regenhash, decred_midstate, decred_prepare_work, queue_decred_kernel, gen_hash, append_blake256_compiler_options }
  A_DECRED("decred"),
#undef A_DECRED

#define A_YESCRYPT(a) \
  { a, ALGO_YESCRYPT, "", 1, 65536, 65536, 0, 0, 0xFF, 0xFFFF000000000000ULL, 0x0000ffffUL, 0, -1, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, yescrypt_regenhash, NULL, NULL, queue_yescrypt_kernel, gen_hash, append_neoscrypt_compiler_options}
  A_YESCRYPT("yescrypt"),
#undef A_YESCRYPT

#define A_YESCRYPT_MULTI(a) \
  { a, ALGO_YESCRYPT_MULTI, "", 1, 65536, 65536, 0, 0, 0xFF, 0xFFFF000000000000ULL, 0x0000ffffUL, 6,-1,CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE , yescrypt_regenhash, NULL, NULL, queue_yescrypt_multikernel, gen_hash, append_neoscrypt_compiler_options}
  A_YESCRYPT_MULTI("yescrypt-multi"),
#undef A_YESCRYPT_MULTI

  // kernels starting from this will have difficulty calculated by using quarkcoin algorithm
#define A_QUARK(a, b) \
  { a, ALGO_QUARK, "", 256, 256, 256, 0, 0, 0xFF, 0xFFFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, b, NULL, NULL, queue_sph_kernel, gen_hash, append_x11_compiler_options }
  A_QUARK("quarkcoin", quarkcoin_regenhash),
  A_QUARK("qubitcoin", qubitcoin_regenhash),
  A_QUARK("animecoin", animecoin_regenhash),
  A_QUARK("sifcoin", sifcoin_regenhash),
#undef A_QUARK

  // kernels starting from this will have difficulty calculated by using bitcoin algorithm
#define A_DARK(a, b) \
  { a, ALGO_X11, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, b, NULL, NULL, queue_sph_kernel, gen_hash, append_x11_compiler_options }
  A_DARK("darkcoin", darkcoin_regenhash),
  A_DARK("sibcoin", sibcoin_regenhash),  
  A_DARK("inkcoin", inkcoin_regenhash),
  A_DARK("myriadcoin-groestl", myriadcoin_groestl_regenhash),
#undef A_DARK

  { "twecoin", ALGO_TWE, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, twecoin_regenhash, NULL, NULL, queue_sph_kernel, sha256, NULL },
  { "maxcoin", ALGO_KECCAK, "", 1, 256, 1, 4, 15, 0x0F, 0xFFFFULL, 0x000000ffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, maxcoin_regenhash, NULL, NULL, queue_maxcoin_kernel, sha256, NULL },

  { "darkcoin-mod", ALGO_X11, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 10, 8 * 16 * 4194304, 0, darkcoin_regenhash, NULL, NULL, queue_darkcoin_mod_kernel, gen_hash, append_x11_compiler_options },

  { "sibcoin-mod", ALGO_X11, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 11, 2 * 16 * 4194304, 0, sibcoin_regenhash, NULL, NULL, queue_sibcoin_mod_kernel, gen_hash, append_x11_compiler_options },
  
  { "marucoin", ALGO_X13, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, marucoin_regenhash, NULL, NULL, queue_sph_kernel, gen_hash, append_x13_compiler_options },
  { "marucoin-mod", ALGO_X13, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 12, 8 * 16 * 4194304, 0, marucoin_regenhash, NULL, NULL, queue_marucoin_mod_kernel, gen_hash, append_x13_compiler_options },
  { "marucoin-modold", ALGO_X13, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 10, 8 * 16 * 4194304, 0, marucoin_regenhash, NULL, NULL, queue_marucoin_mod_old_kernel, gen_hash, append_x13_compiler_options },

  { "x14", ALGO_X14, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 13, 8 * 16 * 4194304, 0, x14_regenhash, NULL, NULL, queue_x14_kernel, gen_hash, append_x13_compiler_options },
  { "x14old", ALGO_X14, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 10, 8 * 16 * 4194304, 0, x14_regenhash, NULL, NULL, queue_x14_old_kernel, gen_hash, append_x13_compiler_options },

  { "bitblock", ALGO_X15, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 14, 4 * 16 * 4194304, 0, bitblock_regenhash, NULL, NULL, queue_bitblock_kernel, gen_hash, append_x13_compiler_options },
  { "bitblockold", ALGO_X15, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 10, 4 * 16 * 4194304, 0, bitblock_regenhash, NULL, NULL, queue_bitblockold_kernel, gen_hash, append_x13_compiler_options },

  { "argon2d",ALGO_ARGON2D,"",1,65536,65536,0,0,0xFF,0xFFFFULL,0x0000ffffUL,2,-1, 0 ,argon2d_regenhash,NULL,NULL,queue_argon2d_kernel,gen_hash, NULL },

  { "x22i", ALGO_X22I, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 23, 4 * 16 * 4194304, 0, x22i_regenhash, NULL, NULL, queue_x22i_kernel, gen_hash, append_x13_compiler_options },
  { "x25x", ALGO_X25X, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 27, 4 * 16 * 4194304, 0, x25x_regenhash, NULL, NULL, queue_x25x_kernel, gen_hash, append_x13_compiler_options },

  { "talkcoin-mod", ALGO_NIST, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 4, 8 * 16 * 4194304, 0, talkcoin_regenhash, NULL, NULL, queue_talkcoin_mod_kernel, gen_hash, append_x11_compiler_options },

  { "phi", ALGO_PHI, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 5, 8 * 16 * 4194304, 0, phi_regenhash, NULL, NULL, queue_phi_kernel, gen_hash, append_x11_compiler_options },
  { "phi2", ALGO_PHI2, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 11, 8 * 16 * 4194304, 0, phi2_regenhash, NULL, NULL, queue_phi2_kernel, gen_hash, append_x11_compiler_options },

  { "fresh", ALGO_FRESH, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 4, 4 * 16 * 4194304, 0, fresh_regenhash, NULL, NULL, queue_fresh_kernel, gen_hash, NULL },

  { "lyra2re", ALGO_LYRA2RE, "", 1, 128, 128, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 4, 2 * 8 * 4194304, 0, lyra2re_regenhash, blake256_midstate, blake256_prepare_work, queue_lyra2re_kernel, gen_hash, NULL },
  { "lyra2rev2", ALGO_LYRA2REV2, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 8, -1, 0, lyra2rev2_regenhash, blake256_midstate, blake256_prepare_work, queue_lyra2rev2_kernel, gen_hash, append_neoscrypt_compiler_options },
  { "lyra2rev3", ALGO_LYRA2REV3, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 8, -1, 0, lyra2rev3_regenhash, blake256_midstate, blake256_prepare_work, queue_lyra2rev3_kernel, gen_hash, append_neoscrypt_compiler_options },
  { "lyra2Z"   , ALGO_LYRA2Z   , "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 4, -1, 0, lyra2Z_regenhash,  blake256_midstate, blake256_prepare_work, queue_lyra2z_kernel, gen_hash, NULL },
  { "lyra2Zz"   , ALGO_LYRA2ZZ   , "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 4, -1, 0, lyra2Zz_regenhash,  blake256_midstate_112, blake256_prepare_work_112, queue_lyra2zz_kernel, gen_hash, NULL },
  { "lyra2h"   , ALGO_LYRA2H   , "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 1, -1, 0, lyra2h_regenhash,  blake256_midstate, blake256_prepare_work, queue_lyra2h_kernel, gen_hash, NULL },
  { "allium", ALGO_ALLIUM, "", 1, 128, 128, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 10, 2 * 8 * 4194304, 0, allium_regenhash, blake256_midstate, blake256_prepare_work, queue_allium_kernel, gen_hash, NULL },
  { "mtp"   , ALGO_MTP   , "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 1, 0,0, mtp_regenhash   , NULL, NULL, queue_mtp_kernel   , gen_hash, NULL },
  { "mtp_vega"   , ALGO_MTP   , "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 1, 0,0, mtp_regenhash   , NULL, NULL, queue_mtp_kernel   , gen_hash, NULL },

  // kernels starting from this will have difficulty calculated by using fuguecoin algorithm
#define A_FUGUE(a, b, c) \
  { a, ALGO_FUGUE, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, b, NULL, NULL, queue_sph_kernel, c, NULL }
  A_FUGUE("fuguecoin", fuguecoin_regenhash, sha256),
  A_FUGUE("groestlcoin", groestlcoin_regenhash, sha256),
  A_FUGUE("diamond", groestlcoin_regenhash, gen_hash),
#undef A_FUGUE

  { "whirlcoin", ALGO_WHIRL, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 3, 8 * 16 * 4194304, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, whirlcoin_regenhash, NULL, NULL, queue_whirlcoin_kernel, sha256, NULL },
  { "whirlpoolx", ALGO_WHIRLPOOLX, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000FFFFUL, 0, 0, 0, whirlpoolx_regenhash, NULL, NULL, queue_whirlpoolx_kernel, gen_hash, NULL },

  { "blake256r8",  ALGO_BLAKECOIN, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x000000ffUL, 0, 128, 0, blakecoin_regenhash, blakecoin_midstate, blakecoin_prepare_work, queue_blake_kernel, sha256,   NULL },
  { "blake256r14", ALGO_BLAKE,     "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x00000000UL, 0, 128, 0, blake256_regenhash, blake256_midstate, blake256_prepare_work, queue_blake_kernel, gen_hash, NULL },
  { "sia",         ALGO_SIA,       "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000FFFFUL, 0, 128, 0, sia_regenhash, NULL, NULL, queue_sia_kernel, NULL, NULL },
  { "vanilla",     ALGO_VANILLA,   "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x000000ffUL, 0, 128, 0, blakecoin_regenhash, blakecoin_midstate, blakecoin_prepare_work, queue_blake_kernel, gen_hash, NULL },
  { "ethash",        ALGO_ETHASH,   "", 0x100010001LLU, 0x100010001LLU, 0x100010001LLU, 0, 0, 0xFF, 0xFFFF000000000000ULL, 72UL, 0, 128, 0, ethash_regenhash, NULL, NULL, queue_ethash_kernel, gen_hash, append_ethash_compiler_options },

  { "lbry", ALGO_LBRY, "", 1, 256, 256, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 2, 4 * 8 * 4194304, 0, lbry_regenhash, NULL, NULL, queue_lbry_kernel, gen_hash, NULL },

  { "pascal", ALGO_PASCAL, "", 1, 1, 1, 0, 0, 0xFF, 0xFFFFULL, 0x0000ffffUL, 0, 0, CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, pascal_regenhash, pascal_midstate, NULL, queue_pascal_kernel, NULL, NULL },

  // Terminator (do not remove)
  { NULL, ALGO_UNK, "", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL }
};

void copy_algorithm_settings(algorithm_t* dest, const char* algo)
{
  algorithm_settings_t* src;

  // Find algorithm settings and copy
  for (src = algos; src->name; src++)
  {
    if (strcasecmp(src->name, algo) == 0)
    {
      strcpy(dest->name, src->name);
      dest->kernelfile = src->kernelfile;
      dest->type = src->type;

      dest->diff_multiplier1 = src->diff_multiplier1;
      dest->diff_multiplier2 = src->diff_multiplier2;
      dest->share_diff_multiplier = src->share_diff_multiplier;
      dest->xintensity_shift = src->xintensity_shift;
      dest->intensity_shift = src->intensity_shift;
      dest->found_idx = src->found_idx;
      dest->diff_numerator = src->diff_numerator;
      dest->diff1targ = src->diff1targ;
      dest->n_extra_kernels = src->n_extra_kernels;
      dest->rw_buffer_size = src->rw_buffer_size;
      dest->cq_properties = src->cq_properties;
      dest->regenhash = src->regenhash;
      dest->calc_midstate = src->calc_midstate;
      dest->prepare_work = src->prepare_work;
      dest->queue_kernel = src->queue_kernel;
      dest->gen_hash = src->gen_hash;
      dest->set_compile_options = src->set_compile_options;
      break;
    }
  }

  // if not found
  if (src->name == NULL)
  {
    applog(LOG_WARNING, "Algorithm %s not found, using %s.", algo, algos->name);
    copy_algorithm_settings(dest, algos->name);
  }
}

static const char *lookup_algorithm_alias(const char *lookup_alias, uint8_t *nfactor)
{
#define ALGO_ALIAS_NF(alias, name, nf) \
  if (strcasecmp(alias, lookup_alias) == 0) { *nfactor = nf; return name; }
#define ALGO_ALIAS(alias, name) \
  if (strcasecmp(alias, lookup_alias) == 0) return name;

  ALGO_ALIAS_NF("scrypt", "ckolivas", 10);
  ALGO_ALIAS_NF("scrypt", "ckolivas", 10);
  ALGO_ALIAS_NF("adaptive-n-factor", "ckolivas", 11);
  ALGO_ALIAS_NF("adaptive-nfactor", "ckolivas", 11);
  ALGO_ALIAS_NF("nscrypt", "ckolivas", 11);
  ALGO_ALIAS_NF("adaptive-nscrypt", "ckolivas", 11);
  ALGO_ALIAS_NF("adaptive-n-scrypt", "ckolivas", 11);
  ALGO_ALIAS("x11mod", "darkcoin-mod");
  ALGO_ALIAS("x11", "darkcoin-mod");
  ALGO_ALIAS("x11-gost", "sibcoin-mod");
  ALGO_ALIAS("x13mod", "marucoin-mod");
  ALGO_ALIAS("x13", "marucoin-mod");
  ALGO_ALIAS("x13old", "marucoin-modold");
  ALGO_ALIAS("x13modold", "marucoin-modold");
  ALGO_ALIAS("x15mod", "bitblock");
  ALGO_ALIAS("x15", "bitblock");
  ALGO_ALIAS("x15modold", "bitblockold");
  ALGO_ALIAS("x15old", "bitblockold");
  ALGO_ALIAS("nist5", "talkcoin-mod");
  ALGO_ALIAS("keccak", "maxcoin");
  ALGO_ALIAS("whirlpool", "whirlcoin");
  ALGO_ALIAS("lyra2", "lyra2re");
  ALGO_ALIAS("lyra2v2", "lyra2rev2");
  ALGO_ALIAS("lyra2v3", "lyra2rev3");
  ALGO_ALIAS("blakecoin", "blake256r8");
  ALGO_ALIAS("blake", "blake256r14");

#undef ALGO_ALIAS
#undef ALGO_ALIAS_NF

  return NULL;
}

void set_algorithm(algorithm_t* algo, const char* newname_alias)
{
  const char *newname;

  //load previous algorithm nfactor in case nfactor was applied before algorithm... or default to 10
  uint8_t old_nfactor = ((algo->nfactor) ? algo->nfactor : 0);
  //load previous kernel file name if was applied before algorithm...
  const char *kernelfile = algo->kernelfile;
  uint8_t nfactor = 10;

  if (!(newname = lookup_algorithm_alias(newname_alias, &nfactor)))
    newname = newname_alias;

  copy_algorithm_settings(algo, newname);

  // use old nfactor if it was previously set and is different than the one set by alias
  if ((old_nfactor > 0) && (old_nfactor != nfactor))
    nfactor = old_nfactor;

  set_algorithm_nfactor(algo, nfactor);

  //reapply kernelfile if was set
  if (!empty_string(kernelfile)) {
    algo->kernelfile = kernelfile;
  }
}

void set_algorithm_nfactor(algorithm_t* algo, const uint8_t nfactor)
{
  algo->nfactor = nfactor;
  algo->n = (1 << nfactor);

  //adjust algo type accordingly
  switch (algo->type)
  {
  case ALGO_SCRYPT:
    //if nfactor isnt 10, switch to NSCRYPT
    if (algo->nfactor != 10)
      algo->type = ALGO_NSCRYPT;
    break;
    //nscrypt
  case ALGO_NSCRYPT:
    //if nfactor is 10, switch to SCRYPT
    if (algo->nfactor == 10)
      algo->type = ALGO_SCRYPT;
    break;
    //ignore rest
  default:
    break;
  }
}

bool cmp_algorithm(const algorithm_t* algo1, const algorithm_t* algo2)
{
  return (!safe_cmp(algo1->name, algo2->name) && !safe_cmp(algo1->kernelfile, algo2->kernelfile) && (algo1->nfactor == algo2->nfactor));
}
