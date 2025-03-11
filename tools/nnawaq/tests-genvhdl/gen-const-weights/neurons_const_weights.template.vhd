
	-- AUTOGEN CONST WEIGHTS BEGIN

	-- This implementation is an example template to be replaced by custom IDs and data
	gen : if LAYER_ID = 0 generate

		assert false report "Warning : Demonstration template implementation for constant weights generation is not supposed to be used" severity note;
		data_out <= (others => '0');

	else generate

		assert false report "Warning : Default case in generation of constant weights is not supposed to be used" severity note;
		data_out <= (others => '0');

	end generate;

	-- AUTOGEN CONST WEIGHTS END

