{
   if ($3 == 1)
   {
       data[$1]=$4;
   }
   else
   {
       if (data[$1] != 0)
       {
           printf("subid: %s; typeid: %s; val1: %d\n",$2, $1, $4 - data[$1]);
       }
       else
       {
           printf("subid: %s; typeid: %s; rawval1: %d\n", $2, $1, $4);
       }
    }
}
