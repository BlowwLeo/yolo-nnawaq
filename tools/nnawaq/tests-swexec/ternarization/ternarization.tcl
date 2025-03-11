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
nn_layer_create ternarize

nn_print -cycles
nn_finalize_hw_config

# Assign config file
nn_layer_set ter0 cfg=$env(TESTPREFIX)config_ter0.csv

# Set input frames
nn_set frames=$env(TESTPREFIX)frames.csv

# Run

nn_set floop=1 ml=1
nn_set fn=1 oraw=1
nn_set o=$env(TESTPREFIX)output.csv

nn_swexec

