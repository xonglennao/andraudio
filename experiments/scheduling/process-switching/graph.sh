#!/bin/sh

DATA=$1
PNG=$2

if [ -z "$DATA" ] || [ -z "$PNG" ]
then
    echo "Usage: `basename $0` <data.txt> <output.png>"
    exit 1
fi

cat << EOF | gnuplot 
set terminal png
set output "$PNG"
set key left top
set key box linestyle 1
set xlabel "Number of processes"
set ylabel "Milliseconds"
plot '$DATA' using 1:(\$9/1000) title "cycle time", \
     '$DATA' using 1:(\$13/1000) title "thread switch time", \
     '$DATA' using 1:((\$9+3*\$10)/1000) title "cycle max", \
     '$DATA' using 1:((\$13+3*\$14)/1000) title "thread switch max"
EOF
