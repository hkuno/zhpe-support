#!/bin/bash

T=/shared/hkuno/zhpe-stats

export LD_LIBRARY_PATH=$T/bld/lib

export PATH=$T/bld/bin:/usr/lib64/qt-3.3/bin:/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin

/bin/ls /tmp/*statoverhead*.1 /tmp/*sanitycheck*.8 > /tmp/$(logname).before >& /dev/null
for i in hw just1cpu just1hw cpu
do
    export ZHPE_STATS_PROFILE="$i"
    $T/bld/libexec/stat_overheads  /tmp/ ${i}_statoverhead
    $T/bld/libexec/sanitycheck_stats  /tmp/ ${i}_sanitycheck
done
/bin/ls /tmp/*statoverhead*.1 /tmp/*sanitycheck*.8 > /tmp/$(logname).after

NEW=`cat /tmp/$(logname).before /tmp/$(logname).before /tmp/$(logname).after | sort | uniq -u`

for j in $NEW
do
    python3 unpackdata.py ${j} > ${j}.dat
    awk -F, -f matchem.awk ${j}.dat > ${j}.dat.matched
done
