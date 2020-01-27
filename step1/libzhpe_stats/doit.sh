#!/bin/bash

if [[ $# -lt 2 ]]; then
   echo "usage: $0 <testdir> <outdir>"
   exit
fi

T=$1
OUTDIR=$2

/bin/rm -rf $OUTDIR; mkdir -p $OUTDIR
export ZHPE_STATS_PROFILE=cpu
$T/bld/libexec/stat_overheads $OUTDIR cpu_stat_overheads
export ZHPE_STATS_PROFILE=cache
$T/bld/libexec/stat_overheads $OUTDIR cache_stat_overheads
export ZHPE_STATS_PROFILE=cache2
$T/bld/libexec/stat_overheads $OUTDIR cache2_stat_overheads

./extract_overheads.sh ${OUTDIR}/cpu_stat_overheads*.0 > cpu.overheads
./extract_overheads.sh ${OUTDIR}/cache_stat_overheads*.0 > cache.overheads
./extract_overheads.sh ${OUTDIR}/cache2_stat_overheads*.0 > cache2.overheads

./extract_distributions.sh ${OUTDIR}/cpu_stat_overheads*.0.dat.matched ${OUTDIR}
./extract_distributions.sh ${OUTDIR}/cache_stat_overheads*.0.dat.matched ${OUTDIR}
./extract_distributions.sh ${OUTDIR}/cache2_stat_overheads*.0.dat.matched ${OUTDIR}

export ZHPE_STATS_PROFILE=cpu
$T/bld/libexec/sanitycheck_stats $OUTDIR cpu_sanitycheck
export ZHPE_STATS_PROFILE=cache
$T/bld/libexec/sanitycheck_stats $OUTDIR cache_sanitycheck
export ZHPE_STATS_PROFILE=cache2
$T/bld/libexec/sanitycheck_stats $OUTDIR cache2_sanitycheck

for i in $( /bin/ls ${OUTDIR}/*sanitycheck*.8 )
do
    python3 unpackdata.py $i > ${i}.dat

    awk -F, -f matchem.awk ${i}.dat > ${i}.dat.matched
done
