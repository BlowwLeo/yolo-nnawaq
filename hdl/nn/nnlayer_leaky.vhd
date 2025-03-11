
-- This block applies Leaky ReLU activation function

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity nnlayer_leaky_relu is
	generic(
		WDATA : natural := 8;
		SDATA : boolean := false;
		WOUT  : natural := 8;
		SOUT  : boolean := false;
		PAR   : natural := 1;
		-- Enable/disable input buffering and flow control
		INBUF : boolean := true;
		-- Take extra margin on the FIFO level, in case there is something outside
		FIFOMARGIN : natural := 0;
		-- Lock the layer parameters to the generic parameter value
		LOCKED : boolean := false
		--NSLOPE : sfixed(0 downto -4) := to_sfixed(0.1, 0, -4)
	);
	port(
		clk            : in  std_logic;
		-- Ports for address control
		clear          : in  std_logic;
		-- The user-specified range for linear output
		user_min       : in  std_logic_vector(WOUT downto 0);
		user_max       : in  std_logic_vector(WOUT downto 0);
		-- Data input
		data_in        : in  std_logic_vector(PAR*WDATA-1 downto 0);
		data_in_valid  : in  std_logic;
		data_in_ready  : out std_logic;
		-- Data output
		data_out       : out std_logic_vector(PAR*WOUT-1 downto 0);
		data_out_valid : out std_logic;
		-- The output data enters a FIFO. This indicates the available room.
		out_fifo_room  : in  std_logic_vector(15 downto 0)
	);
end nnlayer_leaky_relu;

architecture synth of nnlayer_leaky_relu is

	-- First stage buffers
	signal clear1      : std_logic := '0';
	signal data_in1    : std_logic_vector(PAR*WDATA-1 downto 0) := (others => '0');
	signal data_valid1 : std_logic := '0';

	-- One buffer to reduce routing pressure between input and output FIFOs
	-- This register indicates the input data is accepted
	signal buf_in_ready : std_logic := '0';

begin

	-------------------------------------------------------------------
	-- Input buffers
	-------------------------------------------------------------------

	gen_inbuf : if INBUF = true generate

		process(clk)
		begin
			if rising_edge(clk) then

				-- First stage buffers
				clear1      <= clear;
				data_in1    <= data_in;
				data_valid1 <= data_in_valid and buf_in_ready;

				-- Validate data entry in the pipeline
				buf_in_ready <= '0';
				if unsigned(out_fifo_room) >= 4 + FIFOMARGIN then
					buf_in_ready <= '1';
				end if;

				-- General clear
				if clear1 = '1' then
					data_valid1 <= '0';
				end if;

			end if;  -- Rising edge of clock
		end process;

	else generate

		-- Input is always enabled
		buf_in_ready <= '1';

		-- First stage buffers
		clear1      <= clear;
		data_in1    <= data_in;
		data_valid1 <= data_in_valid;

	end generate;

	-------------------------------------------------------------------
	-- Instantiate the parallel data processing paths
	-------------------------------------------------------------------

	gen_par: for p in 0 to PAR-1 generate

		signal loc_data  : std_logic_vector(WDATA-1 downto 0) := (others => '0');
		signal loc_valid : std_logic := '0';

	begin

		loc_data <= data_in1((p+1)*WDATA-1 downto p*WDATA);

		process(clk)
		    variable cmpneg  : std_logic := '0';
			variable cmplow : std_logic := '0';
			variable cmpup  : std_logic := '0';
			
		begin
			if rising_edge(clk) then

				-- Comparators
				if SDATA = true then
				    cmpneg := '1' when signed(loc_data) < 0 else '0';
					cmplow := '1' when signed(loc_data) < signed(user_min) else '0';
					cmpup  := '1' when signed(loc_data) > signed(user_max) else '0';
				else
				    cmpneg := '0';
					cmplow := '1' when unsigned(loc_data) < unsigned(user_min) else '0';
					cmpup  := '1' when unsigned(loc_data) > unsigned(user_max) else '0';
				end if;

				-- Multiplexer
				data_out((p+1)*WOUT-1 downto p*WOUT) <=
					user_min(WOUT-1 downto 0) when cmplow = '1' else
					user_max(WOUT-1 downto 0) when cmpup = '1' else					
					
					std_logic_vector(resize(unsigned(loc_data), WOUT)) when SOUT = false else
					std_logic_vector(resize(  signed(loc_data), WOUT)) when cmpneg = '0' else
					std_logic_vector(shift_right(resize(signed(loc_data), WOUT), 3)); -- Multiplication par 0.12
                    
				-- Tag
				loc_valid <= data_valid1;

				-- General clear
				if clear1 = '1' then
					loc_valid <= '0';
				end if;

			end if;
		end process;

		-- Only one component is driving the output valid signal
		gen_out_valid : if p = 0 generate
			data_out_valid <= loc_valid;
		end generate;

	end generate;

	-------------------------------------------------------------------
	-- Output ports
	-------------------------------------------------------------------

	data_in_ready <= buf_in_ready;

end architecture;

