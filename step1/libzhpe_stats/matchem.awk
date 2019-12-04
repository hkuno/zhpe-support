{
   if ($1 == 1)
   {
       data1[$2]=$3;
       data2[$2]=$4;
       data3[$2]=$5;
   }
   else
   {if ($1 == 2)
   {
       if ((data1[$2] != 0) && ($2 != 0))
       {
           printf("subid: %s; typeid: %s; val1: %d\n",$2, $1, $3 - data1[$2]);
       }
       else
       {
           printf("subid: %s; typeid: %s; rawval1: %d\n", $2, $1, $3);
       }

       if ((data2[$2] != 0) && ($2 != 0))
       {
           printf("subid: %s; val2: %d\n",$2, $1, $4 - data2[$2]);
       }
       else
       {
           printf("subid: %s; typeid: %s; rawval2: %d\n", $2, $1, $4);
       }


       if ((data3[$2] != 0) && ($2 != 0))
       {
           printf("subid: %s; val3: %d\n",$2, $1, $5 - data3[$2]);
       }
       else
       {
           printf("subid: %s; typeid: %s; rawval3: %d\n", $2, $1, $5);
       }
    }
   }
}
