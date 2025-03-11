#!/usr/bin/env python3

import argparse

parser = argparse.ArgumentParser(description="Neuron config file generator")
parser.add_argument('-f', dest="fsize", nargs='?', type=int, default=16, help="Frame size")
parser.add_argument('-n', dest="neu",   nargs='?', type=int, default=8, help="Number of neurons")
parser.add_argument('-o', dest="fname", nargs='?', default="out.csv", help="Destination CSV file")

args = parser.parse_args()

fo = open(args.fname, "w")

fsize = args.fsize
neu   = args.neu

for n in range(0, neu) :
	if n > 0 :
		fo.write("\n")
	for f in range(0, fsize) :
		if f > 0 :
			fo.write(",")
		fo.write("{}".format(n*fsize+f))

