#!/bin/bash

T=/shared/hkuno/zhpe-stats

export LD_LIBRARY_PATH=$T/bld/lib

export PATH=$T/bld/bin:/usr/lib64/qt-3.3/bin:/usr/local/bin:/usr/bin:/usr/local/sbin:/usr/sbin

/bin/ls /tmp/*statoverhead*.1 /tmp/*sanitycheck*.8 > /tmp/$(logname).before >& /dev/null

for i in hw just1cpu just1hw cpu cache
do
    /bin/rm -f /tmp/${i}_statoverhead.*
    /bin/rm -f /tmp/${i}_sanitycheck.*
    export ZHPE_STATS_PROFILE="$i"
    $T/bld/libexec/stat_overheads  /tmp/ ${i}_statoverhead
    ./extract_overheads.sh /tmp/${i}_statoverhead.*.1 > ${i}.overheads

    $T/bld/libexec/sanitycheck_stats  /tmp/ ${i}_sanitycheck
    ./adjust_overheads.sh /tmp/${i}_sanitycheck.[0-9]*.8
    ./adjust_overheads.sh /tmp/${i}_statoverhead.[0-9]*.1
done
