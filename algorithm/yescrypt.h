#ifndef YESCRYPT_H
#define YESCRYPT_H

#include "miner.h"
#define YESCRYPT_NAVI_SCRATCHBUF_SIZE (2 * 2048 * 8 * 4 + 2 * 16 * 8 * 4 + 512 * 4 + 8 * 4 + 32) //uchar
#define YESCRYPT_SCRATCHBUF_SIZE (128 * 2048 * 8 ) //uchar
#define YESCRYP_SECBUF_SIZE (128*64*8)
extern int yescrypt_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce);
extern void yescrypt_regenhash(struct work *work);

void sha256_round_body_host(uint32_t* in, uint32_t* state);

#endif /* YESCRYPT_H */
