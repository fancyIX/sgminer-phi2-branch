/*
 * MTPArgon2 reference source code package - reference C implementations
 *
 * Copyright 2015
 * Daniel Dinu, Dmitry Khovratovich, Jean-Philippe Aumasson, and Samuel Neves
 *
 * You may use this work under the terms of a Creative Commons CC0 1.0 
 * License/Waiver or the Apache Public License 2.0, at your option. The terms of
 * these licenses can be found at:
 *
 * - CC0 1.0 Universal : http://creativecommons.org/publicdomain/zero/1.0
 * - Apache 2.0        : http://www.apache.org/licenses/LICENSE-2.0
 *
 * You should have received a copy of both of these licenses along with this
 * software. If not, they may be obtained at the above URLs.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "mtp_argon2ref/mtp_argon2.h"
#include "mtp_argon2ref/mtp_core.h"

#include "mtp_argon2ref/mtp_blamka-round-ref.h"
#include "mtp_argon2ref/mtp_blake2-impl.h"
#include "mtp_argon2ref/mtp_blake2.h"


/*
 * Function fills a new memory argon_block and optionally XORs the old argon_block over the new one.
 * @next_argon_block must be initialized.
 * @param prev_argon_block Pointer to the previous argon_block
 * @param ref_argon_block Pointer to the reference argon_block
 * @param next_argon_block Pointer to the argon_block to be constructed
 * @param with_xor Whether to XOR into the new argon_block (1) or just overwrite (0)
 * @pre all argon_block pointers must be valid
 */
void getargon_blockIndex(uint32_t ij, mtp_argon2_instance_t *instance, uint32_t *Index)
{
	uint32_t ij_prev = 0;
	if (ij%instance->lane_length == 0)
		ij_prev = ij + instance->lane_length - 1;
	else
		ij_prev = ij - 1;

	if (ij % instance->lane_length == 1)
		ij_prev = ij - 1;

	uint64_t prev_argon_block_opening = instance->memory[ij_prev].v[0];
	uint32_t ref_lane = (uint32_t)((prev_argon_block_opening >> 32) % instance->lanes);

	uint32_t pseudo_rand = (uint32_t)(prev_argon_block_opening & 0xFFFFFFFF);

	uint32_t Lane = ((ij) / instance->lane_length);
	uint32_t Slice = (ij - (Lane * instance->lane_length)) / instance->segment_length;
	uint32_t posIndex = ij - Lane * instance->lane_length - Slice * instance->segment_length;


	uint32_t rec_ij = Slice*instance->segment_length + Lane *instance->lane_length + (ij % instance->segment_length);

	if (Slice == 0)
		ref_lane = Lane;


	mtp_argon2_position_t position = { 0, Lane , (uint8_t)Slice, posIndex };

	uint32_t ref_index = mtp_index_alpha(instance, &position, pseudo_rand, ref_lane == position.lane);

	uint32_t computed_ref_argon_block = instance->lane_length * ref_lane + ref_index;

	Index[0] = ij_prev;
	Index[1] = computed_ref_argon_block;
}



