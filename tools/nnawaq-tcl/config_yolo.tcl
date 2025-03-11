#!./nnawaq -tcl

# This TCL script is intended to be executed by the tool nnawaq
# Configuration script for YOLO network, using CSV weights & biases.

global env

set cfg_dir ../../datasets/YOLO/csv_params

nn_set frames=../../datasets/YOLO/input/pixels_rgb.csv


set par 1
if {[info exists env(PAR)]} {
    set par $env(PAR)
}

set freq 250
if {[info exists env(FREQ)]} {
    set freq $env(FREQ)
}

nn_set fn=1
nn_set f=416/416/3
#nn_set worder=zxy
nn_set in=8u
nn_set acts=8s
nn_set weights=8s
nn_set ml=1
nn_set neu_style=1

nn_set debug=true

puts "Summary of parameters :"
puts "  PAR ...... $par"
puts "  FREQ ..... $freq"

#------------------------------------------------------
# Etape 2 : Chargement des poids/biais depuis CSV
#------------------------------------------------------

for {set i 0} {$i < 9} {incr i} {
    #nn_layer_set neu$i worder=xfirst
	nn_layer_set neu_cm$i cfg=$cfg_dir/weights$i.csv
}

# Set config file for normalization layers
for {set i 0} {$i < 8} {incr i} {
	nn_layer_set norm_cm$i cfg=$cfg_dir/norm$i.csv
}

#------------------------------------------------------
# Etape 3 : nn_print, nn_autopar, etc.
#------------------------------------------------------

nn_print -cycles
set layer_bot [nn_get bottleneck]
set cycles_orig [nn_layer_get $layer_bot cycles_max]
puts "Bottleneck layer is $layer_bot : input cycles [nn_layer_get $layer_bot cycles_in], output cycles [nn_layer_get $layer_bot cycles_out]"

puts "Applying parallelism levels for PAR=$par"
nn_autopar $par
puts "Applying time multiplexing to fastest layers"
nn_autotmux
puts "Applying more parallelism to reduce the number of physical neurons"
nn_maxparin

nn_print -cycles
set cycles_fast [nn_layer_get $layer_bot cycles_max]
puts "Acceleration : cycles $cycles_orig -> $cycles_fast, ratio [expr 1.0 * $cycles_orig / $cycles_fast]"
puts "Performance (assuming $freq MHz) : [expr 1000000.0 * $freq / $cycles_fast] images/sec"

nn_finalize_hw_config
puts "Total number of layers : [nn_get layers_nb] layers"

# Print memory usage
foreach layer [list win neu] { nn_print_mem -type-$layer }
nn_print_mem -total

