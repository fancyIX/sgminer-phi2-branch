#ifndef ARGON2D_H
#define ARGON2D_H


#define AR2D_MEM_PER_BATCH 491520

#include "miner.h"

extern void argon2d_regenhash(struct work *work);

#endif /* ARGON2D_H */
