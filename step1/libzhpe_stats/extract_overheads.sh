#!/bin/bash

INPUT=$1

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# get basic start/stop overhead
LINE=`grep "subid: 1;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15; SUM5+=$18; SUM6+=$21} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

BASIC_V1=`echo $LINE | awk '{printf"%f", $3}'`
BASIC_V2=`echo $LINE | awk '{printf"%f", $6}'`
BASIC_V3=`echo $LINE | awk '{printf"%f", $9}'`
BASIC_V4=`echo $LINE | awk '{printf"%f", $12}'`
BASIC_V5=`echo $LINE | awk '{printf"%f", $15}'`
BASIC_V6=`echo $LINE | awk '{printf"%f", $18}'`

# get nested stamp overhead
LINE=`grep "subid: 2;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15; SUM5+=$18; SUM6+=$21} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

TMP_V1=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V4=`echo $LINE | awk '{printf"%f",$12}'`
TMP_V5=`echo $LINE | awk '{printf"%f",$15}'`
TMP_V6=`echo $LINE | awk '{printf"%f",$18}'`

STAMP_V1=`echo "$TMP_V1 - $BASIC_V1" | bc -l`
STAMP_V2=`echo "$TMP_V2 - $BASIC_V2" | bc -l`
STAMP_V3=`echo "$TMP_V3 - $BASIC_V3" | bc -l`
STAMP_V4=`echo "$TMP_V4 - $BASIC_V4" | bc -l`
STAMP_V5=`echo "$TMP_V5 - $BASIC_V5" | bc -l`
STAMP_V6=`echo "$TMP_V6 - $BASIC_V6" | bc -l`

# get nested start/stop overhead
LINE=`grep "subid: 3;" ${INPUT}.dat.matched | \
awk 'BEGIN {SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM1+=$6; SUM2+=$9; SUM3+=$12; SUM4+=$15; SUM5+=$18; SUM6+=$21} END {printf"avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

TMP_V1=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V4=`echo $LINE | awk '{printf"%f",$12}'`
TMP_V5=`echo $LINE | awk '{printf"%f",$15}'`
TMP_V6=`echo $LINE | awk '{printf"%f",$18}'`

STARTSTOP_V1=`echo "scale=4; $TMP_V1 - $BASIC_V1" | bc`
STARTSTOP_V2=`echo "scale=4; $TMP_V2 - $BASIC_V2" | bc`
STARTSTOP_V3=`echo "scale=4; $TMP_V3 - $BASIC_V3" | bc`
STARTSTOP_V4=`echo "scale=4; $TMP_V4 - $BASIC_V4" | bc`
STARTSTOP_V5=`echo "scale=4; $TMP_V5 - $BASIC_V5" | bc`
STARTSTOP_V6=`echo "scale=4; $TMP_V6 - $BASIC_V6" | bc`

for (( i=1;i<=6;i++ ))
do
    for j in BASIC STAMP STARTSTOP
    do
        vname="${j}_V${i}"
        echo "${vname}: ${!vname}"
    done
    echo ""
done
