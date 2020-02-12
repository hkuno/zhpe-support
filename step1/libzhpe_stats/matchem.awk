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
      for ( i=0; i <20; i++ )
      {
          nested_stamp_count[i]=0;
          nested_measurement_count[i]=0;
      }
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
            if (nestlvl > 0)
            {
                for ( i=0; i< nestlvl; i++)
                    nested_measurement_count[i]++;
            }

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
                if (nestlvl > 0)
                {
                    for ( i=0; i< nestlvl; i++)
                        nested_stamp_count[i]++;
                }

                printf("%d,%d,", $1, $2);
                printf("%d,", $3);
                printf("%d,", $4);
                printf("%d,", $5);
                printf("%d,", $6);
                printf("%d,", $7);
                printf("%d,", $8);
                printf("%d,", $9);
                printf("%d,", nested_measurement_count[nestlvl]);
                printf("%d,", nested_stamp_count[nestlvl]);
                printf("%d,", nestlvl);
                printf("\n");
            }
        else
        {
            if ($1 == ZHPE_STOP)
            {
                stacklen--;
                nestlvl--;

                if (nestlvl > 0)
                {
                    for ( i=0; i< nestlvl; i++)
                        nested_measurement_count[i]++;
                }

                cursubid = stack[stacklen];
                if ( cursubid != $2 )
                {
                    printf("# unmatched stop %d != %d\n",cursubid2, $2);
                    stacklen++;
                    nestlvl++;
                } else {
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
                    printf("%d,", nested_measurement_count[nestlvl]);
                    printf("%d,", nested_stamp_count[nestlvl]);
                    printf("%d,", nestlvl);
                    printf("\n");

                    nested_measurement_count[nestlvl] = 0;
                    nested_stamp_count[nestlvl] = 0;
                }
            }
            else
            {
                if ($1 == ZHPE_STOP_ALL)
                {
                    while ( stacklen > 0 )
                    {
                        stacklen--;
                        nestlvl--;
                        cursubid = stack[stacklen];
                        cur = ndata[cursubid];
                        ndata[cursubid] = cur - 1;
                        printf("%s,%s,", ZHPE_STOP_ALL, cursubid);
                        printf("%d,", $3 - data0[cursubid][cur]);
                        printf("%d,", $4 - data1[cursubid][cur]);
                        printf("%d,", $5 - data2[cursubid][cur]);
                        printf("%d,", $6 - data3[cursubid][cur]);
                        printf("%d,", $7 - data4[cursubid][cur]);
                        printf("%d,", $8 - data5[cursubid][cur]);
                        printf("%d,", $9 - data6[cursubid][cur]);
                        printf("%d,", nested_measurement_count[nestlvl]);
                        printf("%d,", nested_stamp_count[nestlvl]);
                        printf("%d,", nestlvl);
                        printf("\n");
                        nested_measurement_count[nestlvl] = 0;
                        nested_stamp_count[nestlvl] = 0;
                    }
                }
                else
                {
                if ($1 == ZHPE_PAUSE_ALL)
                {
                    printf("# PAUSING %d\n",stacklen);
                    if (( pausedlen > 0 ) && (stacklen > 0))
                    {
                      printf("ERROR: Cannot nest stats_pause_all\n");
                    } else {
                        if (stacklen > 0)
                            pausedlen=stacklen;

                        foocnt=0;
                        while ( stacklen > 0 )
                        {
                            stacklen--;
                            nestlvl--;
                            cursubid = stack[stacklen];
                            cur = ndata[cursubid];
                            ndata[cursubid] = cur - 1;
                            printf("%s,%s,", ZHPE_PAUSE_ALL, cursubid);
                            printf("%d,", $3 - data0[cursubid][cur]);
                            printf("%d,", $4 - data1[cursubid][cur]);
                            printf("%d,", $5 - data2[cursubid][cur]);
                            printf("%d,", $6 - data3[cursubid][cur]);
                            printf("%d,", $7 - data4[cursubid][cur]);
                            printf("%d,", $8 - data5[cursubid][cur]);
                            printf("%d,", $9 - data6[cursubid][cur]);
                            printf("%d,", nested_measurement_count[nestlvl]);
                            printf("%d,", nested_stamp_count[nestlvl]);
                            printf("%d,", nestlvl);
                            printf("\n");
                            nested_measurement_count[nestlvl] = 0;
                            nested_stamp_count[nestlvl] = 0;
                            paused[foocnt] = cursubid;
                            foocnt++;
                        }
                    }
                } else {
                    if ($1 == ZHPE_RESTART_ALL)
                    {
                        printf("# Restarting %d\n",pausedlen);
                        for ( i=pausedlen-1; i >= 0; i-- )
                        {
                            if (nestlvl > 0)
                            {
                                for ( j=0; j< nestlvl; j++)
                                    nested_measurement_count[j]++;
                            }

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
                            nestlvl++;
                        }
                        pausedlen=0;
                    } else {
                                printf("##%d,%d,", $1, $2);
                                printf("%d,", $3);
                                printf("%d,", $4);
                                printf("%d,", $5);
                                printf("%d,", $6);
                                printf("%d,", $7);
                                printf("%d,", $8);
                                printf("%d,", $9);
                                printf("%d,", nested_measurement_count[nestlvl]);
                                printf("%d,", nested_stamp_count[nestlvl]);
                                printf("%d,", nestlvl);
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
            printf("%d,", nested_measurement_count[nestlvl]);
            printf("%d,", nested_stamp_count[nestlvl]);
            printf("%d,", nestlvl);
            printf("\n");
            nestlvl--;
        }
   }
