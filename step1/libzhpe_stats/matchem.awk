{
   if ($1 == 1)
    {
       data1[$2]=$3;
       data2[$2]=$4;
       data3[$2]=$5;
#printf("Saving: subid: %s; op: %s; rawval1: %d; rawval2: %d; rawval3: %d\n", $2, $1, $3, $4,$5);
    }
    else
    {
        if ($1 == 2)
        {
#           printf("Calculating: subid: %s; op: %s; rawval1: %d; rawval2: %d; rawval3: %d\n", $2, $1, $3, $4,$5);
#           printf("from saved: rawval1: %d; rawval2: %d; rawval3: %d\n", data1[$2], data2[$2], data3[$2]);
#           printf("%d - %d is %d\n", $5,data3[$2],$5 - data3[$2]);
           printf("subid: %s; op: %s; val1: %d; val2: %d; val3: %d\n", $2, $1, $3 - data1[$2], $4 - data2[$2], $5 - data3[$2]);
        }
        else
        {
            printf("subid: %s; op: %s; rawval1: %d; rawval2: %d; rawval3: %d; rawval4: %d; rawval5: %d\n", $2, $1, $3, $4,$5,$6,$7);
        }
    }
}
