#!/bin/bash

INPUT=$1

fname=$( basename $INPUT )

OUTPUTDIR=${2}/distributions

mkdir -p ${OUTPUTDIR}
ret=$?

(( $? == 0 )) || (echo "Unable to create ${OUTPUTDIR}" && exit -1)

# Given a data file with "matched" data, plot per-counter per subid distribution


for s in $( grep -e "^2," ${INPUT} | awk -F, '{print $2}' | sort -u )
do
    echo "processing subid $s"
    for (( i=0;i<=6;i++ ))
    do
        grep -e "2,${s}," ${INPUT} | awk -F, '{printf"%d\n",$'$(( i + 3 ))'}' |\
            sort -n | uniq -c > ${OUTPUTDIR}/$fname.subid${s}.val${i}.dist.dat
    done
done

echo "Output in: ${OUTPUTDIR}"