static void fill_argon_block(const argon_block *prev_argon_block, const argon_block *ref_argon_block,
                       argon_block *next_argon_block, int with_xor, uint32_t argon_block_header[4]) {
    argon_block argon_blockR, argon_block_tmp;
    unsigned i;

    copy_argon_block(&argon_blockR, ref_argon_block);

    xor_argon_block(&argon_blockR, prev_argon_block);

    copy_argon_block(&argon_block_tmp, &argon_blockR);
    /* Now argon_blockR = ref_argon_block + prev_argon_block and argon_block_tmp = ref_argon_block + prev_argon_block */
    if (with_xor) {
        /* Saving the next argon_block contents for XOR over: */
        xor_argon_block(&argon_block_tmp, next_argon_block);
        /* Now argon_blockR = ref_argon_block + prev_argon_block and
           argon_block_tmp = ref_argon_block + prev_argon_block + next_argon_block */
    }
//	argon_blockR.v[16] = ((uint64_t*)argon_block_header)[0];
//	argon_blockR.v[17] = ((uint64_t*)argon_block_header)[1];
	memcpy(&argon_blockR.v[16], (uint64_t*)argon_block_header, 2 * sizeof(uint64_t));
//printf("argon_block header in cpu %llx %llx %llx %llx\n", argon_blockR.v[15], argon_blockR.v[16], argon_blockR.v[17], argon_blockR.v[18]);

    /* Apply Blake2 on columns of 64-bit words: (0,1,...,15) , then
       (16,17,..31)... finally (112,113,...127) */
    for (i = 0; i < 8; ++i) {
        MTP_BLAKE2_ROUND_NOMSG(
            argon_blockR.v[16 * i], argon_blockR.v[16 * i + 1], argon_blockR.v[16 * i + 2],
            argon_blockR.v[16 * i + 3], argon_blockR.v[16 * i + 4], argon_blockR.v[16 * i + 5],
            argon_blockR.v[16 * i + 6], argon_blockR.v[16 * i + 7], argon_blockR.v[16 * i + 8],
            argon_blockR.v[16 * i + 9], argon_blockR.v[16 * i + 10], argon_blockR.v[16 * i + 11],
            argon_blockR.v[16 * i + 12], argon_blockR.v[16 * i + 13], argon_blockR.v[16 * i + 14],
            argon_blockR.v[16 * i + 15]);
    }

    /* Apply Blake2 on rows of 64-bit words: (0,1,16,17,...112,113), then
       (2,3,18,19,...,114,115).. finally (14,15,30,31,...,126,127) */
    for (i = 0; i < 8; i++) {
        MTP_BLAKE2_ROUND_NOMSG(
            argon_blockR.v[2 * i], argon_blockR.v[2 * i + 1], argon_blockR.v[2 * i + 16],
            argon_blockR.v[2 * i + 17], argon_blockR.v[2 * i + 32], argon_blockR.v[2 * i + 33],
            argon_blockR.v[2 * i + 48], argon_blockR.v[2 * i + 49], argon_blockR.v[2 * i + 64],
            argon_blockR.v[2 * i + 65], argon_blockR.v[2 * i + 80], argon_blockR.v[2 * i + 81],
            argon_blockR.v[2 * i + 96], argon_blockR.v[2 * i + 97], argon_blockR.v[2 * i + 112],
            argon_blockR.v[2 * i + 113]);
    }

    copy_argon_block(next_argon_block, &argon_block_tmp);
    xor_argon_block(next_argon_block, &argon_blockR);
}

