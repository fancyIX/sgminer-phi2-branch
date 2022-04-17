/*-
 * Copyright 2015 djm34 
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
 */

#include "config.h"
#include "miner.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "algorithm/yescrypt_core.h"

static const uint32_t diff1targ = 0x0000ffff;

/* Used externally as confirmation of correct OCL code */
int yescrypt_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	uint32_t data[20], ohash[8];

	be32enc_vect(data, (const uint32_t *)pdata, 19);
	data[19] = htobe32(nonce);
	yescrypt_hash((unsigned char*)data,(unsigned char*)ohash);

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

void yescrypt_regenhash(struct work *work)
{
        uint32_t data[20];
        uint32_t *nonce = (uint32_t *)(work->data + 76);
        uint32_t *ohash = (uint32_t *)(work->hash);

        be32enc_vect(data, (const uint32_t *)work->data, 19);
        data[19] = htobe32(*nonce);	

		yescrypt_hash((unsigned char*)data, (unsigned char*)ohash);
        
}

int yescryptr16_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce)
{
	uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
	uint32_t data[20], ohash[8];

	be32enc_vect(data, (const uint32_t *)pdata, 19);
	data[19] = htobe32(nonce);
	yescryptr16_hash((unsigned char*)data,(unsigned char*)ohash);

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

void yescryptr16_regenhash(struct work *work)
{
        uint32_t data[20];
        uint32_t *nonce = (uint32_t *)(work->data + 76);
        uint32_t *ohash = (uint32_t *)(work->hash);

        be32enc_vect(data, (const uint32_t *)work->data, 19);
        data[19] = htobe32(*nonce);	

		yescryptr16_hash((unsigned char*)data, (unsigned char*)ohash);
#if 1
        memcpy(work->hash, work->target, 32);
		work->hash[0] = 0;
		work->hash[1] = 0;
#endif
        
}

bool scanhash_yescrypt(struct thr_info *thr, const unsigned char __maybe_unused *pmidstate,
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
	
	while (1)
	{
		uint32_t ostate[8];

		*nonce = ++n;
		data[19] = (n);

		yescrypt_hash((unsigned char*)data, (unsigned char*)ostate);
		tmp_hash7 = (ostate[7]);

		applog(LOG_INFO, "data7 %08lx", (long unsigned int)data[7]);

		if (unlikely(tmp_hash7 <= Htarg))
		{
			((uint32_t *)pdata)[19] = htobe32(n);
			*last_nonce = n;
			ret = true;
			break;
		}

		if (unlikely((n >= max_nonce) || thr->work_restart))
		{
			*last_nonce = n;
			break;
		}
	}

	return ret;
}

#define SPH_T32(x) (x)
#define ROTR32(x, n)      ((x >> n) | (x << (32 - n)))

static const uint32_t cpu_K[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

static void sha256_step1_host(uint32_t a, uint32_t b, uint32_t c, uint32_t *pd,
	uint32_t e, uint32_t f, uint32_t g, uint32_t *ph,
	uint32_t in, const uint32_t Kshared)
{
	uint32_t t1, t2;
	uint32_t vxandx = (((f) ^ (g)) & (e)) ^ (g); // xandx(e, f, g);
	uint32_t bsg21 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25); // bsg2_1(e);
	uint32_t bsg20 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22); //bsg2_0(a);
	uint32_t andorv = ((b) & (c)) | (((b) | (c)) & (a)); //andor32(a,b,c);

	t1 = *ph + bsg21 + vxandx + Kshared + in;
	t2 = bsg20 + andorv;
	*pd = *pd + t1;
	*ph = t1 + t2;
}

static void sha256_step2_host(uint32_t a, uint32_t b, uint32_t c, uint32_t *pd,
	uint32_t e, uint32_t f, uint32_t g, uint32_t *ph,
	uint32_t* in, uint32_t pc, const uint32_t Kshared)
{
	uint32_t t1, t2;

	int pcidx1 = (pc - 2) & 0xF;
	int pcidx2 = (pc - 7) & 0xF;
	int pcidx3 = (pc - 15) & 0xF;

	uint32_t inx0 = in[pc];
	uint32_t inx1 = in[pcidx1];
	uint32_t inx2 = in[pcidx2];
	uint32_t inx3 = in[pcidx3];

	uint32_t ssg21 = ROTR32(inx1, 17) ^ ROTR32(inx1, 19) ^ SPH_T32((inx1) >> 10); //ssg2_1(inx1);
	uint32_t ssg20 = ROTR32(inx3, 7) ^ ROTR32(inx3, 18) ^ SPH_T32((inx3) >> 3); //ssg2_0(inx3);
	uint32_t vxandx = (((f) ^ (g)) & (e)) ^ (g); // xandx(e, f, g);
	uint32_t bsg21 = ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25); // bsg2_1(e);
	uint32_t bsg20 = ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22); //bsg2_0(a);
	uint32_t andorv = ((b) & (c)) | (((b) | (c)) & (a)); //andor32(a,b,c);

	in[pc] = ssg21 + inx2 + ssg20 + inx0;

	t1 = *ph + bsg21 + vxandx + Kshared + in[pc];
	t2 = bsg20 + andorv;
	*pd = *pd + t1;
	*ph = t1 + t2;
}

