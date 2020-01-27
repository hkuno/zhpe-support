    BEGIN {
      stacklen=0;
      nestlvl=0;
      ZHPE_START=1
      ZHPE_STOP=2
      ZHPE_STOP_ALL=3
      ZHPE_STAMP=8
    }
    {
        if (($1 < 0 ) || ($1 > 99))
        {
          printf("#%s\n",$0);
        }
        else
        {
        if ($1 == ZHPE_START)
        {
            nestlvl++;
            stack[stacklen++] = $2;
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
            if ($1 == ZHPE_STAMP)
            {
                nestlvl++;
                printf("%d,%d,", $1, $2);
                printf("%d,", $3);
                printf("%d,", $4);
                printf("%d,", $5);
                printf("%d,", $6);
                printf("%d,", $7);
                printf("%d,", $8);
                printf("%d,", $9);
                printf("%d", nestlvl);
                printf("\n");
                nestlvl--;
            }
        else
        {
            if ($1 == ZHPE_STOP)
            {
                stacklen--;
                cursubid = stack[stacklen];
                if ( cursubid != $2 )
                    printf("Warning: cursubid %d != %d\n",cursubid, $2);
                cur = ndata[$2];
                ndata[$2] = cur - 1;
                printf("%d,%d,", $1, $2);
                printf("%d,", $3 - data0[$2][cur]);
                printf("%d,", $4 - data1[$2][cur]);
                printf("%d,", $5 - data2[$2][cur]);
                printf("%d,", $6 - data3[$2][cur]);
                printf("%d,", $7 - data4[$2][cur]);
                printf("%d,", $8 - data5[$2][cur]);
                printf("%d,", $9 - data6[$2][cur]);
                printf("%d", nestlvl);
                printf("\n");
                nestlvl--;
            }
            else
            {
                if ($1 == ZHPE_STOP_ALL)
                {
                    while ( stacklen > 0 )
                    {
                        stacklen--;
                        cursubid = stack[stacklen];
                        cur = ndata[cursubid];
                        ndata[cursubid] = cur - 1;
                        printf("%s,%s,", ZHPE_STOP, $2);
                        printf("%d,", $3 - data0[cursubid][cur]);
                        printf("%d,", $4 - data1[cursubid][cur]);
                        printf("%d,", $5 - data2[cursubid][cur]);
                        printf("%d,", $6 - data3[cursubid][cur]);
                        printf("%d,", $7 - data4[cursubid][cur]);
                        printf("%d,", $8 - data5[cursubid][cur]);
                        printf("%d,", $9 - data6[cursubid][cur]);
                        printf("%d", nestlvl);
                        printf("\n");
                        nestlvl--;
                    }
                }
                else
                {
                    printf("%d,%d,", $1, $2);
                    printf("%d,", $3);
                    printf("%d,", $4);
                    printf("%d,", $5);
                    printf("%d,", $6);
                    printf("%d,", $7);
                    printf("%d,", $8);
                    printf("%d,", $9);
                    printf("%d", nestlvl);
                    printf("\n");
                }
             }
         }
      }
    }
  }
  END {
        while ( stacklen > 0 )
        {
            stacklen--;
            cursubid = stack[stacklen];
            cur = ndata[cursubid];
            ndata[cursubid] = cur - 1;
            printf("%s,%s,", $1, $2);
            printf("%d,", $3 - data0[cursubid][cur]);
            printf("%d,", $4 - data1[cursubid][cur]);
            printf("%d,", $5 - data2[cursubid][cur]);
            printf("%d,", $6 - data3[cursubid][cur]);
            printf("%d,", $7 - data4[cursubid][cur]);
            printf("%d,", $8 - data5[cursubid][cur]);
            printf("%d,", $9 - data6[cursubid][cur]);
            printf("%d", nestlvl);
            printf("\n");
            nestlvl--;
        }
   }
