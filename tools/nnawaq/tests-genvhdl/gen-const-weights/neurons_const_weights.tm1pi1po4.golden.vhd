
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 16-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Frame position 0
			-- Neurons 3 mod PAR_OUT=4
			"01110000" &
			"00110000" &
			-- Neurons 2 mod PAR_OUT=4
			"01100000" &
			"00100000" &
			-- Neurons 1 mod PAR_OUT=4
			"01010000" &
			"00010000" &
			-- Neurons 0 mod PAR_OUT=4
			"01000000" &
			"00000000",
			-- Frame position 1
			-- Neurons 3 mod PAR_OUT=4
			"01110001" &
			"00110001" &
			-- Neurons 2 mod PAR_OUT=4
			"01100001" &
			"00100001" &
			-- Neurons 1 mod PAR_OUT=4
			"01010001" &
			"00010001" &
			-- Neurons 0 mod PAR_OUT=4
			"01000001" &
			"00000001",
			-- Frame position 2
			-- Neurons 3 mod PAR_OUT=4
			"01110010" &
			"00110010" &
			-- Neurons 2 mod PAR_OUT=4
			"01100010" &
			"00100010" &
			-- Neurons 1 mod PAR_OUT=4
			"01010010" &
			"00010010" &
			-- Neurons 0 mod PAR_OUT=4
			"01000010" &
			"00000010",
			-- Frame position 3
			-- Neurons 3 mod PAR_OUT=4
			"01110011" &
			"00110011" &
			-- Neurons 2 mod PAR_OUT=4
			"01100011" &
			"00100011" &
			-- Neurons 1 mod PAR_OUT=4
			"01010011" &
			"00010011" &
			-- Neurons 0 mod PAR_OUT=4
			"01000011" &
			"00000011",
			-- Frame position 4
			-- Neurons 3 mod PAR_OUT=4
			"01110100" &
			"00110100" &
			-- Neurons 2 mod PAR_OUT=4
			"01100100" &
			"00100100" &
			-- Neurons 1 mod PAR_OUT=4
			"01010100" &
			"00010100" &
			-- Neurons 0 mod PAR_OUT=4
			"01000100" &
			"00000100",
			-- Frame position 5
			-- Neurons 3 mod PAR_OUT=4
			"01110101" &
			"00110101" &
			-- Neurons 2 mod PAR_OUT=4
			"01100101" &
			"00100101" &
			-- Neurons 1 mod PAR_OUT=4
			"01010101" &
			"00010101" &
			-- Neurons 0 mod PAR_OUT=4
			"01000101" &
			"00000101",
			-- Frame position 6
			-- Neurons 3 mod PAR_OUT=4
			"01110110" &
			"00110110" &
			-- Neurons 2 mod PAR_OUT=4
			"01100110" &
			"00100110" &
			-- Neurons 1 mod PAR_OUT=4
			"01010110" &
			"00010110" &
			-- Neurons 0 mod PAR_OUT=4
			"01000110" &
			"00000110",
			-- Frame position 7
			-- Neurons 3 mod PAR_OUT=4
			"01110111" &
			"00110111" &
			-- Neurons 2 mod PAR_OUT=4
			"01100111" &
			"00100111" &
			-- Neurons 1 mod PAR_OUT=4
			"01010111" &
			"00010111" &
			-- Neurons 0 mod PAR_OUT=4
			"01000111" &
			"00000111",
			-- Frame position 8
			-- Neurons 3 mod PAR_OUT=4
			"01111000" &
			"00111000" &
			-- Neurons 2 mod PAR_OUT=4
			"01101000" &
			"00101000" &
			-- Neurons 1 mod PAR_OUT=4
			"01011000" &
			"00011000" &
			-- Neurons 0 mod PAR_OUT=4
			"01001000" &
			"00001000",
			-- Frame position 9
			-- Neurons 3 mod PAR_OUT=4
			"01111001" &
			"00111001" &
			-- Neurons 2 mod PAR_OUT=4
			"01101001" &
			"00101001" &
			-- Neurons 1 mod PAR_OUT=4
			"01011001" &
			"00011001" &
			-- Neurons 0 mod PAR_OUT=4
			"01001001" &
			"00001001",
			-- Frame position 10
			-- Neurons 3 mod PAR_OUT=4
			"01111010" &
			"00111010" &
			-- Neurons 2 mod PAR_OUT=4
			"01101010" &
			"00101010" &
			-- Neurons 1 mod PAR_OUT=4
			"01011010" &
			"00011010" &
			-- Neurons 0 mod PAR_OUT=4
			"01001010" &
			"00001010",
			-- Frame position 11
			-- Neurons 3 mod PAR_OUT=4
			"01111011" &
			"00111011" &
			-- Neurons 2 mod PAR_OUT=4
			"01101011" &
			"00101011" &
			-- Neurons 1 mod PAR_OUT=4
			"01011011" &
			"00011011" &
			-- Neurons 0 mod PAR_OUT=4
			"01001011" &
			"00001011",
			-- Frame position 12
			-- Neurons 3 mod PAR_OUT=4
			"01111100" &
			"00111100" &
			-- Neurons 2 mod PAR_OUT=4
			"01101100" &
			"00101100" &
			-- Neurons 1 mod PAR_OUT=4
			"01011100" &
			"00011100" &
			-- Neurons 0 mod PAR_OUT=4
			"01001100" &
			"00001100",
			-- Frame position 13
			-- Neurons 3 mod PAR_OUT=4
			"01111101" &
			"00111101" &
			-- Neurons 2 mod PAR_OUT=4
			"01101101" &
			"00101101" &
			-- Neurons 1 mod PAR_OUT=4
			"01011101" &
			"00011101" &
			-- Neurons 0 mod PAR_OUT=4
			"01001101" &
			"00001101",
			-- Frame position 14
			-- Neurons 3 mod PAR_OUT=4
			"01111110" &
			"00111110" &
			-- Neurons 2 mod PAR_OUT=4
			"01101110" &
			"00101110" &
			-- Neurons 1 mod PAR_OUT=4
			"01011110" &
			"00011110" &
			-- Neurons 0 mod PAR_OUT=4
			"01001110" &
			"00001110",
			-- Frame position 15
			-- Neurons 3 mod PAR_OUT=4
			"01111111" &
			"00111111" &
			-- Neurons 2 mod PAR_OUT=4
			"01101111" &
			"00101111" &
			-- Neurons 1 mod PAR_OUT=4
			"01011111" &
			"00011111" &
			-- Neurons 0 mod PAR_OUT=4
			"01001111" &
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

