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

#include "sph/sph_sha2.h"
#include "algorithm/pascal.h"

static const uint32_t diff1targ_pascal = 0x000000ff;

void pascalhash(void *state, const void *input)
{
  sph_sha256_context ctx_sha;
  uint32_t hash[16];

  sph_sha256_init(&ctx_sha);
  sph_sha256(&ctx_sha, input, 200);
  sph_sha256_close(&ctx_sha, hash);

  sph_sha256_init(&ctx_sha);
  sph_sha256(&ctx_sha, hash, 32);
  sph_sha256_close(&ctx_sha, hash);

  memcpy(state, hash, 32);

}

void pascal_midstate(struct work *work)
{
  sph_sha256_context     ctx_sha;
  uint32_t data[48];

  memcpy(data, work->data, 192);

  sph_sha256_init(&ctx_sha);
  sph_sha256 (&ctx_sha, (unsigned char *)data, 192);

  memcpy(work->midstate, ctx_sha.val, 32);
  endian_flip32(work->midstate, work->midstate);

/*
  char *strdata, *strmidstate;
  strdata = bin2hex(work->data, 192);
  strmidstate = bin2hex(work->midstate, 32);
  applog(LOG_DEBUG, "data %s midstate %s", strdata, strmidstate);
*/
}

static const uint32_t diff1targ = 0x0000ffff;

/* Used externally as confirmation of correct OCL code */
int pascal_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	uint32_t data[50], ohash[8];

	memcpy(data, pdata, 200);
	data[49] = htobe32(nonce);
	pascalhash(ohash, data);
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

void pascal_regenhash(struct work *work)
{
        uint32_t data[64];
        uint32_t hash[16];
        uint32_t *nonce = (uint32_t *)(work->data + 196);
        uint32_t *ohash = (uint32_t *)(work->hash);

        memcpy(data, work->data, 200);
        data[49] = htole32(*nonce);
        pascalhash(hash, data);
        swab256(ohash, hash);
}
