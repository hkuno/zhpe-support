#!/bin/bash

INPUT=$1

# hardcode temporarily
OVERHEADFILE="carbon.overheads"

declare -A overheads


# read overhead file
for (( i=0;i<=6;i++ ))
do
    for j in BASIC STAMP MEASUREMENT
    do
        vname="${j}_V${i}"
        overheads[${vname}]=$( grep ${vname}: $OVERHEADFILE | awk '{print $2}' )
        echo "${vname}: ${overheads[${vname}]}"
    done
    echo ""
done

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# .dat.matched file has format:
#opflag,subid,val0,val1,val2,val3,val4,val5,val6,nested_measure_cnt,nested_stamp_cnt,nest_lvl
# 1    , 2   ,  3 , 4  , 5  , 6  , 7  , 8  , 9  , 10               ,  11            , 12
# walk through input file and adjust overheads
# For each valn of each line, overhead is:
#        valn - overheads[BASIC_Vn] - (nested_stamp_cnt * overheads[STAMP_Vn]) - (nested_measure_cnt * overheads[MEASUREMENT_Vn])

awk -F, \
'{
    if ( ($1 == 8) || ($1 < 0 ) || ($1 > 99))
    {
        printf("%s\n",$0);
    }
    else
    {
              printf("%d,%d,",$1,$2);
              printf("%.3f,", $3 - '${overheads[BASIC_V0]}' - \
                                   ($11 * '${overheads[STAMP_V0]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V0]}'));

              printf("%.3f,", $4 - '${overheads[BASIC_V1]}' - \
                                   ($11 * '${overheads[STAMP_V1]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V1]}'));

              printf("%.3f,", $5 - '${overheads[BASIC_V2]}' - \
                                   ($11 * '${overheads[STAMP_V2]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V2]}'));

              printf("%.3f,", $6 - '${overheads[BASIC_V3]}' - \
                                   ($11 * '${overheads[STAMP_V3]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V3]}'));

              printf("%.3f,", $7 - '${overheads[BASIC_V4]}' - \
                                   ($11 * '${overheads[STAMP_V4]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V4]}'));

              printf("%.3f,", $8 - '${overheads[BASIC_V5]}' - \
                                   ($11 * '${overheads[STAMP_V5]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V5]}'));

              printf("%.3f,", $9 - '${overheads[BASIC_V6]}' - \
                                   ($11 * '${overheads[STAMP_V6]}') - \
                                   ($10 *  '${overheads[MEASUREMENT_V6]}'));

              printf("%d,%d,%d\n",$10,$11,$12);
    }
}'  ${INPUT}.dat.matched >  ${INPUT}.dat.matched.adjusted
