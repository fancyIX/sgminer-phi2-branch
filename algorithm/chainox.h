#ifndef CHAINOX_H
#define CHAINOX_H

#include "miner.h"

extern int chainox_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void chainox_regenhash(struct work *work);

#endif /* CHAINOX_H */
