#!/bin/bash

# if less than one argument supplied, display usage 
if [ $# -ne 2 ] 
then 
    echo "Usage: ${0} MINOR ENABLED (Y,N)"
    exit 1
fi

if [ $1 -lt 0 -o $1 -gt 127 ]
then
	echo "The minor must be a number between 0 to 127."
	exit 1
fi

if [ $2 != 'Y' -a $2 != 'N' ] 
then
	echo "The enabled flag must be Y or N."
	exit 1;
fi

index=$(($1+1))

cp /sys/module/multi_flow_driver/parameters/enabled /tmp/to_delete

awk 'BEGIN{FS=OFS=","} {if (NR==1) {$'${index}' = "'${2}'"}; print}' /tmp/to_delete > /sys/module/multi_flow_driver/parameters/enabled

rm /tmp/to_delete
