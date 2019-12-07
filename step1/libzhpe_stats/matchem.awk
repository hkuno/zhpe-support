{
   if ($1 != 86) 
   {
   if ( (($1 + 0 >= 87) && ($1 + 0 <= 89)) || (($1 + 0 >= 97) && ($1 + 0 <= 99)))
   {
       printf("calibrate %d rdtscp: %d ; cpucyc: %d ; hwinst: %d ; val: %d ; val: %d\n",$1, $3,$4,$5,$6,$7,$8);
   } else
   {
    if ($1 == 1)
    {
       data1[$2]=$3;
       data2[$2]=$4;
       data3[$2]=$5;
       data4[$2]=$6;
       data5[$2]=$7;
    }
    else
    {
        if ($1 == 2)
        {
           printf("subid: %s; op: %s; val1: %d ; val2: %d ; val3: %d ; val4: %d ; val5: %d\n", $2, $1, $3 - data1[$2], $4 - data2[$2], $5 - data3[$2], $6 - data4[$2], $7 - data5[$2]);
        }
        else
        {
            printf("subid: %s; op: %s; rawval1: %d ; rawval2: %d ; rawval3: %d ; rawval4: %d ; rawval5: %d\n", $2, $1, $3, $4,$5,$6,$7);
        }
    }
 }
 }
}
