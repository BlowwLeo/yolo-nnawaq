
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 16-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Frame position 0
			-- Neurons 7 mod PAR_OUT=8
			"01110000" &
			-- Neurons 6 mod PAR_OUT=8
			"01100000" &
			-- Neurons 5 mod PAR_OUT=8
			"01010000" &
			-- Neurons 4 mod PAR_OUT=8
			"01000000" &
			-- Neurons 3 mod PAR_OUT=8
			"00110000" &
			-- Neurons 2 mod PAR_OUT=8
			"00100000" &
			-- Neurons 1 mod PAR_OUT=8
			"00010000" &
			-- Neurons 0 mod PAR_OUT=8
			"00000000",
			-- Frame position 1
			-- Neurons 7 mod PAR_OUT=8
			"01110001" &
			-- Neurons 6 mod PAR_OUT=8
			"01100001" &
			-- Neurons 5 mod PAR_OUT=8
			"01010001" &
			-- Neurons 4 mod PAR_OUT=8
			"01000001" &
			-- Neurons 3 mod PAR_OUT=8
			"00110001" &
			-- Neurons 2 mod PAR_OUT=8
			"00100001" &
			-- Neurons 1 mod PAR_OUT=8
			"00010001" &
			-- Neurons 0 mod PAR_OUT=8
			"00000001",
			-- Frame position 2
			-- Neurons 7 mod PAR_OUT=8
			"01110010" &
			-- Neurons 6 mod PAR_OUT=8
			"01100010" &
			-- Neurons 5 mod PAR_OUT=8
			"01010010" &
			-- Neurons 4 mod PAR_OUT=8
			"01000010" &
			-- Neurons 3 mod PAR_OUT=8
			"00110010" &
			-- Neurons 2 mod PAR_OUT=8
			"00100010" &
			-- Neurons 1 mod PAR_OUT=8
			"00010010" &
			-- Neurons 0 mod PAR_OUT=8
			"00000010",
			-- Frame position 3
			-- Neurons 7 mod PAR_OUT=8
			"01110011" &
			-- Neurons 6 mod PAR_OUT=8
			"01100011" &
			-- Neurons 5 mod PAR_OUT=8
			"01010011" &
			-- Neurons 4 mod PAR_OUT=8
			"01000011" &
			-- Neurons 3 mod PAR_OUT=8
			"00110011" &
			-- Neurons 2 mod PAR_OUT=8
			"00100011" &
			-- Neurons 1 mod PAR_OUT=8
			"00010011" &
			-- Neurons 0 mod PAR_OUT=8
			"00000011",
			-- Frame position 4
			-- Neurons 7 mod PAR_OUT=8
			"01110100" &
			-- Neurons 6 mod PAR_OUT=8
			"01100100" &
			-- Neurons 5 mod PAR_OUT=8
			"01010100" &
			-- Neurons 4 mod PAR_OUT=8
			"01000100" &
			-- Neurons 3 mod PAR_OUT=8
			"00110100" &
			-- Neurons 2 mod PAR_OUT=8
			"00100100" &
			-- Neurons 1 mod PAR_OUT=8
			"00010100" &
			-- Neurons 0 mod PAR_OUT=8
			"00000100",
			-- Frame position 5
			-- Neurons 7 mod PAR_OUT=8
			"01110101" &
			-- Neurons 6 mod PAR_OUT=8
			"01100101" &
			-- Neurons 5 mod PAR_OUT=8
			"01010101" &
			-- Neurons 4 mod PAR_OUT=8
			"01000101" &
			-- Neurons 3 mod PAR_OUT=8
			"00110101" &
			-- Neurons 2 mod PAR_OUT=8
			"00100101" &
			-- Neurons 1 mod PAR_OUT=8
			"00010101" &
			-- Neurons 0 mod PAR_OUT=8
			"00000101",
			-- Frame position 6
			-- Neurons 7 mod PAR_OUT=8
			"01110110" &
			-- Neurons 6 mod PAR_OUT=8
			"01100110" &
			-- Neurons 5 mod PAR_OUT=8
			"01010110" &
			-- Neurons 4 mod PAR_OUT=8
			"01000110" &
			-- Neurons 3 mod PAR_OUT=8
			"00110110" &
			-- Neurons 2 mod PAR_OUT=8
			"00100110" &
			-- Neurons 1 mod PAR_OUT=8
			"00010110" &
			-- Neurons 0 mod PAR_OUT=8
			"00000110",
			-- Frame position 7
			-- Neurons 7 mod PAR_OUT=8
			"01110111" &
			-- Neurons 6 mod PAR_OUT=8
			"01100111" &
			-- Neurons 5 mod PAR_OUT=8
			"01010111" &
			-- Neurons 4 mod PAR_OUT=8
			"01000111" &
			-- Neurons 3 mod PAR_OUT=8
			"00110111" &
			-- Neurons 2 mod PAR_OUT=8
			"00100111" &
			-- Neurons 1 mod PAR_OUT=8
			"00010111" &
			-- Neurons 0 mod PAR_OUT=8
			"00000111",
			-- Frame position 8
			-- Neurons 7 mod PAR_OUT=8
			"01111000" &
			-- Neurons 6 mod PAR_OUT=8
			"01101000" &
			-- Neurons 5 mod PAR_OUT=8
			"01011000" &
			-- Neurons 4 mod PAR_OUT=8
			"01001000" &
			-- Neurons 3 mod PAR_OUT=8
			"00111000" &
			-- Neurons 2 mod PAR_OUT=8
			"00101000" &
			-- Neurons 1 mod PAR_OUT=8
			"00011000" &
			-- Neurons 0 mod PAR_OUT=8
			"00001000",
			-- Frame position 9
			-- Neurons 7 mod PAR_OUT=8
			"01111001" &
			-- Neurons 6 mod PAR_OUT=8
			"01101001" &
			-- Neurons 5 mod PAR_OUT=8
			"01011001" &
			-- Neurons 4 mod PAR_OUT=8
			"01001001" &
			-- Neurons 3 mod PAR_OUT=8
			"00111001" &
			-- Neurons 2 mod PAR_OUT=8
			"00101001" &
			-- Neurons 1 mod PAR_OUT=8
			"00011001" &
			-- Neurons 0 mod PAR_OUT=8
			"00001001",
			-- Frame position 10
			-- Neurons 7 mod PAR_OUT=8
			"01111010" &
			-- Neurons 6 mod PAR_OUT=8
			"01101010" &
			-- Neurons 5 mod PAR_OUT=8
			"01011010" &
			-- Neurons 4 mod PAR_OUT=8
			"01001010" &
			-- Neurons 3 mod PAR_OUT=8
			"00111010" &
			-- Neurons 2 mod PAR_OUT=8
			"00101010" &
			-- Neurons 1 mod PAR_OUT=8
			"00011010" &
			-- Neurons 0 mod PAR_OUT=8
			"00001010",
			-- Frame position 11
			-- Neurons 7 mod PAR_OUT=8
			"01111011" &
			-- Neurons 6 mod PAR_OUT=8
			"01101011" &
			-- Neurons 5 mod PAR_OUT=8
			"01011011" &
			-- Neurons 4 mod PAR_OUT=8
			"01001011" &
			-- Neurons 3 mod PAR_OUT=8
			"00111011" &
			-- Neurons 2 mod PAR_OUT=8
			"00101011" &
			-- Neurons 1 mod PAR_OUT=8
			"00011011" &
			-- Neurons 0 mod PAR_OUT=8
			"00001011",
			-- Frame position 12
			-- Neurons 7 mod PAR_OUT=8
			"01111100" &
			-- Neurons 6 mod PAR_OUT=8
			"01101100" &
			-- Neurons 5 mod PAR_OUT=8
			"01011100" &
			-- Neurons 4 mod PAR_OUT=8
			"01001100" &
			-- Neurons 3 mod PAR_OUT=8
			"00111100" &
			-- Neurons 2 mod PAR_OUT=8
			"00101100" &
			-- Neurons 1 mod PAR_OUT=8
			"00011100" &
			-- Neurons 0 mod PAR_OUT=8
			"00001100",
			-- Frame position 13
			-- Neurons 7 mod PAR_OUT=8
			"01111101" &
			-- Neurons 6 mod PAR_OUT=8
			"01101101" &
			-- Neurons 5 mod PAR_OUT=8
			"01011101" &
			-- Neurons 4 mod PAR_OUT=8
			"01001101" &
			-- Neurons 3 mod PAR_OUT=8
			"00111101" &
			-- Neurons 2 mod PAR_OUT=8
			"00101101" &
			-- Neurons 1 mod PAR_OUT=8
			"00011101" &
			-- Neurons 0 mod PAR_OUT=8
			"00001101",
			-- Frame position 14
			-- Neurons 7 mod PAR_OUT=8
			"01111110" &
			-- Neurons 6 mod PAR_OUT=8
			"01101110" &
			-- Neurons 5 mod PAR_OUT=8
			"01011110" &
			-- Neurons 4 mod PAR_OUT=8
			"01001110" &
			-- Neurons 3 mod PAR_OUT=8
			"00111110" &
			-- Neurons 2 mod PAR_OUT=8
			"00101110" &
			-- Neurons 1 mod PAR_OUT=8
			"00011110" &
			-- Neurons 0 mod PAR_OUT=8
			"00001110",
			-- Frame position 15
			-- Neurons 7 mod PAR_OUT=8
			"01111111" &
			-- Neurons 6 mod PAR_OUT=8
			"01101111" &
			-- Neurons 5 mod PAR_OUT=8
			"01011111" &
			-- Neurons 4 mod PAR_OUT=8
			"01001111" &
			-- Neurons 3 mod PAR_OUT=8
			"00111111" &
			-- Neurons 2 mod PAR_OUT=8
			"00101111" &
			-- Neurons 1 mod PAR_OUT=8
			"00011111" &
			-- Neurons 0 mod PAR_OUT=8
			"00001111"
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

