#!/usr/bin/python3

import struct
import math
from ctypes import *


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class OperationFlags(c_uint32):
    enSTART             = 1
    enSTOP              = 2
    enPAUSE             = 3
    enRESUME            = 4
    enENABLE            = 5
    enDISABLE           = 6
    enRESTART           = 7
    enSTAMP             = 8
    enOPEN              = 9
    enCLOSE             = 10
    enFLUSH_START       = 11
    enFLUSH_STOP        = 12

    op_flags_dict = {enSTART:       "START",
                     enSTOP:        "STOP",
                     enPAUSE:       "PAUSE",
                     enRESUME:      "RESUME",
                     enENABLE:      "ENABLE",
                     enDISABLE:     "DISABLE",
                     enRESTART:     "RESTART",
                     enSTAMP:       "STAMP",
                     enOPEN:        "OPEN",
                     enCLOSE:       "CLOSE",
                     enFLUSH_START: "FLUSH_START",
                     enFLUSH_STOP:  "FLUSH_STOP"}

    @staticmethod
    def get_str(opflag):
        if opflag <= OperationFlags.enFLUSH_STOP and opflag != 0:
            return OperationFlags.op_flags_dict[opflag]
        else:
            return "UNKNOWN"


    @staticmethod
    def is_beginning_event(opflag):
        return  opflag == OperationFlags.enSTART   or \
                opflag == OperationFlags.enPAUSE   or \
                opflag == OperationFlags.enDISABLE or \
                opflag == OperationFlags.enOPEN    or \
                opflag == OperationFlags.enFLUSH_START

    @staticmethod
    def is_single_event(opflag):
        return opflag == OperationFlags.enSTAMP

    @staticmethod
    def does_end_event_match(opflagbegin, opflagend):
        return ( (opflagbegin == OperationFlags.enPAUSE and \
                 (opflagend == OperationFlags.enRESTART or opflagend == OperationFlags.enRESUME) ) or \
               (opflagbegin == OperationFlags.enDISABLE and opflagend == OperationFlags.enENABLE)  or \
               (opflagbegin == OperationFlags.enOPEN and opflagend == OperationFlags.enCLOSES)     or \
               (opflagbegin == OperationFlags.enSTART and opflagend == OperationFlags.enSTOP)      or \
               (opflagbegin == OperationFlags.enFLUSH_START and opflagend == OperationFlags.enFLUSH_STOP))



class ProfileDataMath(Structure):

    def __init__(self, val0=0, val1=0, val2=0, val3=0, val4=0, val5=0, val6=0):
        self.val0 = val0
        self.val1 = val1
        self.val2 = val2
        self.val3 = val3
        self.val4 = val4
        self.val5 = val5
        self.val6 = val6

    def __add__(self, other):
        profile_data = ProfileDataMath()
        profile_data.val0 = self.val0 + other.val0
        profile_data.val1 = self.val1 + other.val1
        profile_data.val2 = self.val2 + other.val2
        profile_data.val3 = self.val3 + other.val3
        profile_data.val4 = self.val4 + other.val4
        profile_data.val5 = self.val5 + other.val5
        profile_data.val6 = self.val6 + other.val6
        return profile_data

    def __sub__(self, other):
        profile_data = ProfileDataMath()
        profile_data.val0 = self.val0 - other.val0
        profile_data.val1 = self.val1 - other.val1
        profile_data.val2 = self.val2 - other.val2
        profile_data.val3 = self.val3 - other.val3
        profile_data.val4 = self.val4 - other.val4
        profile_data.val5 = self.val5 - other.val5
        profile_data.val6 = self.val6 - other.val6
        return profile_data

    def __truediv__(self, value):
        profile_data = ProfileDataMath()
        profile_data.val0 = math.ceil(self.val0/value)
        profile_data.val1 = math.ceil(self.val1/value)
        profile_data.val2 = math.ceil(self.val2/value)
        profile_data.val3 = math.ceil(self.val3/value)
        profile_data.val4 = math.ceil(self.val4/value)
        profile_data.val5 = math.ceil(self.val5/value)
        profile_data.val6 = math.ceil(self.val6/value)
        return profile_data

    def __mul__(self, other):
        profile_data = ProfileDataMath()
        profile_data.val0 = math.ceil(self.val0*other)
        profile_data.val1 = math.ceil(self.val1*other)
        profile_data.val2 = math.ceil(self.val2*other)
        profile_data.val3 = math.ceil(self.val3*other)
        profile_data.val4 = math.ceil(self.val4*other)
        profile_data.val5 = math.ceil(self.val5*other)
        profile_data.val6 = math.ceil(self.val6*other)
        return profile_data

    def get_higher(self, other):
        self.val0 = self.val0 if self.val0 >= other.val0 else other.val0
        self.val1 = self.val1 if self.val1 >= other.val1 else other.val1
        self.val2 = self.val2 if self.val2 >= other.val2 else other.val2
        self.val3 = self.val3 if self.val3 >= other.val3 else other.val3
        self.val4 = self.val4 if self.val4 >= other.val4 else other.val4
        self.val5 = self.val5 if self.val5 >= other.val5 else other.val5
        self.val6 = self.val6 if self.val6 >= other.val6 else other.val6
        return self

    def get_lower(self, other):
        self.val0 = self.val0 if self.val0 <= other.val0 else other.val0
        self.val1 = self.val1 if self.val1 <= other.val1 else other.val1
        self.val2 = self.val2 if self.val2 <= other.val2 else other.val2
        self.val3 = self.val3 if self.val3 <= other.val3 else other.val3
        self.val4 = self.val4 if self.val4 <= other.val4 else other.val4
        self.val5 = self.val5 if self.val5 <= other.val5 else other.val5
        self.val6 = self.val6 if self.val6 <= other.val6 else other.val6
        return self
