    BEGIN {
      pausedlen=0;
      stacklen=0;
      nestlvl=0;
      ZHPE_START=1
      ZHPE_STOP=2
      ZHPE_STOP_ALL=3
      ZHPE_PAUSE_ALL=4
      ZHPE_RESTART_ALL=5
      ZHPE_STAMP=8
    }
    {
        if (($1 < 0 ) || ($1 > 99))
        {
          printf("#%s\n",$0);
        }
        else
        {
        if (($1 == ZHPE_START) || ($1 == ZHPE_RESTART_ALL))
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
                {
                    printf("Out-of-order warning: stack %d != %d\n",cursubid, $2);
                    cursubid2 = stack[stacklen];
                    if ( cursubid2 == $2 )
                    {
                        stack[stacklen] = cursubid;
                        cursubid = cursubid2;
                    }
                    else
                    {
                        printf("Warning: Also next on stack %d != %d\n",cursubid2, $2);
                        stacklen++;
                    }
                }
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
                        printf("%s,%s,", ZHPE_STOP_ALL, $2);
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
                if ($1 == ZHPE_PAUSE_ALL)
                {
                    if (( pausedlen > 0 ) && (stacklen > 0))
                    {
                      printf("ERROR: Cannot nest stats_pause_all\n");
                    } else {
                        if (stacklen > 0)
                            pausedlen=stacklen;

                        while ( stacklen > 0 )
                        {
                            stacklen--;
                            cursubid = stack[stacklen];
                            cur = ndata[cursubid];
                            ndata[cursubid] = cur - 1;
                            printf("%s,%s,", ZHPE_PAUSE_ALL, $2);
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
                            paused[pausedlen - stacklen] = cursubid;
                        }
                    }
                } else {
                    if ($1 == ZHPE_RESTART_ALL)
                    {

                        printf("%d,%d,%d,0,0,0,0,0,0,0\n", ZHPE_STAMP, ZHPE_RESTART_ALL, pausedlen);
                        for ( i=0; i < pausedlen; i++ )
                        {
                            nestlvl++;
                            cursubid = paused[i];
                            stack[stacklen++] = cursubid;
                            if  (cursubid in ndata)
                            {
                               cur = ndata[cursubid] + 1;
                            }
                            else
                            {
                                cur = 0;
                            }
                            ndata[cursubid] = cur;
                            data0[cursubid][cur]=$3;
                            data1[cursubid][cur]=$4;
                            data2[cursubid][cur]=$5;
                            data3[cursubid][cur]=$6;
                            data4[cursubid][cur]=$7;
                            data5[cursubid][cur]=$8;
                            data6[cursubid][cur]=$9;
                        }
                        pausedlen=0;
                    } else {
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
             } } } } } }
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
