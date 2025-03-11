
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 16-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Frame position 0
			-- Neuron 7
			"01110001" &
			"01110000" &
			-- Neuron 6
			"01100001" &
			"01100000" &
			-- Neuron 5
			"01010001" &
			"01010000" &
			-- Neuron 4
			"01000001" &
			"01000000" &
			-- Neuron 3
			"00110001" &
			"00110000" &
			-- Neuron 2
			"00100001" &
			"00100000" &
			-- Neuron 1
			"00010001" &
			"00010000" &
			-- Neuron 0
			"00000001" &
			"00000000",
			-- Frame position 2
			-- Neuron 7
			"01110011" &
			"01110010" &
			-- Neuron 6
			"01100011" &
			"01100010" &
			-- Neuron 5
			"01010011" &
			"01010010" &
			-- Neuron 4
			"01000011" &
			"01000010" &
			-- Neuron 3
			"00110011" &
			"00110010" &
			-- Neuron 2
			"00100011" &
			"00100010" &
			-- Neuron 1
			"00010011" &
			"00010010" &
			-- Neuron 0
			"00000011" &
			"00000010",
			-- Frame position 4
			-- Neuron 7
			"01110101" &
			"01110100" &
			-- Neuron 6
			"01100101" &
			"01100100" &
			-- Neuron 5
			"01010101" &
			"01010100" &
			-- Neuron 4
			"01000101" &
			"01000100" &
			-- Neuron 3
			"00110101" &
			"00110100" &
			-- Neuron 2
			"00100101" &
			"00100100" &
			-- Neuron 1
			"00010101" &
			"00010100" &
			-- Neuron 0
			"00000101" &
			"00000100",
			-- Frame position 6
			-- Neuron 7
			"01110111" &
			"01110110" &
			-- Neuron 6
			"01100111" &
			"01100110" &
			-- Neuron 5
			"01010111" &
			"01010110" &
			-- Neuron 4
			"01000111" &
			"01000110" &
			-- Neuron 3
			"00110111" &
			"00110110" &
			-- Neuron 2
			"00100111" &
			"00100110" &
			-- Neuron 1
			"00010111" &
			"00010110" &
			-- Neuron 0
			"00000111" &
			"00000110",
			-- Frame position 8
			-- Neuron 7
			"01111001" &
			"01111000" &
			-- Neuron 6
			"01101001" &
			"01101000" &
			-- Neuron 5
			"01011001" &
			"01011000" &
			-- Neuron 4
			"01001001" &
			"01001000" &
			-- Neuron 3
			"00111001" &
			"00111000" &
			-- Neuron 2
			"00101001" &
			"00101000" &
			-- Neuron 1
			"00011001" &
			"00011000" &
			-- Neuron 0
			"00001001" &
			"00001000",
			-- Frame position 10
			-- Neuron 7
			"01111011" &
			"01111010" &
			-- Neuron 6
			"01101011" &
			"01101010" &
			-- Neuron 5
			"01011011" &
			"01011010" &
			-- Neuron 4
			"01001011" &
			"01001010" &
			-- Neuron 3
			"00111011" &
			"00111010" &
			-- Neuron 2
			"00101011" &
			"00101010" &
			-- Neuron 1
			"00011011" &
			"00011010" &
			-- Neuron 0
			"00001011" &
			"00001010",
			-- Frame position 12
			-- Neuron 7
			"01111101" &
			"01111100" &
			-- Neuron 6
			"01101101" &
			"01101100" &
			-- Neuron 5
			"01011101" &
			"01011100" &
			-- Neuron 4
			"01001101" &
			"01001100" &
			-- Neuron 3
			"00111101" &
			"00111100" &
			-- Neuron 2
			"00101101" &
			"00101100" &
			-- Neuron 1
			"00011101" &
			"00011100" &
			-- Neuron 0
			"00001101" &
			"00001100",
			-- Frame position 14
			-- Neuron 7
			"01111111" &
			"01111110" &
			-- Neuron 6
			"01101111" &
			"01101110" &
			-- Neuron 5
			"01011111" &
			"01011110" &
			-- Neuron 4
			"01001111" &
			"01001110" &
			-- Neuron 3
			"00111111" &
			"00111110" &
			-- Neuron 2
			"00101111" &
			"00101110" &
			-- Neuron 1
			"00011111" &
			"00011110" &
			-- Neuron 0
			"00001111" &
			"00001110"
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

