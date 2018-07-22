#ifndef PASCAL_H
#define PASCAL_H

#include "miner.h"

extern int pascal_test(unsigned char *pdata, const unsigned char *ptarget, uint32_t nonce);
extern void pascal_prepare_work(dev_blk_ctx *blk, uint32_t *state, uint32_t *pdata);
extern void pascal_midstate(struct work *work);
extern void pascal_regenhash(struct work *work);

#endif /* PASCAL_H */