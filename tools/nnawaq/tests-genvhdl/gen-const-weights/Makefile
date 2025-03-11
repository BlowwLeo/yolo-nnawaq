
RUNTOOL ?= ../../nnawaq

all :
	# Default
	$(MAKE) TM=1 PI=1  PO=1 onetest
	# With PAR_IN
	$(MAKE) TM=1 PI=2  PO=1 onetest
	$(MAKE) TM=1 PI=8  PO=1 onetest
	$(MAKE) TM=1 PI=16 PO=1 onetest
	# With PAR_OUT
	$(MAKE) TM=1 PI=1  PO=2 onetest
	$(MAKE) TM=1 PI=1  PO=4 onetest
	$(MAKE) TM=1 PI=1  PO=8 onetest
	# With PAR_IN and PAR_OUT
	$(MAKE) TM=1 PI=4  PO=4 onetest
	$(MAKE) TM=1 PI=16 PO=4 onetest
	$(MAKE) TM=1 PI=16 PO=8 onetest
	# With time multiplexing
	$(MAKE) TM=2 PI=1  PO=1 onetest
	$(MAKE) TM=4 PI=1  PO=1 onetest
	$(MAKE) TM=8 PI=1  PO=1 onetest

# Note: Tests with time mux and parallelism are not yet performed
# Because there is no use for this (yet)

# FIXME With TM and PAR_IN
# FIXME With TM and PAR_OUT
# FIXME With TM and PAR_IN and PAR_OUT

config_neu1.csv :
	./gen_weights.py -o $@

onetest : config_neu1.csv
	echo "Running test TM=$${TM} PI=$${PI} PO=$${PO}"
	$(RUNTOOL) -tcl constweights.tcl
	#cp neurons_const_weights.tm$${TM}pi$${PI}po$${PO}.{output,golden}.vhd
	diff -q neurons_const_weights.tm$${TM}pi$${PI}po$${PO}.{golden,output}.vhd

clean :
	rm -f *.output.vhd

