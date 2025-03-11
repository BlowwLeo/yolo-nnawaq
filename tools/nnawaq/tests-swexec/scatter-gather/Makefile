
RUNTOOL ?= ../../nnawaq

all :
	$(MAKE) test1
	$(MAKE) test2

# Parallel branch 1 : Supposed to receive 8 values from each frame (position in frame is an even number)
# Parallel branch 2 : Supposed to receive 5 values from each frame (position in frame is a prime number)
# Parallel branch 3 : Supposed to receive 3 values from each frame (other positions in frame)
# Output is same than input
test1 :
	$(MAKE) TESTPREFIX=test1_ NN_SZ_PAR1=8 NN_SZ_PAR2=5 NN_SZ_PAR3=3 NN_SZ_GAT=16 test1-inner
test1-inner :
	$(RUNTOOL) -o $(TESTPREFIX)config_neu0.csv -gencsv-id $(NN_SZ_PAR1) $(NN_SZ_PAR1)
	$(RUNTOOL) -o $(TESTPREFIX)config_neu1.csv -gencsv-id $(NN_SZ_PAR2) $(NN_SZ_PAR2)
	$(RUNTOOL) -o $(TESTPREFIX)config_neu2.csv -gencsv-id $(NN_SZ_PAR3) $(NN_SZ_PAR3)
	NN_OL=neu0 $(RUNTOOL) -tcl scatter-gather.tcl
	NN_OL=neu1 $(RUNTOOL) -tcl scatter-gather.tcl
	NN_OL=neu2 $(RUNTOOL) -tcl scatter-gather.tcl
	$(RUNTOOL) -tcl scatter-gather.tcl
	diff -q $(TESTPREFIX)output_neu0.golden.csv $(TESTPREFIX)output_neu0.csv
	diff -q $(TESTPREFIX)output_neu1.golden.csv $(TESTPREFIX)output_neu1.csv
	diff -q $(TESTPREFIX)output_neu2.golden.csv $(TESTPREFIX)output_neu2.csv
	diff -q $(TESTPREFIX)frames.csv $(TESTPREFIX)output.csv

# Parallel branch 1 : Supposed to receive 13 values from each frame (all inputs except prime numbers > 5)
# Parallel branch 2 : Supposed to receive 6 values from each frame (position in frame is multiple of 3)
# Parallel branch 3 : Supposed to receive 4 values from each frame (position in frame is multiple of 5)
# Output spreads inputs with some empty values between inputs
test2 :
	$(MAKE) TESTPREFIX=test2_ NN_SZ_PAR1=13 NN_SZ_PAR2=6 NN_SZ_PAR3=4 NN_SZ_GAT=32 test2-inner
test2-inner :
	cp test1_frames.csv $(TESTPREFIX)frames.csv
	$(RUNTOOL) -o $(TESTPREFIX)config_neu0.csv -gencsv-id $(NN_SZ_PAR1) $(NN_SZ_PAR1)
	$(RUNTOOL) -o $(TESTPREFIX)config_neu1.csv -gencsv-id $(NN_SZ_PAR2) $(NN_SZ_PAR2)
	$(RUNTOOL) -o $(TESTPREFIX)config_neu2.csv -gencsv-id $(NN_SZ_PAR3) $(NN_SZ_PAR3)
	$(RUNTOOL) -tcl scatter-gather.tcl
	diff -q $(TESTPREFIX)output.golden.csv $(TESTPREFIX)output.csv

clean :
	rm -f *output.csv
