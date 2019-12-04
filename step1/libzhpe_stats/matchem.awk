{
   if ($1 == 1)
   {
       data[$2]=$3;
   }
   else
   {
       if (data[$2] != 0)
       {
           printf("subid: %s; typeid: %s; val1: %d\n",$2, $1, $3 - data[$2]);
       }
       else
       {
           printf("subid: %s; typeid: %s; rawval1: %d\n", $2, $1, $3);
       }
    }
}
