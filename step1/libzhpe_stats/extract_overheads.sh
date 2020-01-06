#!/bin/bash

INPUT=$1

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# get basic start/stop overhead
LINE=`grep "subid: 1;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR}'`

BASIC_V1=`echo $LINE | awk '{print $3}'`
BASIC_V2=`echo $LINE | awk '{print $6}'`
BASIC_V3=`echo $LINE | awk '{print $9}'`
BASIC_V4=`echo $LINE | awk '{print $12}'`

# get nested stamp overhead
LINE=`grep "subid: 2;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR}'`

TMP_V1=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V4=`echo $LINE | awk '{print $12}'`

STAMP_V1=`echo "$TMP_V1 - $BASIC_V1" | bc -l`
STAMP_V2=`echo "$TMP_V2 - $BASIC_V2" | bc -l`
STAMP_V3=`echo "$TMP_V3 - $BASIC_V3" | bc -l`
STAMP_V4=`echo "$TMP_V4 - $BASIC_V4" | bc -l`

# get nested start/stop overhead
LINE=`grep "subid: 3;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR}'`

TMP_V1=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V4=`echo $LINE | awk '{printf"%f",$12}'`

STARTSTOP_V1=`echo "scale=4; $TMP_V1 - $BASIC_V1" | bc`
STARTSTOP_V2=`echo "scale=4; $TMP_V2 - $BASIC_V2" | bc`
STARTSTOP_V3=`echo "scale=4; $TMP_V3 - $BASIC_V3" | bc`
STARTSTOP_V4=`echo "scale=4; $TMP_V4 - $BASIC_V4" | bc`

for (( i=1;i<=4;i++ ))
do
    for j in BASIC STAMP STARTSTOP
    do
        vname="${j}_V${i}"
        echo "${vname}: ${!vname}"
    done
    echo ""
done