#!/usr/bin/python3

import os
import struct
import sys
from ctypes import *
import statistics

class Record(Structure):
    _fields_ = [('profileid', c_size_t),
                ('subid', c_uint32),
                ('opflag', c_uint32),
                ('val1', c_uint64),
                ('val2', c_uint64),
                ('val3', c_uint64),
                ('val4', c_uint64),
                ('val5', c_uint64)]

def printRecordHeader():
    print('profileid,subid,opflag,val1,val2,val3,val4,val5')

def prettyPrintRecord(aRecord):
    print('profileid:{}, subid:{}, opflag:{},  val1:{},  val2:{}, val3:{}, val4:{}, val5:{}'.format(aRecord.profileid,aRecord.subid,aRecord.opflag,aRecord.val1,aRecord.val2,aRecord.val3,aRecord.val4,aRecord.val5))

def printRecord(aRecord):
    print('{},{},{},{},{},{},{},{}'.format(aRecord.profileid,aRecord.subid,aRecord.opflag,aRecord.val1,aRecord.val2,aRecord.val3,aRecord.val4,aRecord.val5))

def printStatsForArray(aName, anArray):
    idx=0
    max=0
    max_idx=0
    for x in anArray:
        if (idx == 0):
            max=x
            max_idx=idx
        else:
            if (x > max):
                max=x
                max_idx=idx
        idx = idx + 1
    print('{}: Max is {}, index {}'.format(aName,max,max_idx)) 

    print(aName + ": average: ",end="")
    print(statistics.mean(anArray))

    print(aName + ": mode: ",end="")
    print(statistics.mode(anArray))

    print('/tmp/'+aName + ": Population standard deviation: ",end="")
    print(statistics.pstdev(anArray))

def unpackfile(afilename):
    print ('Unpacking %s' %afilename)
    with open(afilename,'rb') as file:
        my_idx=0
        total_array=[]
        cpl0_array=[]
        cpl3_array=[]
        printRecordHeader()
        x = Record()
        while file.readinto(x):
            val1_array.append(x.val1)
            val2_array.append(x.val2)
            val3_array.append(x.val3)
            val4_array.append(x.val4)
            val5_array.append(x.val5)
            printRecord(x)
            my_idx= my_idx + 1

#     printStatsForArray(afilename + " val1", val1_array)
#     printStatsForArray(afilename + " val2", val2_array)
#     printStatsForArray(afilename + " val3", val3_array)
#     printStatsForArray(afilename + " val4", val4_array)
#     printStatsForArray(afilename + " val5", val5_array)

# main
val1_array=[]
val2_array=[]
val3_array=[]
val4_array=[]
val5_array=[]

filename = sys.argv[-1]

if os.path.isdir(filename):
    flist=[ os.path.basename(i) for i in os.listdir(filename)]
    for afile in sorted(flist):
        unpackfile(filename+'/'+afile)
else:
    unpackfile(filename)

# printStatsForArray('execInstTotal', all_totals_array)
# printStatsForArray('cpl0', all_cpl0_array)
# printStatsForArray('cpl3', all_cpl0_array)
