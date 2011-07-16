#!/bin/bash
# Plots some of the process-switching data using gnuplot.

# SCRIPT CONFIG
GNUPLOT=$(which gnuplot)

if [ $# -eq 0 ] ; then
cat <<EOF
Usage: $0 [options] <data_file>

The "process-switching" test application sends human-readable output to
STDOUT, and GNUPLOT-friendly data to STDERR.  If you save this STDERR
stream to a file, this script will give you a nice plot of the data
using gnuplot.

Options:
   -o FN     Output a PNG to file FN instead of displaying.
EOF
    exit 0
fi

if [ ! -x "$GNUPLOT" ] ; then
    echo "ERROR: gnuplot not found in PATH"
    exit 1
fi

unset PNG_MODE
unset OUTFILE
unset INFILE
PAUSE_AT_END=yes

# PARSE OPTIONS
while [ $# -gt 0 ] ; do
    case $1 in
        "-o")
            if [ $# -lt 2 ] ; then
                echo "ERROR: the -o paramater takes an argument"
                exit 1
            fi
            PNG_MODE=yes
            OUTFILE="$2"
            shift
            ;;
        *)
            INFILE="$1"
            ;;
    esac
    shift
done
    
if [ "$PNG_MODE" = "yes" ] ; then
    unset PAUSE_AT_END
fi

# PLOTTING CONFIG
# Mapping column numbers to variables to make it easier
# to update the column order.
NTHREADS=1
CYC_TIME=9
CYC_TIME_STDDEV=10
THD_SW_TIME=13
THD_SW_TIME_STDDEV=14

# Data note:  While there is a `max` column for the test runs, the
# cycle max is being reported a 3*STDDEV.  Assuming that the data
# has a normal distribution, the time will be < AVG + 3*STDDEV
# 99.9% of the time.
cat <<EOF | "$GNUPLOT" -
${PNG_MODE:+set term png}
${OUTFILE:+set output '$OUTFILE'}
set key left top
set key box linestyle 1
plot '$INFILE' using ${NTHREADS}:${CYC_TIME} title "cycle time", \
     '$INFILE' using ${NTHREADS}:${THD_SW_TIME} title "thread switch time", \
     '$INFILE' using ${NTHREADS}:(\$${CYC_TIME}+3*\$${CYC_TIME_STDDEV}) title "cycle max", \
     '$INFILE' using ${NTHREADS}:(\$${THD_SW_TIME}+3*\$${THD_SW_TIME_STDDEV}) title "thread switch max"
${PAUSE_AT_END:+pause mouse}

EOF