static void fill_argon_block_withIndex(const argon_block *prev_argon_block, const argon_block *ref_argon_block,
	argon_block *next_argon_block, int with_xor, uint32_t argon_block_header[8], uint32_t index) {
	argon_block argon_blockR, argon_block_tmp;
	uint32_t TheIndex[2] = {0,index};
	unsigned i;

	copy_argon_block(&argon_blockR, ref_argon_block);

	xor_argon_block(&argon_blockR, prev_argon_block);

	copy_argon_block(&argon_block_tmp, &argon_blockR);
	/* Now argon_blockR = ref_argon_block + prev_argon_block and argon_block_tmp = ref_argon_block + prev_argon_block */
	if (with_xor) {
		/* Saving the next argon_block contents for XOR over: */
		xor_argon_block(&argon_block_tmp, next_argon_block);
		/* Now argon_blockR = ref_argon_block + prev_argon_block and
		argon_block_tmp = ref_argon_block + prev_argon_block + next_argon_block */
	}
	//	argon_blockR.v[16] = ((uint64_t*)argon_block_header)[0];
	//	argon_blockR.v[17] = ((uint64_t*)argon_block_header)[1];
	memcpy(&argon_blockR.v[14], TheIndex,  sizeof(uint64_t)); //index here
	memcpy(&argon_blockR.v[16], (uint64_t*)argon_block_header, 2 * sizeof(uint64_t));
	memcpy(&argon_blockR.v[18], (uint64_t*)(argon_block_header + 4), 2 * sizeof(uint64_t));
	//printf("argon_block header in cpu %llx %llx %llx %llx\n", argon_blockR.v[15], argon_blockR.v[16], argon_blockR.v[17], argon_blockR.v[18]);

	/* Apply Blake2 on columns of 64-bit words: (0,1,...,15) , then
	(16,17,..31)... finally (112,113,...127) */
	for (i = 0; i < 8; ++i) {
		MTP_BLAKE2_ROUND_NOMSG(
			argon_blockR.v[16 * i], argon_blockR.v[16 * i + 1], argon_blockR.v[16 * i + 2],
			argon_blockR.v[16 * i + 3], argon_blockR.v[16 * i + 4], argon_blockR.v[16 * i + 5],
			argon_blockR.v[16 * i + 6], argon_blockR.v[16 * i + 7], argon_blockR.v[16 * i + 8],
			argon_blockR.v[16 * i + 9], argon_blockR.v[16 * i + 10], argon_blockR.v[16 * i + 11],
			argon_blockR.v[16 * i + 12], argon_blockR.v[16 * i + 13], argon_blockR.v[16 * i + 14],
			argon_blockR.v[16 * i + 15]);
	}

	/* Apply Blake2 on rows of 64-bit words: (0,1,16,17,...112,113), then
	(2,3,18,19,...,114,115).. finally (14,15,30,31,...,126,127) */
	for (i = 0; i < 8; i++) {
		MTP_BLAKE2_ROUND_NOMSG(
			argon_blockR.v[2 * i], argon_blockR.v[2 * i + 1], argon_blockR.v[2 * i + 16],
			argon_blockR.v[2 * i + 17], argon_blockR.v[2 * i + 32], argon_blockR.v[2 * i + 33],
			argon_blockR.v[2 * i + 48], argon_blockR.v[2 * i + 49], argon_blockR.v[2 * i + 64],
			argon_blockR.v[2 * i + 65], argon_blockR.v[2 * i + 80], argon_blockR.v[2 * i + 81],
			argon_blockR.v[2 * i + 96], argon_blockR.v[2 * i + 97], argon_blockR.v[2 * i + 112],
			argon_blockR.v[2 * i + 113]);
	}

	copy_argon_block(next_argon_block, &argon_block_tmp);
	xor_argon_block(next_argon_block, &argon_blockR);
}


static void next_addresses(argon_block *address_argon_block, argon_block *input_argon_block,
                           const argon_block *zero_argon_block, uint32_t argon_block_header[4]) {
    input_argon_block->v[6]++;
    fill_argon_block(zero_argon_block, input_argon_block, address_argon_block, 0, argon_block_header);
    fill_argon_block(zero_argon_block, address_argon_block, address_argon_block, 0, argon_block_header);
}

