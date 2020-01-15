{
 if (($1 > 0) && ($1 < 9))
 {
    if ($1 == 1)
    {
        if  ($2 in ndata)
        {
           cur = ndata[$2] + 1;
        }
        else
        {
           cur = 0;
        }
        ndata[$2] = cur;
        data0[$2][cur]=$3;
        data1[$2][cur]=$4;
        data2[$2][cur]=$5;
        data3[$2][cur]=$6;
        data4[$2][cur]=$7;
        data5[$2][cur]=$8;
        data6[$2][cur]=$9;
    }
    else
    {
        if ($1 == 2)
        {
            cur = ndata[$2];
            ndata[$2] = cur - 1;
            printf("subid: %s; op: %s; val0: %d ; val1: %d ; val2: %d ; val3: %d ; val4: %d ; val5: %d ; val6: %d\n", $2, $1, $3 - data0[$2][cur], $4 - data1[$2][cur], $5 - data2[$2][cur], $6 - data3[$2][cur], $7 - data4[$2][cur], $8 - data5[$2][cur], $9 - data6[$2][cur]);
        }
        else
        {
                printf("subid: %s; op: %s; rawval0: %d ; rawval1: %d ; rawval2: %d ; rawval3: %d ; rawval4: %d ; rawval5: %d ; rawval6: %d\n", $2, $1, $3, $4,$5,$6,$7,$8,$9);
        }
     }
  }
}
