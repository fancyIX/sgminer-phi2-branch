#ifndef BLAKECOIN_H
#define BLAKECOIN_H

#include "miner.h"

extern int blakecoin_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce);
extern void blakecoin_prepare_work(dev_blk_ctx *blk, uint32_t *state, uint32_t *data);
extern void blakecoin_midstate(struct work *work);
extern void blakecoin_regenhash(struct work *work);

#endif /* BLAKECOIN_H */