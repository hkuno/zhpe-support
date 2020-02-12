#!/bin/bash

INPUT=$1

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# get overhead file from metadata

profileid=`grep profileid: ${INPUT}.dat | head -1 | awk '{print $2}'`

case $profileid in
    200) OVERHEADFILE="carbon.overheads"
         ;;
    100) OVERHEADFILE="cache.overheads"
         ;;
    101) OVERHEADFILE="cache_alt.overheads"
         ;;
    300) OVERHEADFILE="cpu.overheads"
         ;;
    default) echo "ERROR: No overhead for $profileid"
          exit;
         ;;
esac

declare -A overheads

if [[ ! -f $OVERHEADFILE ]]; then
    echo "ERROR: No overhead for $profileid"
    exit;
fi

# read overhead file
for (( i=0;i<=6;i++ ))
do
    for j in STAMP MEASUREMENT BASIC
    do
        vname="${j}_V${i}"
        overheads[${vname}]=$( grep ${vname}: $OVERHEADFILE | awk '{print $2}' )
        echo "${vname}: ${overheads[${vname}]}"
    done
    echo ""
done


# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# produce .dat.matched.adjusted file
awk -F, -v v0_measure_oh=${overheads[MEASUREMENT_V0]} \
        -v v0_basic_oh=${overheads[BASIC_V0]} \
        -v v0_stamp_oh=${overheads[STAMP_V0]} \
        -v v1_measure_oh=${overheads[MEASUREMENT_V1]} \
        -v v0_basic_oh=${overheads[BASIC_V1]} \
        -v v1_stamp_oh=${overheads[STAMP_V1]} \
        -v v2_measure_oh=${overheads[MEASUREMENT_V2]} \
        -v v0_basic_oh=${overheads[BASIC_V2]} \
        -v v2_stamp_oh=${overheads[STAMP_V2]} \
        -v v3_measure_oh=${overheads[MEASUREMENT_V3]} \
        -v v0_basic_oh=${overheads[BASIC_V3]} \
        -v v3_stamp_oh=${overheads[STAMP_V3]} \
        -v v4_measure_oh=${overheads[MEASUREMENT_V4]} \
        -v v0_basic_oh=${overheads[BASIC_V4]} \
        -v v4_stamp_oh=${overheads[STAMP_V4]} \
        -v v5_measure_oh=${overheads[MEASUREMENT_V5]} \
        -v v0_basic_oh=${overheads[BASIC_V5]} \
        -v v5_stamp_oh=${overheads[STAMP_V5]} \
        -v v6_measure_oh=${overheads[MEASUREMENT_V6]} \
        -v v0_basic_oh=${overheads[BASIC_V6]} \
        -v v6_stamp_oh=${overheads[STAMP_V6]} \
       -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched.adjusted
