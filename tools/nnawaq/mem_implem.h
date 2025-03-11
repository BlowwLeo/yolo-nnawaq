
#pragma once

#include <cstdbool>


//============================================
// Definition of a memory implementation
//============================================

typedef class MemImplem MemImplem;

class MemImplem {

	public :

	enum style_type {
		STYLE_NONE   = 0,
		STYLE_REG    = 1,
		STYLE_LUTRAM = 2,
		STYLE_BRAM   = 3,
		STYLE_URAM   = 4,
		STYLE_AUTO   = 5,
	};

	// Fields

	// Dimensions
	unsigned width = 0;
	unsigned lines = 0;
	unsigned num   = 1;  // Number of memory banks

	// To evaluate with objective for speed or size
	bool opt_speed = false;

	// Resulting properties
	style_type style = STYLE_NONE;
	unsigned long blocks = 0;  // Total number of memory blocks (bram, lutram, reg, ...)

	public :

	// Constructor/destructor : just default

	// Static methods

	static style_type GetStyle(const char* style_name);
	static style_type GetStyleVerbose(const char* style_name);
	static const char* GetStyleName(style_type style);

	// Methods

	inline const char* GetStyleName(void) { return GetStyleName(style); }

	inline bool IsEmpty(void) { return (width == 0) || (lines == 0) || (num == 0); }

	inline unsigned long EvalSizeOneMem(void) { return (unsigned long)width * (unsigned long)lines; }
	inline unsigned long EvalSizeTotal(void)  { return (unsigned long)width * (unsigned long)lines * num; }

	void EvalBlocksReg(void);
	void EvalBlocksLutram(void);
	void EvalBlocksBram(void);
	void EvalBlocksUram(void);

	void EvalBlocks(unsigned lut_threshold, bool use_uram);

};