void mtp_fill_segment(const mtp_argon2_instance_t *instance,
                  mtp_argon2_position_t position) {
    argon_block *ref_argon_block = NULL, *curr_argon_block = NULL;
	uint32_t *zPrevargon_block = NULL, *zRefargon_block = NULL;
    argon_block address_argon_block, input_argon_block, zero_argon_block;
    uint64_t pseudo_rand, ref_index, ref_lane;
    uint32_t prev_offset, curr_offset;
    uint32_t starting_index;
    uint32_t i;
    int data_independent_addressing;

    if (instance == NULL) {
        return;
    }

    data_independent_addressing =
        (instance->type == MTPArgon2_i) ||
        (instance->type == MTPArgon2_id && (position.pass == 0) &&
         (position.slice < MTP_ARGON2_SYNC_POINTS / 2));


    if (data_independent_addressing) {
        init_argon_block_value(&zero_argon_block, 0);
        init_argon_block_value(&input_argon_block, 0);
 
        input_argon_block.v[0] = position.pass;
        input_argon_block.v[1] = position.lane;
        input_argon_block.v[2] = position.slice;
        input_argon_block.v[3] = instance->memory_argon_blocks;
        input_argon_block.v[4] = instance->passes;
        input_argon_block.v[5] = instance->type;
    }

    starting_index = 0;

    if ((0 == position.pass) && (0 == position.slice)) {
        starting_index = 2; /* we have already generated the first two argon_blocks */

        /* Don't forget to generate the first argon_block of addresses: */
        if (data_independent_addressing) {
            next_addresses(&address_argon_block, &input_argon_block, &zero_argon_block, (uint32_t*)instance->argon_block_header);
        }
    }
    /* Offset of the current argon_block */
    curr_offset = position.lane * instance->lane_length +
                  position.slice * instance->segment_length + starting_index;


    if (0 == curr_offset % instance->lane_length) {
        /* Last argon_block in this lane */
        prev_offset = curr_offset + instance->lane_length - 1;
    } else {
        /* Previous argon_block */
        prev_offset = curr_offset - 1;
    }
int truc = 0;
    for (i = starting_index; i < instance->segment_length;
         ++i, ++curr_offset, ++prev_offset) {
truc++;
        /*1.1 Rotating prev_offset if needed */
        if (curr_offset % instance->lane_length == 1) {
            prev_offset = curr_offset - 1;
        }

        /* 1.2 Computing the index of the reference argon_block */
        /* 1.2.1 Taking pseudo-random value from the previous argon_block */
        if (data_independent_addressing) {
            if (i % MTP_ARGON2_ADDRESSES_IN_argon_block == 0) {
                next_addresses(&address_argon_block, &input_argon_block, &zero_argon_block, (uint32_t*)instance->argon_block_header);
            }
            pseudo_rand = address_argon_block.v[i % MTP_ARGON2_ADDRESSES_IN_argon_block];
        } else {
           pseudo_rand = instance->memory[prev_offset].v[0];
        }

        /* 1.2.2 Computing the lane of the reference argon_block */
        ref_lane = ((pseudo_rand >> 32)) % instance->lanes;

        if ((position.pass == 0) && (position.slice == 0)) {
            /* Can not reference other lanes yet */
            ref_lane = position.lane;
        }

        /* 1.2.3 Computing the number of possible reference argon_block within the
         * lane.
         */
        position.index = i;




		ref_index = mtp_index_alpha(instance, &position, pseudo_rand & 0xFFFFFFFF,
                                ref_lane == position.lane);


        /* 2 Creating a new argon_block */
        ref_argon_block =
            instance->memory + instance->lane_length * ref_lane + ref_index;

        curr_argon_block = instance->memory + curr_offset;
		uint64_t Theargon_blockIndex = instance->lane_length * ref_lane + ref_index;

//		uint32_t TheCompargon_blockIndex[2] = { 0 };
//		getargon_blockIndex(curr_offset, instance, TheCompargon_blockIndex);

//if (TheCompargon_blockIndex[1]!=(uint32_t)(Theargon_blockIndex & 0xFFFFFFFF))
//printf("curr_offset = %d prev_offset = %d ref_argon_block = %d \n", curr_offset ,prev_offset, instance->lane_length * ref_lane + ref_index);
//	printf("computed value: prev_argon_block %f ref_argon_block %f prev_offset %f  Standard one %f\n", (double)TheCompargon_blockIndex[0], (double)TheCompargon_blockIndex[1],(double)prev_offset, (double)Theargon_blockIndex);

/*
		zRefargon_block  = &instance->TheRefargon_block + curr_offset;
		zPrevargon_block = &instance->ThePrevargon_block + curr_offset;
*/
        if (MTP_ARGON2_VERSION_10 == instance->version) {
            /* version 1.2.1 and earlier: overwrite, not XOR */

//            fill_argon_block(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 0, instance->argon_block_header);
	fill_argon_block_withIndex(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 0, (uint32_t*)instance->argon_block_header, Theargon_blockIndex);
        } else {
            if(0 == position.pass) {
//                        fill_argon_block(instance->memory + prev_offset, ref_argon_block,curr_argon_block, 0, instance->argon_block_header);
				fill_argon_block_withIndex(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 0, (uint32_t*)instance->argon_block_header, Theargon_blockIndex);
//				curr_argon_block->ref_argon_block = instance->lane_length * ref_lane + ref_index;
//				curr_argon_block->prev_argon_block = prev_offset | (instance->lane_length * ref_lane + ref_index) << 32;;
//				uint64_t zHistory =  (prev_offset) | (instance->lane_length * ref_lane + ref_index) << 32;
//				curr_argon_block->BlokHistory = zHistory;
            } else {


//                fill_argon_block(instance->memory + prev_offset, ref_argon_block,curr_argon_block, 1, instance->argon_block_header);
				fill_argon_block_withIndex(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 1, (uint32_t*)instance->argon_block_header, Theargon_blockIndex);
            }
        }
    }
}

