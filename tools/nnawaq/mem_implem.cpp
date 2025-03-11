
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <math.h>

}

#include "mem_implem.h"


using namespace std;


//============================================
// Static methods
//============================================

MemImplem::style_type MemImplem::GetStyle(const char* name) {
	if(strcmp(name, "none") == 0)   return MemImplem::STYLE_NONE;
	if(strcmp(name, "auto") == 0)   return MemImplem::STYLE_AUTO;
	if(strcmp(name, "reg") == 0)    return MemImplem::STYLE_REG;
	if(strcmp(name, "lut") == 0)    return MemImplem::STYLE_LUTRAM;
	if(strcmp(name, "lutram") == 0) return MemImplem::STYLE_LUTRAM;
	if(strcmp(name, "bram") == 0)   return MemImplem::STYLE_BRAM;
	if(strcmp(name, "uram") == 0)   return MemImplem::STYLE_URAM;
	return MemImplem::STYLE_NONE;
}

MemImplem::style_type MemImplem::GetStyleVerbose(const char* style_name) {
	style_type style_id = GetStyle(style_name);
	if(style_id == STYLE_NONE) {
		printf("Error: Unknown memory implementation style '%s'\n", style_name);
	}
	return style_id;
}

const char* MemImplem::GetStyleName(style_type style) {
	static const char* arr_names[] = {
		"none",
		"reg",
		"lutram",
		"bram",
		"uram",
		"auto",
	};
	return arr_names[(unsigned)style];
}


//============================================
// Methods
//============================================

void MemImplem::EvalBlocksReg(void) {
	blocks = EvalSizeTotal();
}

void MemImplem::EvalBlocksLutram(void) {
	// FIXME This does not take into account the slice constraints, the actual LUTRAM usage can be significantly higher, also the number of Write and Read ports matters
	if(lines <= 32) blocks = (width + 1) / 2 * num;
	else {
		unsigned height_num = (lines + 63) / 64;
		blocks = height_num * width * num;
	}
}

void MemImplem::EvalBlocksBram(void) {
	// Number of bram18k blocks
	unsigned blocks_best = 0;
	unsigned height_best = 0;

	auto one_config = [&](unsigned blk_lines, unsigned blk_width, unsigned blocks) {
		// Eval number of blocks for this config
		unsigned width_num  = (width + blk_width - 1) / blk_width;
		unsigned height_num = (lines + blk_lines - 1) / blk_lines;
		unsigned blocks_num = height_num * width_num * blocks;
		// Optimize for number of BRAM
		// This may use extra address decoding and multiplexing logic, and an additional pipeline register
		if(opt_speed == false) {
			if(blocks_best == 0 || blocks_num <= blocks_best) blocks_best = blocks_num;
		}
		// Optimize for speed
		// This may use up to 100% more BRAMs than necessary if number of lines is just above a power of 2
		if(opt_speed == true) {
			if(
				blocks_best == 0 ||
				height_num < height_best ||
				((height_num == height_best) && (blocks_num <= blocks_best))
			) {
				blocks_best = blocks_num;
				height_best = height_num;
			}
		}
	};

	// bram18k
	one_config(  512, 36, 1);
	one_config( 1024, 18, 1);
	one_config( 2048,  9, 1);
	one_config( 4096,  4, 1);
	one_config( 8192,  2, 1);
	one_config(16384,  1, 1);

	// bram36k
	one_config(  512, 72, 2);
	one_config( 1024, 36, 2);
	one_config( 2048, 18, 2);
	one_config( 4096,  9, 2);
	one_config( 8192,  4, 2);
	one_config(16384,  2, 2);
	one_config(32768,  1, 2);
	one_config(65536,  1, 4);  // bram36k, cascaded

	// Keep unit bram18k
	blocks = blocks_best * num;
}

void MemImplem::EvalBlocksUram(void) {
	// Number of bram18k blocks
	unsigned blocks_best = 0;
	unsigned height_best = 0;

	auto one_config = [&](unsigned blk_lines, unsigned blk_width, unsigned blocks) {
		// Eval number of blocks for this config
		unsigned width_num  = (width + blk_width - 1) / blk_width;
		unsigned height_num = (lines + blk_lines - 1) / blk_lines;
		unsigned blocks_num = height_num * width_num * blocks;
		// Optimize for number of BRAM
		// This may use extra address decoding and multiplexing logic, and an additional pipeline register
		if(opt_speed == false) {
			if(blocks_best == 0 || blocks_num <= blocks_best) blocks_best = blocks_num;
		}
		// Optimize for speed
		// This may use up to 100% more BRAMs than necessary if number of lines is just above a power of 2
		if(opt_speed == true) {
			if(
				blocks_best == 0 ||
				height_num < height_best ||
				((height_num == height_best) && (blocks_num <= blocks_best))
			) {
				blocks_best = blocks_num;
				height_best = height_num;
			}
		}
	};

	// uram288
	one_config(4096, 72, 1);

	blocks = blocks_best * num;
}

void MemImplem::EvalBlocks(unsigned lut_threshold, bool use_uram) {
	if(IsEmpty()) return;

	// Automatically assign an implementation style
	if(style == STYLE_NONE || style == STYLE_AUTO) {
		if(lut_threshold <= 0) lut_threshold = 64;
		if(lines <= 1) style = STYLE_REG;
		else if(lines <= lut_threshold) style = STYLE_LUTRAM;
		else if(use_uram == true && width > 72 && lines > 4096) style = STYLE_URAM;
		else style = STYLE_BRAM;
	}

	if     (style == STYLE_REG)    EvalBlocksReg();
	else if(style == STYLE_LUTRAM) EvalBlocksLutram();
	else if(style == STYLE_BRAM)   EvalBlocksBram();
	else if(style == STYLE_URAM)   EvalBlocksUram();
}

