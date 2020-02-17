#!/bin/bash -e

T=/shared/hkuno/zhpe-stats

OUTDIR=/tmp/$(logname)/zhpe-stats

mkdir -p $OUTDIR

export LD_LIBRARY_PATH=$T/bld/lib

export PATH=$T/bld/bin:/usr/lib64/qt-3.3/bin:/usr/local/bin:/usr/bin
export PATH=${PATH}:/usr/local/sbin:/usr/sbin

for i in hw just1cpu just1hw cpu cache
do
    /bin/rm -f ${OUTDIR}/${i}_stat*
    /bin/rm -f ${OUTDIR}/${i}_sanity*
    export ZHPE_STATS_PROFILE="$i"
    $T/bld/libexec/stat_overheads  $OUTDIR ${i}_statoh
    ./extract_overheads.sh $OUTDIR/${i}_statoh*.1 > ${OUTDIR}/${i}.overheads

    $T/bld/libexec/sanitycheck_stats  ${OUTDIR}/ ${i}_sanitycheck

   $T/bin/mpitest.sh -h ~/hostfile.2.1 -p zhpe -x ZHPE_STATS_PROFILE \
         $T/bin/mpi_numactl.sh $T/bld/libexec/mpi_send 100 1 ${OUTDIR}/ ${i}_mpisend
         # $T/bin/debug_wrapper $T/bld/libexec/mpi_send 100 1 ${OUTDIR}/ ${i}_mpisend

    ./adjust_overheads.sh  ${OUTDIR}/${i}_sanitycheck.[0-9]*.8
    ./adjust_overheads.sh  ${OUTDIR}/${i}_statoh.[0-9]*.1
    for j in ${OUTDIR}/${i}_mpisend.[0-9]*.1 ${OUTDIR}/${i}_mpisend.[0-9]*.1000

    do
        ./adjust_overheads.sh ${j}
    done
done

for i in just1hw
do
    export ZHPE_STATS_PROFILE="$i"
done
