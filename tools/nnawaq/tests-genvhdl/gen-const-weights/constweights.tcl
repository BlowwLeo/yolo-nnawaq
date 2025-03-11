#!./nnawaq -tcl

# This TCL script is intended to be executed by the tool nnawaq

# Input images : 1x1x16
# Input data : 8b signed

global env

set tm 1
set pi 1
set po 1

if {[info exists env(TM)]} {
	set tm $env(TM)
}
if {[info exists env(PI)]} {
	set pi $env(PI)
}
if {[info exists env(PO)]} {
	set po $env(PO)
}

nn_set f=1/1/16
nn_set fn=1
nn_set inpar=1

nn_set in=8u
nn_set weights=8u

# Default values for ReLU, just model a kind of crop for the sake of bit width
nn_set relu=0/255

# Create the network
# Use a different number for fsize and neurons
# Use phony neuron layers before and after to not being limited by interface widthes
nn_layer_create neurons neu=16 paro=$pi
nn_layer_create relu
nn_layer_create window win=1x1 step=1x1 pad=0x0 repeat=$tm
nn_layer_create neurons neu=8 paro=$po weights_const=true tmux=$tm
nn_layer_create relu
nn_layer_create neurons neu=8

nn_print -cycles
nn_finalize_hw_config

# Assign config file
nn_layer_set neu1 cfg=config_neu1.csv

# Generate the constant weights

set fi neurons_const_weights.template.vhd
set fo neurons_const_weights.tm${tm}pi${pi}po${po}.output.vhd
nn_genvhdl_const_weights $fi $fo

