
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 64-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Time multiplexing : neurons 0 to 1
			-- Frame position 0
			"00010000" &
			"00000000",
			-- Frame position 1
			"00010001" &
			"00000001",
			-- Frame position 2
			"00010010" &
			"00000010",
			-- Frame position 3
			"00010011" &
			"00000011",
			-- Frame position 4
			"00010100" &
			"00000100",
			-- Frame position 5
			"00010101" &
			"00000101",
			-- Frame position 6
			"00010110" &
			"00000110",
			-- Frame position 7
			"00010111" &
			"00000111",
			-- Frame position 8
			"00011000" &
			"00001000",
			-- Frame position 9
			"00011001" &
			"00001001",
			-- Frame position 10
			"00011010" &
			"00001010",
			-- Frame position 11
			"00011011" &
			"00001011",
			-- Frame position 12
			"00011100" &
			"00001100",
			-- Frame position 13
			"00011101" &
			"00001101",
			-- Frame position 14
			"00011110" &
			"00001110",
			-- Frame position 15
			"00011111" &
			"00001111",
			-- Time multiplexing : neurons 2 to 3
			-- Frame position 0
			"00110000" &
			"00100000",
			-- Frame position 1
			"00110001" &
			"00100001",
			-- Frame position 2
			"00110010" &
			"00100010",
			-- Frame position 3
			"00110011" &
			"00100011",
			-- Frame position 4
			"00110100" &
			"00100100",
			-- Frame position 5
			"00110101" &
			"00100101",
			-- Frame position 6
			"00110110" &
			"00100110",
			-- Frame position 7
			"00110111" &
			"00100111",
			-- Frame position 8
			"00111000" &
			"00101000",
			-- Frame position 9
			"00111001" &
			"00101001",
			-- Frame position 10
			"00111010" &
			"00101010",
			-- Frame position 11
			"00111011" &
			"00101011",
			-- Frame position 12
			"00111100" &
			"00101100",
			-- Frame position 13
			"00111101" &
			"00101101",
			-- Frame position 14
			"00111110" &
			"00101110",
			-- Frame position 15
			"00111111" &
			"00101111",
			-- Time multiplexing : neurons 4 to 5
			-- Frame position 0
			"01010000" &
			"01000000",
			-- Frame position 1
			"01010001" &
			"01000001",
			-- Frame position 2
			"01010010" &
			"01000010",
			-- Frame position 3
			"01010011" &
			"01000011",
			-- Frame position 4
			"01010100" &
			"01000100",
			-- Frame position 5
			"01010101" &
			"01000101",
			-- Frame position 6
			"01010110" &
			"01000110",
			-- Frame position 7
			"01010111" &
			"01000111",
			-- Frame position 8
			"01011000" &
			"01001000",
			-- Frame position 9
			"01011001" &
			"01001001",
			-- Frame position 10
			"01011010" &
			"01001010",
			-- Frame position 11
			"01011011" &
			"01001011",
			-- Frame position 12
			"01011100" &
			"01001100",
			-- Frame position 13
			"01011101" &
			"01001101",
			-- Frame position 14
			"01011110" &
			"01001110",
			-- Frame position 15
			"01011111" &
			"01001111",
			-- Time multiplexing : neurons 6 to 7
			-- Frame position 0
			"01110000" &
			"01100000",
			-- Frame position 1
			"01110001" &
			"01100001",
			-- Frame position 2
			"01110010" &
			"01100010",
			-- Frame position 3
			"01110011" &
			"01100011",
			-- Frame position 4
			"01110100" &
			"01100100",
			-- Frame position 5
			"01110101" &
			"01100101",
			-- Frame position 6
			"01110110" &
			"01100110",
			-- Frame position 7
			"01110111" &
			"01100111",
			-- Frame position 8
			"01111000" &
			"01101000",
			-- Frame position 9
			"01111001" &
			"01101001",
			-- Frame position 10
			"01111010" &
			"01101010",
			-- Frame position 11
			"01111011" &
			"01101011",
			-- Frame position 12
			"01111100" &
			"01101100",
			-- Frame position 13
			"01111101" &
			"01101101",
			-- Frame position 14
			"01111110" &
			"01101110",
			-- Frame position 15
			"01111111" &
			"01101111"
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

