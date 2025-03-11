
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 16-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Frame position 0
			-- Neurons 3 mod PAR_OUT=4
			-- Neuron 7
			"01110011" &
			"01110010" &
			"01110001" &
			"01110000" &
			-- Neuron 3
			"00110011" &
			"00110010" &
			"00110001" &
			"00110000" &
			-- Neurons 2 mod PAR_OUT=4
			-- Neuron 6
			"01100011" &
			"01100010" &
			"01100001" &
			"01100000" &
			-- Neuron 2
			"00100011" &
			"00100010" &
			"00100001" &
			"00100000" &
			-- Neurons 1 mod PAR_OUT=4
			-- Neuron 5
			"01010011" &
			"01010010" &
			"01010001" &
			"01010000" &
			-- Neuron 1
			"00010011" &
			"00010010" &
			"00010001" &
			"00010000" &
			-- Neurons 0 mod PAR_OUT=4
			-- Neuron 4
			"01000011" &
			"01000010" &
			"01000001" &
			"01000000" &
			-- Neuron 0
			"00000011" &
			"00000010" &
			"00000001" &
			"00000000",
			-- Frame position 4
			-- Neurons 3 mod PAR_OUT=4
			-- Neuron 7
			"01110111" &
			"01110110" &
			"01110101" &
			"01110100" &
			-- Neuron 3
			"00110111" &
			"00110110" &
			"00110101" &
			"00110100" &
			-- Neurons 2 mod PAR_OUT=4
			-- Neuron 6
			"01100111" &
			"01100110" &
			"01100101" &
			"01100100" &
			-- Neuron 2
			"00100111" &
			"00100110" &
			"00100101" &
			"00100100" &
			-- Neurons 1 mod PAR_OUT=4
			-- Neuron 5
			"01010111" &
			"01010110" &
			"01010101" &
			"01010100" &
			-- Neuron 1
			"00010111" &
			"00010110" &
			"00010101" &
			"00010100" &
			-- Neurons 0 mod PAR_OUT=4
			-- Neuron 4
			"01000111" &
			"01000110" &
			"01000101" &
			"01000100" &
			-- Neuron 0
			"00000111" &
			"00000110" &
			"00000101" &
			"00000100",
			-- Frame position 8
			-- Neurons 3 mod PAR_OUT=4
			-- Neuron 7
			"01111011" &
			"01111010" &
			"01111001" &
			"01111000" &
			-- Neuron 3
			"00111011" &
			"00111010" &
			"00111001" &
			"00111000" &
			-- Neurons 2 mod PAR_OUT=4
			-- Neuron 6
			"01101011" &
			"01101010" &
			"01101001" &
			"01101000" &
			-- Neuron 2
			"00101011" &
			"00101010" &
			"00101001" &
			"00101000" &
			-- Neurons 1 mod PAR_OUT=4
			-- Neuron 5
			"01011011" &
			"01011010" &
			"01011001" &
			"01011000" &
			-- Neuron 1
			"00011011" &
			"00011010" &
			"00011001" &
			"00011000" &
			-- Neurons 0 mod PAR_OUT=4
			-- Neuron 4
			"01001011" &
			"01001010" &
			"01001001" &
			"01001000" &
			-- Neuron 0
			"00001011" &
			"00001010" &
			"00001001" &
			"00001000",
			-- Frame position 12
			-- Neurons 3 mod PAR_OUT=4
			-- Neuron 7
			"01111111" &
			"01111110" &
			"01111101" &
			"01111100" &
			-- Neuron 3
			"00111111" &
			"00111110" &
			"00111101" &
			"00111100" &
			-- Neurons 2 mod PAR_OUT=4
			-- Neuron 6
			"01101111" &
			"01101110" &
			"01101101" &
			"01101100" &
			-- Neuron 2
			"00101111" &
			"00101110" &
			"00101101" &
			"00101100" &
			-- Neurons 1 mod PAR_OUT=4
			-- Neuron 5
			"01011111" &
			"01011110" &
			"01011101" &
			"01011100" &
			-- Neuron 1
			"00011111" &
			"00011110" &
			"00011101" &
			"00011100" &
			-- Neurons 0 mod PAR_OUT=4
			-- Neuron 4
			"01001111" &
			"01001110" &
			"01001101" &
			"01001100" &
			-- Neuron 0
			"00001111" &
			"00001110" &
			"00001101" &
			"00001100"
		);

		attribute ram_style : string;
		attribute ram_style of memweight : signal is "distributed";

	begin

		data_out <= memweight(to_integer(unsigned(addr_in)));

	else generate

		-- Default assignement just in case
		data_out <= (others => '0');

	end generate;

	-- AUTOGEN CONST WEIGHTS END

