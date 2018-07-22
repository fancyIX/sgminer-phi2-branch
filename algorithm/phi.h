#ifndef PHI_H
#define PHI_H

#include "miner.h"

extern int phi_test(unsigned char *pdata, const unsigned char *ptarget,
			uint32_t nonce);
extern void phi_regenhash(struct work *work);

#endif /* PHI_H */
