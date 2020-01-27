#!/bin/bash

INPUT=$1

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# discard outliers
TOTAL=`grep -e '^2,0,' ${INPUT}.dat.matched | wc -l`

#HEAD=$TOTAL
HEAD=$(( $TOTAL - 50 ))
TAIL=$(( $TOTAL - 100 ))

if [[ $HEAD -le 0 ]]; then
   echo "ERROR: TOTAL is $TOTAL; HEAD is $HEAD"
   exit
fi

LINE=`grep -e '^2,0,' ${INPUT}.dat.matched |\
grep -v \- |\
sort -n -t, -k3 |\
head -$HEAD |\
tail -$TAIL |\
awk -F, 'BEGIN {SUM0=0;SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM0+=$3; SUM1+=$4; SUM2+=$5; SUM3+=$6; SUM4+=$7; SUM5+=$8; SUM6+=$9} END {printf"avg val0: %f avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM0/NR,SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

echo "average basic start/stop costs: $LINE"

BASIC_V0=`echo $LINE | awk '{printf"%f", $3}'`
BASIC_V1=`echo $LINE | awk '{printf"%f", $6}'`
BASIC_V2=`echo $LINE | awk '{printf"%f", $9}'`
BASIC_V3=`echo $LINE | awk '{printf"%f", $12}'`
BASIC_V4=`echo $LINE | awk '{printf"%f", $15}'`
BASIC_V5=`echo $LINE | awk '{printf"%f", $18}'`
BASIC_V6=`echo $LINE | awk '{printf"%f", $21}'`

# get nested stamp overhead
LINE=`grep -e '^2,2,' ${INPUT}.dat.matched | \
grep -v \- |\
sort -n -t, -k3 |\
head -${HEAD} |\
tail -$TAIL |\
awk -F, 'BEGIN {SUM0=0;SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM0+=$3; SUM1+=$4; SUM2+=$5; SUM3+=$6; SUM4+=$7; SUM5+=$8; SUM6+=$9} END {printf"avg val0: %f avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM0/NR,SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

echo "average raw stamp costs: $LINE"
echo""

TMP_V0=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V1=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$12}'`
TMP_V4=`echo $LINE | awk '{printf"%f",$15}'`
TMP_V5=`echo $LINE | awk '{printf"%f",$18}'`
TMP_V6=`echo $LINE | awk '{printf"%f",$21}'`

STAMP_V0=`echo "$TMP_V0 - $BASIC_V0" | bc -l`
STAMP_V1=`echo "$TMP_V1 - $BASIC_V1" | bc -l`
STAMP_V2=`echo "$TMP_V2 - $BASIC_V2" | bc -l`
STAMP_V3=`echo "$TMP_V3 - $BASIC_V3" | bc -l`
STAMP_V4=`echo "$TMP_V4 - $BASIC_V4" | bc -l`
STAMP_V5=`echo "$TMP_V5 - $BASIC_V5" | bc -l`
STAMP_V6=`echo "$TMP_V6 - $BASIC_V6" | bc -l`

# get nested start/stop overhead
LINE=`grep '^2,3' ${INPUT}.dat.matched | \
grep -v \- |\
sort -n -t, -k3 |\
head -${HEAD} |\
tail -$TAIL |\
awk -F, 'BEGIN {SUM0=0;SUM1=0;SUM2=0;SUM3=0;SUM4=0;SUM5=0;SUM6=0}; {SUM0+=$3; SUM1+=$4; SUM2+=$5; SUM3+=$6; SUM4+=$7; SUM5+=$8; SUM6+=$9} END {printf"avg val0: %f avg val1: %f avg val2: %f avg val3: %f avg val4: %f avg val5: %f avg val6: %f",SUM0/NR,SUM1/NR,SUM2/NR,SUM3/NR,SUM4/NR,SUM5/NR,SUM6/NR}'`

echo "average raw start-stop costs: $LINE"
echo""

TMP_V0=`echo $LINE | awk '{printf"%f",$3}'`
TMP_V1=`echo $LINE | awk '{printf"%f",$6}'`
TMP_V2=`echo $LINE | awk '{printf"%f",$9}'`
TMP_V3=`echo $LINE | awk '{printf"%f",$12}'`
TMP_V4=`echo $LINE | awk '{printf"%f",$15}'`
TMP_V5=`echo $LINE | awk '{printf"%f",$18}'`
TMP_V6=`echo $LINE | awk '{printf"%f",$21}'`

STARTSTOP_V0=`echo "scale=4; $TMP_V0 - $BASIC_V0" | bc`
STARTSTOP_V1=`echo "scale=4; $TMP_V1 - $BASIC_V1" | bc`
STARTSTOP_V2=`echo "scale=4; $TMP_V2 - $BASIC_V2" | bc`
STARTSTOP_V3=`echo "scale=4; $TMP_V3 - $BASIC_V3" | bc`
STARTSTOP_V4=`echo "scale=4; $TMP_V4 - $BASIC_V4" | bc`
STARTSTOP_V5=`echo "scale=4; $TMP_V5 - $BASIC_V5" | bc`
STARTSTOP_V6=`echo "scale=4; $TMP_V6 - $BASIC_V6" | bc`

for (( i=0;i<=6;i++ ))
do
    for j in BASIC STAMP STARTSTOP
    do
        vname="${j}_V${i}"
        echo "${vname}: ${!vname}"
    done
    echo ""
done
