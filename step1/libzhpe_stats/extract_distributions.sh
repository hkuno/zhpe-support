#!/bin/bash

APPNAME=$( basename $0 )
RELATIVE_APPDIR=$( dirname $0 )
STATS_INCLUDE_DIR=$( cd ${RELATIVE_APPDIR}/../include; pwd -P )
ZHPE_STATS_TYPES_H=${STATS_INCLUDE_DIR}/zhpe_stats_types.h

function config2human()
{
    local confcode=$1
    local hstring;

    if [[ "$confcode" != "" ]]; then
        hstring=$( grep -i "${confcode}" $ZHPE_STATS_TYPES_H | awk '{print $1}' )
        if [[ -z "${hstring}" ]];then
            hstring=$confcode
        fi
        echo "${hstring}"
     fi
}


function profile2human()
{
    local profcode=$1
    local hstring;

    hstring=$( grep "${profcode}" $ZHPE_STATS_TYPES_H | \
               grep ZHPE_STATS_PROFILE |awk '{print $1}' )
    if [[ -z "${hstring}" ]];then
        hstring=$profcode
    fi
    echo "${hstring}"
}

function usage()
{
  echo "${APPNAME} <input.matched>  <outputdirname>"
  exit
}

if [[ $# -ne 2 ]]; then
  usage
fi


INPUT=$1

fname=$( basename $INPUT )

OUTPUTDIR=${2}/distributions

mkdir -p ${OUTPUTDIR}
ret=$?

(( $? == 0 )) || (echo "Unable to create ${OUTPUTDIR}" && exit -1)

if [[ ! -f $INPUT ]]; then
    echo "ERROR: $INPUT does not exist"
    exit -1
fi

# Given a data file with at least "matched" data, plot per-counter per subid distribution

PROFILEID=$( grep -w '^\#profileid:' ${INPUT} | awk '{print $2}' )
NUMCOUNTERS=1
LINE=$( grep -w '^\#profileid:' ${INPUT} | head -1 )
AWKOUT=`echo $LINE | awk '{ for (i=0;i<NF;i++) {printf"%d: %s\n",i, $i}}'`
VNAMES=("rdtscp")
for (( i=1; i<7; i++ ))
do
    vname="VAL${i}NAME"
    tmp1=$( echo $LINE | grep "val${i}_config" )
    if [[ "${tmp1}" != "" ]]; then
        j=$(( 8 + 2 * i ))
        tmp2=`echo $LINE |\
                  awk '{ for (i=0;i<NF;i++) {printf"%d: %s\n",i, $i}}' |\
                  grep "${j}:" | awk '{print $2}'`
        NUMCOUNTERS=$(( NUMCOUNTERS + 1 ))
        tmp3=$(config2human $tmp2)
        VNAMES+=($tmp3)
    fi
done

if [[ -f ${ZHPE_STATS_TYPES_H} ]]; then
    PROFILE_NAME=$(profile2human $PROFILEID)
else
    PROFILE_NAME=$PROFILEID
fi

for s in $( grep -e "^2," ${INPUT} | awk -F, '{print $2}' | grep -v 0 | sort -u )
do
    echo "processing subid $s"
    for (( i=0;i<$NUMCOUNTERS;i++ ))
    do
        tmpname=${VNAMES[$i]}
        ofile="${OUTPUTDIR}/$fname.subid${s}.${tmpname}.dist.dat"
        grep -e "2,${s}," ${INPUT} | awk -F, '{printf"%d\n",$'$(( i + 3 ))'}' |\
            sort -n | uniq -c > $ofile
    done
done

echo "Output in: ${OUTPUTDIR}"
