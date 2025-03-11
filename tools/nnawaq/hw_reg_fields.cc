
extern "C" {

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <assert.h>

#include "nnawaq_utils.h"

}

#include "hw_reg_fields.h"


using namespace std;


//============================================
// LayerRegField class
//============================================

LayerRegField::LayerRegField(const char* name_in, uint32_t mask_reg_in, bool sign_in) : name(name_in), sign(sign_in) {

	if(mask_reg_in == 0) {
		printf("INTERNAL ERROR : Invalid mask 0x%x for field %s\n", mask_reg_in, name_in);
		abort();
	}

	// Compute the shift
	sh = __builtin_ctz(mask_reg_in);

	mask_reg = mask_reg_in;
	mask_val = mask_reg_in >> sh;

	bits = uint_bitsnb(mask_val);

	// Ensure all bits at 1 in the mask are consecutive
	if(mask_val != uint_genmask(bits)) {
		printf("INTERNAL ERROR : Invalid mask 0x%08x for field %s\n", mask_reg_in, name_in);
		abort();
	}

}

LayerRegField::LayerRegField(const char* name_in, unsigned hi, unsigned lo, bool sign_in) : name(name_in), sign(sign_in) {

	if(lo > hi || hi > 31) {
		printf("INTERNAL ERROR : Invalid bit range (%u downto %u) for field %s\n", hi, lo, name_in);
		abort();
	}

	sh   = lo;
	bits = hi - lo + 1;

	mask_val = uint_genmask(bits);
	mask_reg = mask_val << sh;

}

LayerRegField& LayerRegField::AppendMask(std::vector<LayerRegField>& vec, const char* name_in, uint32_t mask_reg_in, bool sign) {
	vec.emplace_back(name_in, mask_reg_in, sign);
	return vec.back();
}
LayerRegField& LayerRegField::AppendRange(std::vector<LayerRegField>& vec, const char* name_in, unsigned hi, unsigned lo, bool sign) {
	vec.emplace_back(name_in, hi, lo, sign);
	return vec.back();
}

void LayerRegField::AssignRegIdx(std::vector<LayerRegField>& vec, unsigned idx) {
	for(auto& field : vec) field.reg_idx = idx;
}
void LayerRegField::AssignRegIdx(std::vector< std::vector<LayerRegField> >& vec) {
	for(unsigned i = 0; i<vec.size(); i++) AssignRegIdx(vec[i], i);
}

int LayerRegField::EnsureNoOverlaps(const std::vector<LayerRegField>& vec, bool do_abord) {
	uint32_t mask_reg = 0;
	unsigned errors_nb = 0;
	for(unsigned i = 0; i < vec.size(); i++) {
		if((mask_reg & vec[i].mask_reg) != 0) {
			// Overlap found, print all other registers
			for(unsigned j = 0; j < i; j++) {
				if((vec[i].mask_reg & vec[j].mask_reg) == 0) continue;
				printf("INTERNAL ERROR : Field '%s' mask 0x%08x overlaps with field '%s' mask 0x%08x\n", vec[i].name, vec[i].mask_reg, vec[j].name, vec[j].mask_reg);
			}
			// Count errors
			errors_nb++;
		}
		// Add the mask
		mask_reg |= vec[i].mask_reg;
	}
	if(errors_nb != 0) {
		abort();
	}
	return errors_nb;
}

int LayerRegField::EnsureNoOverlaps(const std::vector< std::vector<LayerRegField> >& vec, bool do_abort) {
	unsigned errors_nb = 0;
	for(unsigned i = 0; i < vec.size(); i++) {
		int z = EnsureNoOverlaps(vec[i], false);
		if(z != 0) errors_nb++;
	}
	if(errors_nb != 0) {
		abort();
	}
	return errors_nb;
}

int LayerRegField::CheckCapacity(unsigned val) const {
	if(sign) {
		// Isolate the sign extension bits and the sign bit
		int v = val & ~(mask_val >> 1);
		// Arithmetic shift to align on zero
		v >>= bits - 1;
		// Permitted values are 0 and -1
		return (v != 0 && v != -1) ? 1 : 0;
	}
	// Sign extension bits must be zero
	return (val & ~mask_val) ? 1 : 0;
}

int LayerRegField::SetRefSilent(uint32_t& reg, unsigned val) const {
	reg = Set(reg, val);
	return CheckCapacity(val);
}

int LayerRegField::SetRefVerbose(uint32_t& reg, unsigned val) const {
	int z = SetRefSilent(reg, val);
	if(z != 0) {
		if(sign) printf("WARNING : Value %i exceeds capacity of %u bits of field '%s'\n", (int)val, bits, name);
		else     printf("WARNING : Value %u exceeds capacity of %u bits of field '%s'\n", val, bits, name);
	}
	return z;
}

