#include "config.h"
#include "miner.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "sph/sph_sha2.h"
#include "sph/sph_ripemd.h"

typedef struct {
  sph_sha256_context  sha256;
  sph_sha512_context  sha512;
  sph_ripemd160_context  ripemd;
} lbryhash_context_holder;

void lbryhash(void* output, const void* input)
{
  uint32_t hashA[16], hashB[16], hashC[16];
  lbryhash_context_holder ctx;

  sph_sha256_init(&ctx.sha256);
  sph_sha512_init(&ctx.sha512);
  sph_ripemd160_init(&ctx.ripemd);

  sph_sha256 (&ctx.sha256, input, 112);
  sph_sha256_close(&ctx.sha256, hashA);

  sph_sha256 (&ctx.sha256, hashA, 32);
  sph_sha256_close(&ctx.sha256, hashA);

  sph_sha512 (&ctx.sha512, hashA, 32);
  sph_sha512_close(&ctx.sha512, hashA);

  sph_ripemd160 (&ctx.ripemd, hashA, 32);
  sph_ripemd160_close(&ctx.ripemd, hashB);

  sph_ripemd160 (&ctx.ripemd, hashA+8, 32);
  sph_ripemd160_close(&ctx.ripemd, hashC);

  sph_sha256 (&ctx.sha256, hashB, 20);
  sph_sha256 (&ctx.sha256, hashC, 20);
  sph_sha256_close(&ctx.sha256, hashA);

  sph_sha256 (&ctx.sha256, hashA, 32);
  sph_sha256_close(&ctx.sha256, hashA);

  memcpy(output, hashA, 32);
}

void lbry_regenhash(struct work *work)
{
  uint32_t data[28];
  uint32_t *nonce = (uint32_t *)(work->data + 108);
  uint32_t *ohash = (uint32_t *)(work->hash);

  be32enc_vect(data, (const uint32_t *)work->data, 27);
  data[27] = htobe32(*nonce);
  lbryhash(ohash, data);
}
