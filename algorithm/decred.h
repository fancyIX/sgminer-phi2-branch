#ifndef DECRED_H
#define DECRED_H

#include "miner.h"

extern int decred_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce);
extern void decred_prepare_work(dev_blk_ctx *blk, uint32_t *state, uint32_t *pdata);
extern void decred_midstate(struct work *work);
extern void decred_regenhash(struct work *work);

#endif /* DECRED_H */