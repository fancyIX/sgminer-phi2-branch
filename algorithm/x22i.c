/*-
 * Copyright 2009 Colin Percival, 2011 ArtForz
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 * 
 * @author ccminer-x22i 2018
 * @author fancyIX 2018
 */

#include "config.h"
#include "miner.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>


#include "sph/sph_blake.h"
#include "sph/sph_bmw.h"
#include "sph/sph_groestl.h"
#include "sph/sph_jh.h"
#include "sph/sph_keccak.h"
#include "sph/sph_skein.h"
#include "sph/sph_luffa.h"
#include "sph/sph_cubehash.h"
#include "sph/sph_shavite.h"
#include "sph/sph_simd.h"
#include "sph/sph_echo.h"
#include "sph/sph_hamsi.h"
#include "sph/sph_fugue.h"
#include "sph/sph_shabal.h"
#include "sph/sph_whirlpool.h"
#include "sph/sph_sha2.h"
#include "sph/sph_haval.h"
#include "sph/sph_tiger.h"
#include "lyra2.h"
#include "sph/sph_streebog.h"
#include "SWIFFTX/SWIFFTX.h"


void x22ihash(void *output, const void *input)
{
	//unsigned char _ALIGN(64) hash[128];
	unsigned char hash[64 * 4] = {0}, hash2[64] = {0};

	// x11 + hamsi12-fugue13-shabal14-whirlpool15-sha512-haval256

	sph_blake512_context ctx_blake;
	sph_bmw512_context ctx_bmw;
	sph_groestl512_context ctx_groestl;
	sph_jh512_context ctx_jh;
	sph_keccak512_context ctx_keccak;
	sph_skein512_context ctx_skein;
	sph_luffa512_context ctx_luffa;
	sph_cubehash512_context ctx_cubehash;
	sph_shavite512_context ctx_shavite;
	sph_simd512_context ctx_simd;
	sph_echo512_context ctx_echo;
	sph_hamsi512_context ctx_hamsi;
	sph_fugue512_context ctx_fugue;
	sph_shabal512_context ctx_shabal;
	sph_whirlpool_context ctx_whirlpool;
	sph_sha512_context ctx_sha512;
	sph_haval256_5_context ctx_haval;
	sph_tiger_context         ctx_tiger;
	sph_gost512_context       ctx_gost;
	sph_sha256_context        ctx_sha;

	sph_blake512_init(&ctx_blake);
	sph_blake512(&ctx_blake, input, 80);
	sph_blake512_close(&ctx_blake, hash);

	sph_bmw512_init(&ctx_bmw);
	sph_bmw512(&ctx_bmw, (const void*) hash, 64);
	sph_bmw512_close(&ctx_bmw, hash);

	sph_groestl512_init(&ctx_groestl);
	sph_groestl512(&ctx_groestl, (const void*) hash, 64);
	sph_groestl512_close(&ctx_groestl, hash);

	sph_skein512_init(&ctx_skein);
	sph_skein512(&ctx_skein, (const void*) hash, 64);
	sph_skein512_close(&ctx_skein, hash);

	sph_jh512_init(&ctx_jh);
	sph_jh512(&ctx_jh, (const void*) hash, 64);
	sph_jh512_close(&ctx_jh, hash);

	sph_keccak512_init(&ctx_keccak);
	sph_keccak512(&ctx_keccak, (const void*) hash, 64);
	sph_keccak512_close(&ctx_keccak, hash);

	sph_luffa512_init(&ctx_luffa);
	sph_luffa512(&ctx_luffa, (const void*) hash, 64);
	sph_luffa512_close (&ctx_luffa, hash);

	sph_cubehash512_init(&ctx_cubehash);
	sph_cubehash512(&ctx_cubehash, (const void*) hash, 64);
	sph_cubehash512_close(&ctx_cubehash, hash);

	sph_shavite512_init(&ctx_shavite);
	sph_shavite512(&ctx_shavite, (const void*) hash, 64);
	sph_shavite512_close(&ctx_shavite, hash);

	sph_simd512_init(&ctx_simd);
	sph_simd512(&ctx_simd, (const void*) hash, 64);
	sph_simd512_close(&ctx_simd, hash);

	sph_echo512_init(&ctx_echo);
	sph_echo512(&ctx_echo, (const void*) hash, 64);
	sph_echo512_close(&ctx_echo, hash);

	sph_hamsi512_init(&ctx_hamsi);
	sph_hamsi512(&ctx_hamsi, (const void*) hash, 64);
	sph_hamsi512_close(&ctx_hamsi, hash);

	sph_fugue512_init(&ctx_fugue);
	sph_fugue512(&ctx_fugue, (const void*) hash, 64);
	sph_fugue512_close(&ctx_fugue, hash);

	sph_shabal512_init(&ctx_shabal);
	sph_shabal512(&ctx_shabal, (const void*) hash, 64);
	sph_shabal512_close(&ctx_shabal, &hash[64]);

	sph_whirlpool_init(&ctx_whirlpool);
	sph_whirlpool (&ctx_whirlpool, (const void*) &hash[64], 64);
	sph_whirlpool_close(&ctx_whirlpool, &hash[128]);

	sph_sha512_init(&ctx_sha512);
	sph_sha512(&ctx_sha512,(const void*) &hash[128], 64);
	sph_sha512_close(&ctx_sha512,(void*) &hash[192]);

	InitializeSWIFFTX();
	ComputeSingleSWIFFTX((unsigned char*)hash, (unsigned char*)hash2, false);

	memset(hash, 0, 64);
	sph_haval256_5_init(&ctx_haval);
	sph_haval256_5(&ctx_haval,(const void*) hash2, 64);
	sph_haval256_5_close(&ctx_haval,hash);

	memset(hash2, 0, 64);
	sph_tiger_init(&ctx_tiger);
	sph_tiger (&ctx_tiger, (const void*) hash, 64);
	sph_tiger_close(&ctx_tiger, (void*) hash2);

	memset(hash, 0, 64);
	LYRA2((void*) hash, 32, (const void*) hash2, 32, (const void*) hash2, 32, 1, 4, 4);

	sph_gost512_init(&ctx_gost);
	sph_gost512 (&ctx_gost, (const void*) hash, 64);
	sph_gost512_close(&ctx_gost, (void*) hash);

	sph_sha256_init(&ctx_sha);
	sph_sha256 (&ctx_sha, (const void*) hash, 64);
	sph_sha256_close(&ctx_sha, (void*) hash);

	/*
  // zero hash test print
  printf("%lu\n", ((uint64_t*)(hash))[0]);
  */

	memcpy(output, hash, 32);
}

