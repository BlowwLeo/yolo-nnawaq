
RUNTOOL ?= ../../nnawaq

all :
	$(MAKE) TESTPREFIX=test1_ onetest

onetest :
	$(RUNTOOL) -tcl ternarization.tcl
	diff -q $(TESTPREFIX)output_golden.csv $(TESTPREFIX)output.csv

clean :
	rm -f *output.csv