"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class ProfileData(Structure):
    _pack_   = 1
    _fields_ = [('opFlag' , c_uint32),
                ('subId'  , c_uint32),
                ('val0'   , c_uint64),
                ('val1'   , c_uint64),
                ('val2'   , c_uint64),
                ('val3'   , c_uint64),
                ('val4'   , c_uint64),
                ('val5'   , c_uint64),
                ('val6'   , c_uint64)]

    def __add__(self, other):
        profile_data_math = ProfileDataMath(self.val0, self.val1, self.val2, self.val3, self.val4, self.val5, self.val6)
        profile_data_math = profile_data_math + other
        return profile_data_math

    def __sub__(self, other):
        profile_data_math = ProfileDataMath(self.val0, self.val1, self.val2, self.val3, self.val4, self.val5, self.val6)
        profile_data_math = profile_data_math - other
        return profile_data_math

    def __truediv__(self, value):
        profile_data_math = ProfileDataMath(self.val0, self.val1, self.val2, self.val3, self.val4, self.val5, self.val6)
        profile_data_math = profile_data_math/value
        return profile_data_math

    def __mul__(self, other):
        profile_data_math = ProfileDataMath(self.val0, self.val1, self.val2, self.val3, self.val4, self.val5, self.val6)
        profile_data_math = profile_data_math*other
        return profile_data_math



"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class EventKey(Structure):
    _fields_ = [('subId'    , c_uint32),
                ('sequence' , c_uint32),
                ('nesting'  , c_uint32)]


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class AliveEvents(Structure):
    _fields_ = [('key'    , EventKey),
                ('opFlag' , c_uint32),
                ('nesting', c_uint32),
                ('profile', ProfileData)]


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class Metadata(Structure):
    _fields_ = [('profileId'   , c_uint32),
                ('perf_typeid' , c_uint32),
                ('config_count', c_int),
                ('config_list' , c_uint64 * 6),
               ]


"""
-----------------------------------------------------------------
Working in the overhead list  -----------------------------------
"""
class ProfileEvent(Structure):
    _fields_ = [('profile'  , ProfileData),
                ('sequence' , c_uint32)]    #Order which the same subId is called for the same begining event.
                                            #Each subId has it's own sequence value
                                            #i.e. START 100 //sequence = 0
                                            #     STOP  100 //sequance = 0
                                            #     START 200 //sequence = 0 
                                            #     START 100 //sequence = 1
                                            #     START 300 //sequence = 0
                                            #     START 300 //sequence = 1

"""
-----------------------------------------------------------------
Overhead events present between an STOP/START Interval ----------
"""
class OverheadInterval(Structure):
    _fields_ = [('start', ProfileData),
                ('stop', ProfileData)]


"""
-----------------------------------------------------------------
List of the all START/STOP events  ------------------------------
"""
class IntervalEvent(Structure):

    def __init__(self):
        self.key = EventKey()
        self.begin = ProfileData()
        self.end = ProfileData()
        self.overhead = []


"""
-----------------------------------------------------------------
List of the calibration Results  ------------------------------
"""
class CalibrationResults(Structure):
    def __init__(self):
        self.max = ProfileDataMath()
        self.min = ProfileDataMath()
        self.avg = ProfileDataMath()

"""
-----------------------------------------------------------------
List of the calibration Overheads  ------------------------------
"""
class CalibrationOverheads(Structure):
    def __init__(self):
        self.start_stop         = CalibrationResults()
        self.stamp              = CalibrationResults()
        self.nesting_start_stop = CalibrationResults()
        