void sha256_round_body_host(uint32_t* in, uint32_t* state)
{
	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t e = state[4];
	uint32_t f = state[5];
	uint32_t g = state[6];
	uint32_t h = state[7];

	sha256_step1_host(a, b, c, &d, e, f, g, &h, in[0], cpu_K[0]);
	sha256_step1_host(h, a, b, &c, d, e, f, &g, in[1], cpu_K[1]);
	sha256_step1_host(g, h, a, &b, c, d, e, &f, in[2], cpu_K[2]);
	sha256_step1_host(f, g, h, &a, b, c, d, &e, in[3], cpu_K[3]);
	sha256_step1_host(e, f, g, &h, a, b, c, &d, in[4], cpu_K[4]);
	sha256_step1_host(d, e, f, &g, h, a, b, &c, in[5], cpu_K[5]);
	sha256_step1_host(c, d, e, &f, g, h, a, &b, in[6], cpu_K[6]);
	sha256_step1_host(b, c, d, &e, f, g, h, &a, in[7], cpu_K[7]);
	sha256_step1_host(a, b, c, &d, e, f, g, &h, in[8], cpu_K[8]);
	sha256_step1_host(h, a, b, &c, d, e, f, &g, in[9], cpu_K[9]);
	sha256_step1_host(g, h, a, &b, c, d, e, &f, in[10], cpu_K[10]);
	sha256_step1_host(f, g, h, &a, b, c, d, &e, in[11], cpu_K[11]);
	sha256_step1_host(e, f, g, &h, a, b, c, &d, in[12], cpu_K[12]);
	sha256_step1_host(d, e, f, &g, h, a, b, &c, in[13], cpu_K[13]);
	sha256_step1_host(c, d, e, &f, g, h, a, &b, in[14], cpu_K[14]);
	sha256_step1_host(b, c, d, &e, f, g, h, &a, in[15], cpu_K[15]);

	for (int i = 0; i < 3; i++)
	{
		sha256_step2_host(a, b, c, &d, e, f, g, &h, in, 0, cpu_K[16 + 16 * i]);
		sha256_step2_host(h, a, b, &c, d, e, f, &g, in, 1, cpu_K[17 + 16 * i]);
		sha256_step2_host(g, h, a, &b, c, d, e, &f, in, 2, cpu_K[18 + 16 * i]);
		sha256_step2_host(f, g, h, &a, b, c, d, &e, in, 3, cpu_K[19 + 16 * i]);
		sha256_step2_host(e, f, g, &h, a, b, c, &d, in, 4, cpu_K[20 + 16 * i]);
		sha256_step2_host(d, e, f, &g, h, a, b, &c, in, 5, cpu_K[21 + 16 * i]);
		sha256_step2_host(c, d, e, &f, g, h, a, &b, in, 6, cpu_K[22 + 16 * i]);
		sha256_step2_host(b, c, d, &e, f, g, h, &a, in, 7, cpu_K[23 + 16 * i]);
		sha256_step2_host(a, b, c, &d, e, f, g, &h, in, 8, cpu_K[24 + 16 * i]);
		sha256_step2_host(h, a, b, &c, d, e, f, &g, in, 9, cpu_K[25 + 16 * i]);
		sha256_step2_host(g, h, a, &b, c, d, e, &f, in, 10, cpu_K[26 + 16 * i]);
		sha256_step2_host(f, g, h, &a, b, c, d, &e, in, 11, cpu_K[27 + 16 * i]);
		sha256_step2_host(e, f, g, &h, a, b, c, &d, in, 12, cpu_K[28 + 16 * i]);
		sha256_step2_host(d, e, f, &g, h, a, b, &c, in, 13, cpu_K[29 + 16 * i]);
		sha256_step2_host(c, d, e, &f, g, h, a, &b, in, 14, cpu_K[30 + 16 * i]);
		sha256_step2_host(b, c, d, &e, f, g, h, &a, in, 15, cpu_K[31 + 16 * i]);
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
	state[5] += f;
	state[6] += g;
	state[7] += h;
}
