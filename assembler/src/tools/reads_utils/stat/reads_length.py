#!/usr/bin/python

#Count

import sys
import os

if len(sys.argv) != 2:
        print("Usage: <fastq file>");
        exit(0)

inFile = open(sys.argv[1])

line = inFile.readline();

lens = [0 for i in range(101)]

while line:
	line = inFile.readline();

	lens[len(line.strip())] += 1

	inFile.readline();
	inFile.readline();
	line = inFile.readline();

summ = 0
for i in range(100,50, -1):
	summ += lens[i]
	print(">=" + str(i) + ": " + str(summ))

	
