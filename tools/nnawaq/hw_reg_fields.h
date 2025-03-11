
#pragma once

extern "C" {

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

}

#include <vector>


//============================================
// Definition of a field in config registers
//============================================

class LayerRegField {

	public :

	const char* name = nullptr;

	uint32_t mask_reg = 1;  // Shifted for position in configuration register
	uint32_t mask_val = 1;  // Aligned on position zero
	unsigned bits = 1;
	unsigned sh = 0;
	bool     sign = false;

	// Index of the register within the targeted address space
	unsigned reg_idx = 0;

	// Constructor

	LayerRegField(const char* name_in, uint32_t mask_reg_in, bool sign_in = false);
	LayerRegField(const char* name_in, unsigned hi, unsigned lo, bool sign_in = false);
	//~LayerRegField(void);

	// Static methods

	static LayerRegField& AppendMask(std::vector<LayerRegField>& vec, const char* name_in, uint32_t mask_reg_in, bool sign = false);
	static inline LayerRegField& AppendMask(std::vector<LayerRegField>* vec, const char* name_in, uint32_t mask_reg_in, bool sign = false) {
		return AppendMask(*vec, name_in, mask_reg_in, sign);
	}

	static LayerRegField& AppendRange(std::vector<LayerRegField>& vec, const char* name, unsigned hi, unsigned lo, bool sign = false);
	static inline LayerRegField& AppendRange(std::vector<LayerRegField>* vec, const char* name, unsigned hi, unsigned lo, bool sign = false) {
		return AppendRange(*vec, name, hi, lo, sign);
	}

	static inline LayerRegField& AppendBit(std::vector<LayerRegField>& vec, const char* name, unsigned lo, bool sign = false) {
		return AppendRange(vec, name, lo, lo, sign);
	}
	static inline LayerRegField& AppendBit(std::vector<LayerRegField>* vec, const char* name, unsigned lo, bool sign = false) {
		return AppendRange(*vec, name, lo, lo, sign);
	}

	static inline LayerRegField& AppendWidth(std::vector<LayerRegField>& vec, const char* name, unsigned bits, bool sign = false) {
		unsigned sh = vec.empty() ? 0 : vec.back().sh + vec.back().bits;
		return AppendRange(vec, name, sh + bits-1, sh, sign);
	}
	static inline LayerRegField& AppendWidth(std::vector<LayerRegField>* vec, const char* name, unsigned bits, bool sign = false) {
		return AppendWidth(*vec, name, bits, sign);
	}

	static void AssignRegIdx(std::vector<LayerRegField>& vec, unsigned idx);
	static inline void AssignRegIdx(std::vector<LayerRegField>* vec, unsigned idx) { AssignRegIdx(*vec, idx); }

	static void AssignRegIdx(std::vector< std::vector<LayerRegField> >& vec);
	static void AssignRegIdx(std::vector< std::vector<LayerRegField> >* vec) { AssignRegIdx(*vec); }

	static int EnsureNoOverlaps(const std::vector<LayerRegField>& vec, bool do_abort = true);
	static inline int EnsureNoOverlaps(const std::vector<LayerRegField>* vec, bool do_abort = true) {
		return EnsureNoOverlaps(*vec, do_abort);
	}

	static int EnsureNoOverlaps(const std::vector< std::vector<LayerRegField> >& vec, bool do_abort = true);
	static inline int EnsureNoOverlaps(const std::vector< std::vector<LayerRegField> >* vec, bool do_abort = true) {
		return EnsureNoOverlaps(*vec, do_abort);
	}

	// Methods

	inline unsigned GetLo(void) const { return sh; }
	inline unsigned GetHi(void) const { return sh + bits - 1; }

	inline unsigned GetUnsigned(uint32_t reg) const {
		return (reg >> sh) & mask_val;
	}
	inline int GetSigned(uint32_t reg) const {
		unsigned val = (reg >> sh) & mask_val;
		// Sign extension :
		// - Isolate the sign bit
		// - Negate
		// - OR with unsigned value
		val |= -(val & ~(mask_val >> 1));
		return val;
	}
	inline unsigned Get(uint32_t reg) const {
		unsigned val = (reg >> sh) & mask_val;
		if(sign) val |= -(val & ~(mask_val >> 1));
		return val;
	}

	// Capacity check, return 0 if OK
	int CheckCapacity(unsigned val) const;

	// No capacity check
	inline uint32_t SetRef(uint32_t& reg, unsigned val) const {
		reg = (reg & ~mask_reg) | ((val << sh) & mask_reg);
		return reg;
	}
	inline uint32_t Set(uint32_t reg, unsigned val) const __attribute__ ((warn_unused_result)) {
		SetRef(reg, val);
		return reg;
	}

	// Return 0 if there is no capacity issue
	int SetRefSilent(uint32_t& reg, unsigned val) const;
	int SetRefVerbose(uint32_t& reg, unsigned val) const;

};

