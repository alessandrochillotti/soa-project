#!/bin/bash

# if less than one argument supplied, display usage 
if [ $# -ne 2 ] 
then 
    echo "Usage: ${0} MINOR PRIORITY (LOW = 0, HIGH = 1)"
    exit 1
fi

if [ $1 -lt 0 -o $1 -gt 127 ]
then
	echo "The minor must be a number between 0 to 127."
	exit 1
fi

if [ $2 -lt 0 -o $2 -gt 1 ]
then
	echo "The priority must be 0 (LOW) or 1 (HIGH)."
 	exit 1
fi

index=$(($2*128+$1+1))

cut -d, -f $index /sys/module/multi_flow_dev/parameters/byte_in_buffer
