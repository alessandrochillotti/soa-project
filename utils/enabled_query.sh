#!/bin/bash

# if less than one argument supplied, display usage 
if [ $# -ne 1 ] 
then 
    echo "Usage: ${0} MINOR"
    exit 1
fi

if [ $1 -lt 0 -o $1 -gt 127 ]
then
	echo "The minor must be a number between 0 to 127."
	exit 1
fi

index=$(($1+1))

cut -d, -f $index /sys/module/multi_flow_dev/parameters/enabled
