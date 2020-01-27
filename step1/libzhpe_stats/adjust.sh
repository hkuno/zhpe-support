#!/bin/bash

APPNAME=$( basename $0 )

function usage()
{
  echo "usage: $APPNAME <inputfile> <overheadfile>"
  exit -1
}


[[ $#  -ge 2 ]] || usage

INPUT=$1

OVERHEADFILE=$2


( [[ -f $OVERHEADFILE ]] &&  [[ -f $INPUT ]] ) || ( usage )

declare -A overheads

# read overhead file
for (( i=0;i<=6;i++ ))
do
    for j in BASIC STAMP STARTSTOP
    do
        vname="${j}_V${i}"
        overheads[${vname}]=$( grep -e "^${vname}:" $OVERHEADFILE | grep -v \# | awk '{print $2}' )
        #echo "${vname}: ${overheads[${vname}]}"
    done
    #echo ""
done

# produce .dat file
python3 unpackdata.py $INPUT > ${INPUT}.dat

# produce .dat.matched file
awk -F, -f matchem.awk ${INPUT}.dat > ${INPUT}.dat.matched

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
egrep -v '^#' ${INPUT}.dat.matched |\
awk -F, '
    BEGIN {
      nested_stamp_count=0;
      nested_ss_count=0;
      prev_nest=0;
      prev_stamp=0;
      ZHPE_START=1
      ZHPE_STOP=2
      ZHPE_STOP_ALL=3
      ZHPE_STAMP=8
    }
    {
      if (($1 < 0 ) || ($1 > 99))
      {
          printf("#%s\n",$0);
      }
      else
      {
        this_opt=$1
        this_nest=$10
        if (this_opt == ZHPE_STAMP)
        {
             if ( this_nest>=prev_nest )
             {
                # printf("in stamp this_nest was %d,prev_nest was %d so reset\n", this_nest, prev_nest);
                nested_ss_count=0;
                nested_stamp_count=0;
             }
             print $0;
             nested_stamp_count++;
             prev_stamp=1;
             # printf("stamp: this_nest is %d\n",this_nest);
        }
        else
        {
            if ( this_nest<prev_nest )
            {
                if ( prev_stamp == 1 )
                {
                    prev_stamp=0;
                }
                else
                {
                    nested_ss_count++;
                }
                # printf("this_nest was %d,prev_nest was %d so potential nested_ss_count++\n", this_nest, prev_nest);
            }
            else
            {
                # printf("this_nest was %d,prev_nest was %d so reset\n", this_nest, prev_nest);
                nested_ss_count=0;
                nested_stamp_count=0;
            }

            if (this_opt == ZHPE_STOP)
            {
                # printf("v0 is %.3f,", $3);
                # printf("basic v0 is %d,", '${overheads[BASIC_V0]}');
                # printf("nested_stamp_count is %d,", nested_stamp_count );
                # printf("nested_ss_count is %d,", nested_ss_count );
                printf("%d,%d,", $1, $2);
                printf("%.3f,", (($3 - '${overheads[BASIC_V0]}') - (nested_stamp_count * '${overheads[STAMP_V0]}')  - (nested_ss_count * '${overheads[STARTSTOP_V0]}')));
                printf("%.3f,", (($4 - '${overheads[BASIC_V1]}') - (nested_stamp_count * '${overheads[STAMP_V1]}')  - (nested_ss_count * '${overheads[STARTSTOP_V1]}')));
                printf("%.3f,", (($5 - '${overheads[BASIC_V2]}') - (nested_stamp_count * '${overheads[STAMP_V2]}')  - (nested_ss_count * '${overheads[STARTSTOP_V2]}')));
                printf("%.3f,", (($6 - '${overheads[BASIC_V3]}') - (nested_stamp_count * '${overheads[STAMP_V3]}')  - (nested_ss_count * '${overheads[STARTSTOP_V3]}')));
                printf("%.3f,", (($7 - '${overheads[BASIC_V4]}') - (nested_stamp_count * '${overheads[STAMP_V4]}')  - (nested_ss_count * '${overheads[STARTSTOP_V4]}')));
                printf("%.3f,", (($8 - '${overheads[BASIC_V5]}') - (nested_stamp_count * '${overheads[STAMP_V5]}')  - (nested_ss_count * '${overheads[STARTSTOP_V5]}')));
                printf("%.3f,", (($9 - '${overheads[BASIC_V6]}') - (nested_stamp_count * '${overheads[STAMP_V6]}')  - (nested_ss_count * '${overheads[STARTSTOP_V6]}')));
                printf("%d", this_nest);
                printf("\n");
            }
        }
        prev_nest=this_nest;
        #printf("set prev_nest to %d\n",prev_nest);
   }
   }
done' > ${INPUT}.dat.matched.adjusted

echo "Output in: ${INPUT}.dat.matched.adjusted"
