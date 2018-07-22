/*
 * BLAKE implementation.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2007-2010  Projet RNRT SAPHIR
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
 * @author   Thomas Pornin <thomas.pornin@cryptolog.com>
 *
 * Modified for more speed by BlueDragon747 for the Blakecoin project
 */

#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

#include "sph/sph_blake.h"
#include "algorithm/decred.h"

static const uint32_t diff1targ_decred = 0x000000ff;

void decredhash(void *state, const void *input)
{
  sph_blake256_context ctx_blake;
  sph_blake256_init(&ctx_blake);
  sph_blake256(&ctx_blake, input, 180);
  sph_blake256_close(&ctx_blake, state);
}

void decred_midstate(struct work *work)
{
  sph_blake256_context     ctx_blake;
  sph_blake256_init(&ctx_blake);
  sph_blake256 (&ctx_blake, (unsigned char *)work->data, 128);

  memcpy(work->midstate, ctx_blake.H, 32);
  endian_flip32(work->midstate, work->midstate);

  char *strdata, *strmidstate;
  strdata = bin2hex(work->data, 128);
  strmidstate = bin2hex(work->midstate, 32);
  applog(LOG_DEBUG, "data %s midstate %s", strdata, strmidstate);
}

void decred_prepare_work(dev_blk_ctx *blk, uint32_t *state, uint32_t *pdata)
{
	blk->ctx_a = state[0];
	blk->ctx_b = state[1];
	blk->ctx_c = state[2];
	blk->ctx_d = state[3];
	blk->ctx_e = state[4];
	blk->ctx_f = state[5];
	blk->ctx_g = state[6];
	blk->ctx_h = state[7];

	blk->cty_a = htobe32(pdata[32]);
	blk->cty_b = htobe32(pdata[33]);
	blk->cty_c = htobe32(pdata[34]);
	/* blk->cty_d = htobe32(pdata[35] = nonce) */

	blk->cty_d = htobe32(pdata[36]);
	blk->cty_e = htobe32(pdata[37]);
	blk->cty_f = htobe32(pdata[38]);
	blk->cty_g = htobe32(pdata[39]);

	blk->cty_h = htobe32(pdata[40]);
	blk->cty_i = htobe32(pdata[41]);
	blk->cty_j = htobe32(pdata[42]);
	blk->cty_k = htobe32(pdata[43]);

	blk->cty_l = htobe32(pdata[44]);
}


static const uint32_t diff1targ = 0x0000ffff;

/* Used externally as confirmation of correct OCL code */
int decred_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	uint32_t data[45], ohash[8];

	memcpy(data, pdata, 180);
	data[35] = htobe32(nonce);
	decredhash(ohash, data);
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

void decred_regenhash(struct work *work)
{
        uint32_t data[45];
        uint32_t *nonce = (uint32_t *)(work->data + 140);
        uint32_t *ohash = (uint32_t *)(work->hash);

        memcpy(data, work->data, 180);
        data[35] = htobe32(*nonce);
        decredhash(ohash, data);
}

bool scanhash_decred(struct thr_info *thr, const unsigned char __maybe_unused *pmidstate,
		     unsigned char *pdata, unsigned char __maybe_unused *phash1,
		     unsigned char __maybe_unused *phash, const unsigned char *ptarget,
		     uint32_t max_nonce, uint32_t *last_nonce, uint32_t n)
{
	uint32_t *nonce = (uint32_t *)(pdata + 140);
	uint32_t data[45];
	uint32_t tmp_hash7;
	uint32_t Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	bool ret = false;

	memcpy(data, pdata, 180);

	while(1) {
		uint32_t ostate[8];

		*nonce = ++n;
		data[35] = (n);
		decredhash(ostate, data);
		tmp_hash7 = (ostate[7]);

		applog(LOG_INFO, "data7 %08lx",
					(long unsigned int)data[7]);

		if (unlikely(tmp_hash7 <= Htarg)) {
			((uint32_t *)pdata)[35] = htobe32(n);
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
