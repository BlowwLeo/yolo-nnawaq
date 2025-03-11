
# Compilation flags and sources

CFLAGS   = -Wall -std=gnu99
CXXFLAGS = -Wall -std=gnu++14
LDFLAGS  =

LDLIBS  = -lrt -lm -lpthread

ifdef DEBUG
	CFLAGS   += -g -O0
	CXXFLAGS += -g -O0
	LDFLAGS  +=
else
	CFLAGS   += -O2 -DNDEBUG
	CXXFLAGS += -O2 -DNDEBUG
	LDFLAGS  += -s
endif

SRC = \
	load_config.c \
	nnawaq_utils.c

SRCPP = \
	hw_reg_fields.cpp \
	hwacc_common.cpp \
	hwacc_run.cpp \
	mem_implem.cpp \
	nnawaq.cpp \
	nn_hw_config.cpp \
	nn_hwacc_config.cpp \
	nn_layers_create.cpp \
	nn_layers_utils.cpp \
	nn_load_config.cpp \
	swexec.cpp

ifdef LIMITED
	CFLAGS   += -DLIMITED
	CXXFLAGS += -DLIMITED
else
	SRCPP += genvhdl.cpp estimasic.cpp compress.cpp
endif

# Detection of Riffa

ifeq ("$(shell /sbin/ldconfig -p | grep libriffa | head -n 1)","")
	NORIFFA = 1
endif

ifeq ("$(NORIFFA)","")
	SRCPP  += hwacc_pcieriffa.cpp
	LDLIBS += -lriffa
	CFLAGS   += -DHAVE_RIFFA
	CXXFLAGS += -DHAVE_RIFFA
endif

# Support of Zynq-7000

ifeq ("$(shell test -e /dev/xdevcfg && echo -n "true")","")
	NOZYNQ7 = 1
endif

ifeq ("$(ZYNQ7)","1")
	NOZYNQ7 =
endif

ifeq ("$(NOZYNQ7)","")
	SRCPP  += hwacc_zynq7.cpp
	CFLAGS   += -DHAVE_ZYNQ7
	CXXFLAGS += -DHAVE_ZYNQ7
endif

# Detection of TCL

ifeq ($(shell pkg-config --exists tcl || echo fail),fail)
	NOTCL = 1
else
	# Note: On Debian, the header tcl.h is another package tcl-dev that may not be installed
	# So we need to check if tcl.h exists indeed (Debian style or not)
	d := $(shell pkg-config --cflags-only-I tcl | sed -nE 's/^-I([^ ]+).*$$/\1/p')
	ifeq ("$(d)","")
		d := /usr/include/
	endif
	ifeq ($(shell test -f $(d)/tcl.h || echo fail),fail)
		NOTCL = 1
	endif
endif

ifeq ("$(NOTCL)","")
	SRCPP    += tcl_parser.cpp
	CFLAGS   += $(shell pkg-config --cflags tcl)
	CXXFLAGS += $(shell pkg-config --cflags tcl)
	LDLIBS   += $(shell pkg-config --libs tcl)
else
	CFLAGS   += -DNOTCL
	CXXFLAGS += -DNOTCL
endif

OBJ   = $(patsubst %.c,%.o,$(SRC))
OBJPP = $(patsubst %.cpp,%.o,$(SRCPP))
SRCH  = $(wildcard *.h)
PROG  = nnawaq


# Compilation recipe

.PHONY: clean

$(PROG): $(OBJPP) $(OBJ)
	g++ -fsanitize=undefined $(LDFLAGS) -g -o $@ $^ $(LDLIBS)

%.o : %.c $(SRCH)
	gcc -fsanitize=undefined $(CFLAGS) -c -g -o $@ $<

%.o : %.cpp $(SRCH)
	g++ -fsanitize=undefined $(CXXFLAGS) -c -g -o $@ $<

clean:
	rm -f *.o
	rm -f $(PROG)


# Test recipes

tests :
	$(MAKE) -C tests-genvhdl/gen-const-weights
	$(MAKE) -C tests-swexec/ternarization
	$(MAKE) -C tests-swexec/scatter-gather


# Handy alias command to launch debugging tools

LAUNCH = ./$(PROG)

ifdef DEBUG
	LAUNCH = gdb --args ./$(PROG)
endif

ifdef VALGRIND
	LAUNCH = valgrind ./$(PROG)
endif


# Recipes specific for Riffa PCIe framework

.PHONY: riffa-reset

riffa-reset: $(PROG)
	$(LAUNCH) -riffa-reset

# Recipes specific for Zynq-7000 framework

.PHONY: zynq7-reset

zynq7-reset: $(PROG)
	$(LAUNCH) -zynq7-init -hwacc-clear

# Recipes for basic HwAcc control

.PHONY: regs nnregs print fifos reset

# Possible values : riffa, zynq7, hwacc
HWID = hwacc

regs: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-regs

nnregs: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-nnregs

print: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-build -print

fifos: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-fifos

clkfreq: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-clkfreq 100

reset: $(PROG)
	$(LAUNCH) -$(HWID)-init -hwacc-clear

