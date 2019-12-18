#!/usr/bin/python3

import os
import struct
import sys
from ctypes import *
import numpy as np


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
                     enOPEN:        "OPEN",
                     enCLOSE:       "CLOSE",
                     enFLUSH_START: "FLUSH_START",
                     enFLUSH_STOP:  "FLUSH_STOP"}

    @staticmethod
    def get_str(opflag):
        return OperationFlags.op_flags_dict[opflag]

    @staticmethod
    def is_beginning_event(opflag):
        return  opflag == OperationFlags.enSTART   or \
                opflag == OperationFlags.enPAUSE   or \
                opflag == OperationFlags.enDISABLE or \
                opflag == OperationFlags.enOPEN    or \
                opflag == OperationFlags.enFLUSH_START

    @staticmethod
    def does_end_event_match(opflagbegin, opflagend):
        return ( (opflagbegin == OperationFlags.enPAUSE and \
                 (opflagend == OperationFlags.enRESTART or opflagend == OperationFlags.enRESUME) ) or \
               (opflagbegin == OperationFlags.enDISABLE and opflagend == OperationFlags.enENABLE)  or \
               (opflagbegin == OperationFlags.enOPEN and opflagend == OperationFlags.enCLOSES)     or \
               (opflagbegin == OperationFlags.enSTART and opflagend == OperationFlags.enSTOP)      or \
               (opflagbegin == OperationFlags.enFLUSH_START and opflagend == OperationFlags.enFLUSH_STOP))

"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
class ProfileData(Structure):
    _pack_   = 1
    _fields_ = [('opFlag' , c_uint32),
                ('subId'  , c_uint32),
                ('val1'   , c_uint64),
                ('val2'   , c_uint64),
                ('val3'   , c_uint64),
                ('val4'   , c_uint64),
                ('val5'   , c_uint64),
                ('val6'   , c_uint64),
                ('pad2'   , c_uint64)]


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
    _fields_ = [('profileId', c_uint32)]



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

