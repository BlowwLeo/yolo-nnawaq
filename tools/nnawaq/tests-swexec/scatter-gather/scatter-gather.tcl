#!./nnawaq -tcl

# This TCL script is intended to be executed by the tool nnawaq

# Input images : 1x1x16
# Input data : 8b signed

global env

nn_set f=1/1/16
nn_set fn=1
nn_set inpar=1

nn_set in=8s

# Create the network
# Note : there are neuron layers inside the parallel branches, just to have identifiers for get outputs

# Scatter layer
nn_layer_create scatter

set scatter_id [nn_get last]
set gat_prev [list ]

# Parallel part 1
set sz $env(NN_SZ_PAR1)
nn_layer_create neurons neu=$sz prev=$scatter_id fz=$sz fsize=$sz
lappend gat_prev prev=[nn_get last]

# Parallel part 2
set sz $env(NN_SZ_PAR2)
nn_layer_create neurons neu=$sz prev=$scatter_id fz=$sz fsize=$sz
lappend gat_prev prev=[nn_get last]

# Parallel part 3
set sz $env(NN_SZ_PAR3)
nn_layer_create neurons neu=$sz prev=$scatter_id fz=$sz fsize=$sz
lappend gat_prev prev=[nn_get last]

# Gather layer
set sz $env(NN_SZ_GAT)
nn_layer_create gather $gat_prev fz=$sz fsize=$sz

nn_print -cycles
nn_finalize_hw_config

# Assign config files

# Same config to obtain outputs identical to inputs
nn_layer_set sca0 cfg=$env(TESTPREFIX)config_sca0.csv
nn_layer_set gat0 cfg=$env(TESTPREFIX)config_gat0.csv

# These are supposed to be identity matrices, so data is unaffected, which simplifies testing
nn_layer_set neu0 cfg=$env(TESTPREFIX)config_neu0.csv
nn_layer_set neu1 cfg=$env(TESTPREFIX)config_neu1.csv
nn_layer_set neu2 cfg=$env(TESTPREFIX)config_neu2.csv

# Set input frames
nn_set frames=$env(TESTPREFIX)frames.csv

# Run

nn_set floop=1 ml=1
nn_set fn=2 oraw=1

if {[info exists env(NN_FN)]} {
	nn_set fn=$env(NN_FN)
}

if {[info exists env(NN_OL)]} {
	nn_set ol=$env(NN_OL)
	nn_set o=$env(TESTPREFIX)output_$env(NN_OL).csv
} else {
	nn_set o=$env(TESTPREFIX)output.csv
}

nn_swexec

