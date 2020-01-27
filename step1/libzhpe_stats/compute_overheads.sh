#!/bin/bash

INPUT=$1

# hardcode temporarily
OVERHEADFILE="cpu.overheads"

declare -A overheads


BASIC=

# read overhead file
for (( i=0;i<=6;i++ ))
do
    for j in BASIC STAMP STARTSTOP
    do
        vname="${j}_V${i}"
        overheads[${vname}]=$( grep ${vname}: $OVERHEADFILE | awk '{print $2}' )
        echo "${vname}: ${overheads[${vname}]}"
    done
    echo ""
done

# produce .dat file
#python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
#awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

# walk through input file and adjust overheads
# nested_stamp_count=0
# nested_ss_count=0
# cur_nest=0
# For each line, look at that line's op (this_op) and nesting level (this_nest).
#
# 1. If this_op is ZHPE_STAMP, then print out line and nested_stamp_count++
#
# 1. If this_op is ZHPE_STOP, then:
#     1. if this_nest < prev_nest;
#                then nested_ss_count++;
#        else
#                then nested_ss_count=0; nested_stamp_count=0;
#     1. adjust and print each counter like this:
#          val - overheads[$basic_vname]
#              - (nested_stamp_count * overheads[$stamp_vname])
#              - (nested_ss_count * overheads[$ss_vname])
#     1. prev_nest = this_nest
#
awk -F, '
    BEGIN {
      nested_stamp_count=0;
      nested_ss_count=0;
      prev_nest=0;
      ZHPE_START=1
      ZHPE_STOP=2
      ZHPE_STOP_ALL=3
      ZHPE_STAMP=8
    }
    {
        this_opt=$1
        this_nest=$10
        if (this_opt == ZHPE_STAMP)
        {
             print $0;
             nested_stamp_count++;
        }
        else
        {
            if ( this_nest < prev_nest)
            {
                nested_ss_count++;
            }
            else
            {
                nested_ss_count=0;
                nested_stamp_count=0;
            }
            if (this_opt == ZHPE_STOP)
            {
                printf("%d,%d,", $1, $2);
                printf("v0 is %.3f,", $3);
                printf("basic v0 is %d,", '${overheads[BASIC_V0]}');
                printf("nested_stamp_count is %d,", nested_stamp_count );
                printf("nested_ss_count is %d,", nested_ss_count );
                printf("adjusted v0 is %.3f,", (($3 - '${overheads[BASIC_V0]}') - (nested_stamp_count * '${overheads[STAMP_V0]}')  - (nested_ss_count * '${overheads[STARTSTOP_V0]}')));
                printf("%d", this_nest);
                printf("\n");
            }
            prev_nest = this_nest;
        }
   }
done' $1

