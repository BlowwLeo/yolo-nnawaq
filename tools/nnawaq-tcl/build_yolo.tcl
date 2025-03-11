#!./nnawaq -tcl

global env

set par 1
if {[info exists env(PAR)]} {
    set par $env(PAR)
}

set freq 250
if {[info exists env(FREQ)]} {
    set freq $env(FREQ)
}

set neu_mul_id 2

# Input shape: 416x416x3
nn_set fn=1
nn_set f=416/416/3

nn_set in=8s
nn_set acts=2s
nn_set weights=8s
nn_set round_near=true
nn_set neu_style=2
set neu_mul_id custom_mul=2

# ---------------------------------------------------
# Definition de la procedure create_nn_conv_neu
# ---------------------------------------------------
proc create_nn_conv_neu {win step pad neu nwin} {
    global neu_mul_id
    nn_layer_create window $win $step $pad $nwin
    nn_layer_create neuron $neu $neu_mul_id
    nn_layer_create norm
    nn_layer_create relu
}



# Convolution layer 0
create_nn_conv_neu win=3 step=1 pad=1 neu=16 nwin=416x416

# MaxPool layer 1

nn_layer_create window win=2x2 step=2x2 pad=0x0 nwin=208x208
nn_layer_create maxpool

# Convolution layer 2
create_nn_conv_neu win=3 step=1 pad=1 neu=32 nwin=208x208

# MaxPool layer 3
nn_layer_create window win=2x2 step=2x2 pad=0x0 nwin=104x104
nn_layer_create maxpool

# Convolution layer 4
create_nn_conv_neu win=3 step=1 pad=1 neu=64 nwin=104x104

# MaxPool layer 5
nn_layer_create window win=2x2 step=2x2 pad=0x0 nwin=52x52
nn_layer_create maxpool

# Convolution layer 6
create_nn_conv_neu win=3 step=1 pad=1 neu=128 nwin=52x52

# MaxPool layer 7
nn_layer_create window win=2x2 step=2x2 pad=0x0 nwin=26x26
nn_layer_create maxpool

# Convolution layer 8
create_nn_conv_neu win=3 step=1 pad=1 neu=256 nwin=26x26

# MaxPool layer 9
nn_layer_create window win=2x2 step=2x2 pad=0x0 nwin=13x13
nn_layer_create maxpool

# Convolution layer 10
create_nn_conv_neu win=3 step=1 pad=1 neu=512 nwin=13x13

# MaxPool layer 11
nn_layer_create window win=2x2 step=1x1 pad=0x0 nwin=13x13
nn_layer_create maxpool

# Convolution layer 12
create_nn_conv_neu win=3 step=1 pad=1 neu=1024 nwin=13x13

# Convolution layer 13
create_nn_conv_neu win=3 step=1 pad=1 neu=512 nwin=13x13

# Convolution layer 14
create_nn_conv_neu win=1 step=1 pad=0 neu=425 nwin=13x13

# IGNORED region layer 15

# ---------------------------------------------------
# Post-processing: parallelism, nn_print, etc.
# ---------------------------------------------------

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
set layer_bot [nn_get bottleneck]
set cycles_fast [nn_layer_get $layer_bot cycles_max]
puts "Bottleneck layer is $layer_bot : input cycles [nn_layer_get $layer_bot cycles_in], output cycles [nn_layer_get $layer_bot cycles_out]"
puts "Acceleration : cycles $cycles_orig -> $cycles_fast, ratio [expr 1.0 * $cycles_orig / $cycles_fast]"
puts "Performance (assuming $freq MHz) : [expr 1000000.0 * $freq / $cycles_fast] images/sec"

nn_finalize_hw_config
puts "Total number of layers : [nn_get layers_nb] layers"
nn_print -type-neu

# Optional memory printing
foreach layer [list win neu ter] { nn_print_mem -type-$layer }
nn_print_mem -total

puts "END BUILD YOLO"