static const uint32_t diff1targ = 0x0000ffff;


/* Used externally as confirmation of correct OCL code */
int x22i_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
  uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
  uint32_t data[20], ohash[8];

  be32enc_vect(data, (const uint32_t *)pdata, 19);
  data[19] = htobe32(nonce);
  x22ihash(ohash, data);
  tmp_hash7 = be32toh(ohash[7]);

  applog(LOG_DEBUG, "htarget %08lx diff1 %08lx hash %08lx",
        (long unsigned int)Htarg,
        (long unsigned int)diff1targ,
        (long unsigned int)tmp_hash7);
  if (tmp_hash7 > diff1targ)
    return -1;
  if (tmp_hash7 > Htarg)
    return 0;
  return 1;
}

void x22i_regenhash(struct work *work)
{
  uint32_t data[20];
  uint32_t *nonce = (uint32_t *)(work->data + 76);
  uint32_t *ohash = (uint32_t *)(work->hash);

  be32enc_vect(data, (const uint32_t *)work->data, 19);
  data[19] = htobe32(*nonce);
  x22ihash(ohash, data);
}

bool scanhash_x22i(struct thr_info *thr, const unsigned char __maybe_unused *pmidstate,
         unsigned char *pdata, unsigned char __maybe_unused *phash1,
         unsigned char __maybe_unused *phash, const unsigned char *ptarget,
         uint32_t max_nonce, uint32_t *last_nonce, uint32_t n)
{
  uint32_t *nonce = (uint32_t *)(pdata + 76);
  uint32_t data[20];
  uint32_t tmp_hash7;
  uint32_t Htarg = le32toh(((const uint32_t *)ptarget)[7]);
  bool ret = false;

  be32enc_vect(data, (const uint32_t *)pdata, 19);

  while(1) {
    uint32_t ostate[8];
    *nonce = ++n;
    data[19] = (n);
    x22ihash(ostate, data);
    tmp_hash7 = (ostate[7]);

    applog(LOG_INFO, "data7 %08lx",
          (long unsigned int)data[7]);

    if (unlikely(tmp_hash7 <= Htarg)) {
      ((uint32_t *)pdata)[19] = htobe32(n);
      *last_nonce = n;
      ret = true;
      break;
    }

    if (unlikely((n >= max_nonce) || thr->work_restart)) {
      *last_nonce = n;
      break;
    }
  }

  return ret;
}