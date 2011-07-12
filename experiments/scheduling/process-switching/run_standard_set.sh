#!/bin/sh

if [ ! -x ./process-switching ] ; then
    echo "ERROR: the program ./process-switching is missing"
    exit
fi

for N in 8 16 32 64 ; do
    for I in 20000 40000 60000 ; do
        for D in 3 30 300 ; do
            ./process-switching $N $I $D
        done
    done
done
