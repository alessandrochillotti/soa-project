#!/bin/bash

# if less than one argument supplied, display usage 
if [ $# -ne 3 ] 
then 
    echo "Usage: ${0} PATH MAJOR MINOR"
    exit 1
fi

if [ $3 -lt 0 -o $3 -gt 127 ]
then
	echo "The minor must be a number between 0 to 127."
	exit 1
fi

mknod $1 c $2 $3
