
	-- AUTOGEN CONST WEIGHTS BEGIN

	if LAYER_ID = 2 generate

		type memweight_type is array (0 to 16-1) of std_logic_vector(WDATA*PAR_OUT-1 downto 0);
		signal memweight : memweight_type := (
			-- Frame position 0
			-- Neurons 1 mod PAR_OUT=2
			"01110000" &
			"01010000" &
			"00110000" &
			"00010000" &
			-- Neurons 0 mod PAR_OUT=2
			"01100000" &
			"01000000" &
			"00100000" &
			"00000000",
			-- Frame position 1
			-- Neurons 1 mod PAR_OUT=2
			"01110001" &
			"01010001" &
			"00110001" &
			"00010001" &
			-- Neurons 0 mod PAR_OUT=2
			"01100001" &
			"01000001" &
			"00100001" &
			"00000001",
			-- Frame position 2
			-- Neurons 1 mod PAR_OUT=2
			"01110010" &
			"01010010" &
			"00110010" &
			"00010010" &
			-- Neurons 0 mod PAR_OUT=2
			"01100010" &
			"01000010" &
			"00100010" &
			"00000010",
			-- Frame position 3
			-- Neurons 1 mod PAR_OUT=2
			"01110011" &
			"01010011" &
			"00110011" &
			"00010011" &
			-- Neurons 0 mod PAR_OUT=2
			"01100011" &
			"01000011" &
			"00100011" &
			"00000011",
			-- Frame position 4
			-- Neurons 1 mod PAR_OUT=2
			"01110100" &
			"01010100" &
			"00110100" &
			"00010100" &
			-- Neurons 0 mod PAR_OUT=2
			"01100100" &
			"01000100" &
			"00100100" &
			"00000100",
			-- Frame position 5
			-- Neurons 1 mod PAR_OUT=2
			"01110101" &
			"01010101" &
			"00110101" &
			"00010101" &
			-- Neurons 0 mod PAR_OUT=2
			"01100101" &
			"01000101" &
			"00100101" &
			"00000101",
			-- Frame position 6
			-- Neurons 1 mod PAR_OUT=2
			"01110110" &
			"01010110" &
			"00110110" &
			"00010110" &
			-- Neurons 0 mod PAR_OUT=2
			"01100110" &
			"01000110" &
			"00100110" &
			"00000110",
			-- Frame position 7
			-- Neurons 1 mod PAR_OUT=2
			"01110111" &
			"01010111" &
			"00110111" &
			"00010111" &
			-- Neurons 0 mod PAR_OUT=2
			"01100111" &
			"01000111" &
			"00100111" &
			"00000111",
			-- Frame position 8
			-- Neurons 1 mod PAR_OUT=2
			"01111000" &
			"01011000" &
			"00111000" &
			"00011000" &
			-- Neurons 0 mod PAR_OUT=2
			"01101000" &
			"01001000" &
			"00101000" &
			"00001000",
			-- Frame position 9
			-- Neurons 1 mod PAR_OUT=2
			"01111001" &
			"01011001" &
			"00111001" &
			"00011001" &
			-- Neurons 0 mod PAR_OUT=2
			"01101001" &
			"01001001" &
			"00101001" &
			"00001001",
			-- Frame position 10
			-- Neurons 1 mod PAR_OUT=2
			"01111010" &
			"01011010" &
			"00111010" &
			"00011010" &
			-- Neurons 0 mod PAR_OUT=2
			"01101010" &
			"01001010" &
			"00101010" &
			"00001010",
			-- Frame position 11
			-- Neurons 1 mod PAR_OUT=2
			"01111011" &
			"01011011" &
			"00111011" &
			"00011011" &
			-- Neurons 0 mod PAR_OUT=2
			"01101011" &
			"01001011" &
			"00101011" &
			"00001011",
			-- Frame position 12
			-- Neurons 1 mod PAR_OUT=2
			"01111100" &
			"01011100" &
			"00111100" &
			"00011100" &
			-- Neurons 0 mod PAR_OUT=2
			"01101100" &
			"01001100" &
			"00101100" &
			"00001100",
			-- Frame position 13
			-- Neurons 1 mod PAR_OUT=2
			"01111101" &
			"01011101" &
			"00111101" &
			"00011101" &
			-- Neurons 0 mod PAR_OUT=2
			"01101101" &
			"01001101" &
			"00101101" &
			"00001101",
			-- Frame position 14
			-- Neurons 1 mod PAR_OUT=2
			"01111110" &
			"01011110" &
			"00111110" &
			"00011110" &
			-- Neurons 0 mod PAR_OUT=2
			"01101110" &
			"01001110" &
			"00101110" &
			"00001110",
			-- Frame position 15
			-- Neurons 1 mod PAR_OUT=2
			"01111111" &
			"01011111" &
			"00111111" &
			"00011111" &
			-- Neurons 0 mod PAR_OUT=2
			"01101111" &
			"01001111" &
			"00101111" &
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

