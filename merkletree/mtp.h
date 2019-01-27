//
//

#ifndef ZCOIN_MTP_H
#define ZCOIN_MTP_H

#endif //ZCOIN_MTP_H

#ifdef __APPLE_CC__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h> 
#endif


#include "merkle-tree.hpp"

#include <immintrin.h>
#include "mtp_argon2ref/mtp_core.h"
#include "mtp_argon2ref/mtp_argon2.h"
#include "mtp_argon2ref/mtp_thread.h"
#include "mtp_argon2ref/mtp_blake2.h"
#include "mtp_argon2ref/mtp_blake2-impl.h"
#include "mtp_argon2ref/mtp_blamka-round-opt.h"
//#include "merkletree/sha.h"

//#include "openssl\sha.h"

#include "uint256.h"

#ifdef __cplusplus
//#include "serialize.h"
class Cargon_block;

/* Size of MTP proof */
const unsigned int MTP_PROOF_SIZE = 1471;// 1431;
/* Size of MTP argon_block proof size */
const unsigned int MTP_argon_block_PROOF_SIZE = 64;
/* Size of MTP argon_block */
const unsigned int MTP_argon_block_SIZE = 140;

typedef struct argon_block_with_offset_ {
	argon_block memory;
	//	char* proof;
	char proof[MTP_PROOF_SIZE];
} argon_block_with_offset;

typedef struct argon_block_mtpProof_ {
	argon_block memory;
	char proof[MTP_PROOF_SIZE];
} argon_block_mtpProof;

typedef struct mtp_Proof_ {
	char proof[MTP_PROOF_SIZE]; 
} mtp_Proof;

void copy_argon_blockS(argon_blockS *dst, const argon_block *src);

void mtp_hash(char* output, const char* input, unsigned int d, uint32_t TheNonce);

extern "C"
#endif
mtp_argon2_context init_mtp_argon2d_param(const char* input);

#ifdef __cplusplus
void getargon_blockindex_orig(uint32_t ij, mtp_argon2_instance_t *instance, uint32_t *out_ij_prev, uint32_t *out_computed_ref_argon_block);

void getargon_blockindex(int thr_id, cl_command_queue Queue, cl_mem block, cl_mem block2, uint32_t ij, mtp_argon2_instance_t *instance, uint32_t *out_ij_prev, uint32_t *out_computed_ref_argon_block);


//int mtp_solver_withargon_block(uint32_t TheNonce, mtp_argon2_instance_t *instance, unsigned int d, argon_block_mtpProof *output,
// uint8_t *resultMerkleRoot, MerkleTree TheTree,uint32_t* input, uint256 hashTarget);

int mtp_solver_orig(uint32_t TheNonce, mtp_argon2_instance_t *instance,
	argon_blockS *nargon_blockMTP /*[72 * 2][128]*/, unsigned char *nProofMTP, unsigned char* resultMerkleRoot, unsigned char* mtpHashValue,
	MerkleTree TheTree, uint32_t* input, uint256 hashTarget);

int mtp_solver(int thr_id, cl_command_queue Queue, cl_mem clblock, cl_mem clblock2, uint32_t TheNonce, mtp_argon2_instance_t *instance,
	argon_blockS *nargon_blockMTP /*[72 * 2][128]*/, unsigned char *nProofMTP, unsigned char* resultMerkleRoot, unsigned char* mtpHashValue,
	MerkleTree TheTree, uint32_t* input, uint256 hashTarget);

extern "C"
#endif
int mtp_solver_c(int thr_id, cl_command_queue Queue, cl_mem clblock, cl_mem clblock2, uint32_t TheNonce, mtp_argon2_instance_t *instance,
	argon_blockS *nargon_blockMTP /*[72 * 2][128]*/, unsigned char *nProofMTP, unsigned char* resultMerkleRoot, unsigned char* mtpHashValue,
	MerkleTree* pTheTree, uint32_t* input, uint256* phashTarget);


#ifdef __cplusplus

MerkleTree::Elements mtp_init(mtp_argon2_instance_t *instance);
//MerkleTree::Elements mtp_init2(mtp_argon2_instance_t *instance);
//uint8_t *mtp_init3(mtp_argon2_instance_t *instance, int thr_id);
//void  mtp_init3(mtp_argon2_instance_t *instance, int thr_id, MerkleTree *TheTree);
//MerkleTree  mtp_init3(mtp_argon2_instance_t *instance, int thr_id);
void  mtp_init3(mtp_argon2_instance_t *instance, int thr_id, MerkleTree &ThatTree);

#endif