void fill_segment_noinde(const mtp_argon2_instance_t *instance,
	mtp_argon2_position_t position) {
	argon_block *ref_argon_block = NULL, *curr_argon_block = NULL;
	argon_block address_argon_block, input_argon_block, zero_argon_block;
	uint64_t pseudo_rand, ref_index, ref_lane;
	uint32_t prev_offset, curr_offset;
	uint32_t starting_index;
	uint32_t i;
	int data_independent_addressing;

	if (instance == NULL) {
		return;
	}


	starting_index = 0;

	if ((0 == position.pass) && (0 == position.slice)) {
		starting_index = 2; /* we have already generated the first two argon_blocks */
	}

	/* Offset of the current argon_block */
	curr_offset = position.lane * instance->lane_length +
		position.slice * instance->segment_length + starting_index;

	if (0 == curr_offset % instance->lane_length) {
		/* Last argon_block in this lane */
		prev_offset = curr_offset + instance->lane_length - 1;
	}
	else {
		/* Previous argon_block */
		prev_offset = curr_offset - 1;
	}

	for (i = starting_index; i < instance->segment_length;
		++i, ++curr_offset, ++prev_offset) {


		/*1.1 Rotating prev_offset if needed */
		if (curr_offset % instance->lane_length == 1) {
			prev_offset = curr_offset - 1;
		}

		/* 1.2 Computing the index of the reference argon_block */
		/* 1.2.1 Taking pseudo-random value from the previous argon_block */

			pseudo_rand = instance->memory[prev_offset].v[0];
		
		/* 1.2.2 Computing the lane of the reference argon_block */
		ref_lane = ((pseudo_rand >> 32)) % instance->lanes;

		if ((position.pass == 0) && (position.slice == 0)) {
			/* Can not reference other lanes yet */
			ref_lane = position.lane;
		}

		/* 1.2.3 Computing the number of possible reference argon_block within the
		* lane.
		*/
		position.index = i;
		ref_index = mtp_index_alpha(instance, &position, pseudo_rand & 0xFFFFFFFF,
			ref_lane == position.lane);

		/* 2 Creating a new argon_block */
		ref_argon_block =
			instance->memory + instance->lane_length * ref_lane + ref_index;
		curr_argon_block = instance->memory + curr_offset;

			if (0 == position.pass) {
				fill_argon_block(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 0, (uint32_t*)instance->argon_block_header);
//				curr_argon_block->ref_argon_block = instance->lane_length * ref_lane + ref_index;
//				curr_argon_block->prev_argon_block = prev_offset;
			}
			else {
				fill_argon_block(instance->memory + prev_offset, ref_argon_block, curr_argon_block, 1, (uint32_t*)instance->argon_block_header);
			}
		
	}
}

