#!/usr/bin/python3

import os
import struct
import sys
from ctypes import *
import statistics
import enum
import pprint
import logging
from statsstructures import *
from bcolors import *
import copy
from calibration import *
import glob

def pretty_print_profile(profile):
    print('{:<7},{:<6},{},{},{},{},{},{}'.format(OperationFlags.get_str(profile.opFlag), 
                                                                   profile.subId,
                                                                   profile.val1,
                                                                   profile.val2,
                                                                   profile.val3,
                                                                   profile.val4,
                                                                   profile.val5,
                                                                   profile.val6))


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""                                                                                        
def pretty_print_all_events_list(all_events):
    for index, event in enumerate(all_events):
        if event.end == None:
            logger.critical(bcolors.FAIL+"Missing STOP event for subId " + str(event.begin.subId) + bcolors.ENDC)
        else:
            print('SubId: {:<5} Seq:{:<4} Nesting: {} \n'.format(event.key.subId, event.key.sequence, event.key.nesting), end='')
            pretty_print_delta_raw(event.end, event.begin)
            sum_nest_raw = ProfileDataMath()
            
            print('\t\tOverheads:')
            if len(event.overhead):

                '''Sum of nested raw deltas'''
                for index, overhead in enumerate(event.overhead):
                    if overhead.stop.opFlag != 0:
                        sum_nest_raw += overhead.stop - overhead.start
                        
                print(bcolors.OKBLUE+ "\t\tSUM OF NESTED RAW DELTA" + 6*" " + "({}, {}, {}, {}, {}, {})".format( 
                                                                                                sum_nest_raw.val1,
                                                                                                sum_nest_raw.val2,
                                                                                                sum_nest_raw.val3,
                                                                                                sum_nest_raw.val4,
                                                                                                sum_nest_raw.val5,
                                                                                                sum_nest_raw.val6) + bcolors.ENDC)
                '''printing all raw deltas'''
                for index, overhead in enumerate(event.overhead):
                    if overhead.stop.opFlag == 0:
                        logger.warning(bcolors.WARNING + "Missing stop Overhead for " + OperationFlags.get_str(overhead.start.opFlag) + bcolors.ENDC)
                    else:
                        print('\t', end='')
                        pretty_print_delta_raw(overhead.stop, overhead.start)
                
            else:
                print(bcolors.OKBLUE+'\t\t\tNONE'+bcolors.ENDC)

            print('')


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""                                                                                        
def pretty_print_metadata(aMetadata):
    print('ProfileId: {}\n'
          'Perf Typeid: {}\n'
          'Config Count: {}\n'
          'Config List: {}, {}, {}, {}, {}, {}\n\n'.format(aMetadata.profileId,
                                                           aMetadata.perf_typeid,
                                                           aMetadata.config_count,
                                                           aMetadata.config_list[0],
                                                           aMetadata.config_list[1],
                                                           aMetadata.config_list[2],
                                                           aMetadata.config_list[3],
                                                           aMetadata.config_list[4],
                                                           aMetadata.config_list[5]))

"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
def pretty_print_event(aAliveEvent, aProfile, aListAliveEvents):
    print('{:<7},{:<6},{},{},{},{},{},{}   Nesting: {}'.format( OperationFlags.get_str(aProfile.opFlag), 
                                                                aProfile.subId,
                                                                aProfile.val1,
                                                                aProfile.val2,
                                                                aProfile.val3,
                                                                aProfile.val4,
                                                                aProfile.val5,
                                                                aProfile.val6,
                                                                aAliveEvent.nesting ), end= ' ')
    print( '[', end='')
    event = None
    for index, event in enumerate(aListAliveEvents):
        print('{}/{}:{}'.format(event.key.subId, 
                                event.key.sequence, 
                                OperationFlags.get_str(event.opFlag)[:1] if event.opFlag == OperationFlags.enPAUSE else '\b'), 
                                end=", ")
    if event is None:
        print('  ',end='')
    print('\b\b]')



"""
-----------------------------------------------------------------
-----------------------------------------------------------------
Prints the STOP-START value 
"""
def pretty_print_delta_raw(eventstop, eventstart):
    print(bcolors.OKBLUE+"\tDELTA RAW[{:<6}] [{:7} > {:6}] ({}, {}, {}, {}, {}, {})".format(eventstop.subId,
                                                                            OperationFlags.get_str(eventstart.opFlag),
                                                                            OperationFlags.get_str(eventstop.opFlag),
                                                                            eventstop.val1 - eventstart.val1,
                                                                            eventstop.val2 - eventstart.val2,
                                                                            eventstop.val3 - eventstart.val3,
                                                                            eventstop.val4 - eventstart.val4,
                                                                            eventstop.val5 - eventstart.val5,
                                                                            eventstop.val6 - eventstart.val6,) + bcolors.ENDC)


"""
-----------------------------------------------------------------
Working in the overhead list  -----------------------------------
"""

listKey_test = []
nesting = 0

def increase_sequence(subid):
    global listKey_test

    sequence = 0
    if listKey_test is None:
        listKey_test = []

    for index, key in enumerate(listKey_test):
        if key.subId == subid:
            sequence = key.sequence + 1
            key.sequence = sequence
            listKey_test[index] = key
            break
    else:
        localKey = EventKey()
        localKey.subId = subid
        localKey.sequence = sequence
        listKey_test.append(copy.deepcopy(localKey))

    return sequence

def get_current_sequence(subid):
    global listKey_test
    sequence = 0
    for index, key in enumerate(listKey_test):
        if key.subId == subid:
            sequence = key.sequence
            break
    else:
        logger.warning(bcolors.WARNING +"Sequence not found for subId: "+ str(subid) + bcolors.ENDC)

    return sequence



"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
# alive_event_list has all the events that started and didn't stoped yet
alive_event_list = []

def add_overhead_event_in_all_alive_list(profile_event, all_events_list):
    no_stop_overhead = False
    for index_alive_events, alive_event in reversed(list(enumerate(alive_event_list))):
        for index_all_events, event in enumerate(all_events_list):
            if event.key.subId == alive_event.key.subId and \
               event.key.sequence == alive_event.key.sequence:
                #if a begining event
                if OperationFlags.is_beginning_event(profile_event.opFlag) or \
                   OperationFlags.is_single_event(profile_event.opFlag):
                    stop_event = ProfileData()
                    if OperationFlags.is_single_event(profile_event.opFlag):
                        #add in the overhead list as a start/stop event
                        stop_event = profile_event
                    #add in the overhead list
                    overhead = OverheadInterval(profile_event, stop_event)
                    all_events_list[index_all_events].overhead.append(copy.deepcopy(overhead))
                
                #Do not save the STOP event that is related to the start event of this specific 
                #SubID present in the top of the stack
                elif profile_event.opFlag == OperationFlags.enSTOP and \
                     profile_event.subId  == event.key.subId  and \
                     no_stop_overhead == False:
                    no_stop_overhead = True
                else:
                    #find the begining event and add 
                    #as a pair in the overhead list
                    for index_overhead, overhead in reversed(list(enumerate(event.overhead))):
                        if overhead.start.subId == profile_event.subId and \
                           OperationFlags.does_end_event_match(overhead.start.opFlag, profile_event.opFlag):
                            if overhead.stop.opFlag != 0:
                                logger.error(bcolors.FAIL + "Stop overwritten for overhead on subId " + str(alive_event.key.subId) + bcolors.ENDC)
#                                pretty_print_profile(profile_event)
                            else:
                                overhead.stop = profile_event
                                all_events_list[index_all_events].overhead[index_overhead] = copy.deepcopy(overhead)
                            break
                    else:
                        logger.error(bcolors.FAIL + OperationFlags.get_str(profile_event.opFlag) + "({})".format(profile_event.subId) + " does not have start overhead list of subId " + str(alive_event.key.subId) + bcolors.ENDC)
#                        pretty_print_profile(profile_event)

"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
def process_profile_event(profile_event, all_events_list):
    global nesting
    #Add the event in all events in the alive_event_list
    
    add_overhead_event_in_all_alive_list(profile_event, all_events_list)

    #All starts are added in the alive_event_list
    if profile_event.opFlag == OperationFlags.enSTART:
        #event_record = IntervalEvent(EventKey(), ProfileData(), ProfileData(), OverheadInterval())
        event_record = IntervalEvent()
        event_record.key.subId = profile_event.subId
        event_record.key.nesting = nesting
        event_record.key.sequence = increase_sequence(profile_event.subId)
        event_record.begin = profile_event
        event_record.end = None
        copied_event = copy.deepcopy(event_record)
        all_events_list.append(copied_event)
        nesting += 1
        alive_event_list.append(copy.deepcopy(event_record))

    elif profile_event.opFlag == OperationFlags.enSTOP:
        sequence = get_current_sequence(profile_event.subId)
        for index, event in enumerate(all_events_list):
            if event.key.subId    == profile_event.subId and \
                event.key.sequence == sequence:
                event.end = profile_event
                all_events_list[index] = copy.deepcopy(event)
                #remove from alive list
                for index, alive_event in reversed(list(enumerate(alive_event_list))):
                    if event.key.subId == alive_event.key.subId:
                        alive_event_list.remove(alive_event)
                        nesting -= 1
                        break
                break
        else:
            logger.error(bcolors.FAIL + "Start not found for subId " + str(profile_event.subId) + bcolors.ENDC)


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
def test_summary(filename, metadata, calibration, events_list, mode, factor = 1):

    if mode == "max":
        calib_start_stop         = calibration.start_stop.max
        calib_stamp              = calibration.stamp.max
        calib_nesting_start_stop = calibration.nesting_start_stop.max
    elif mode == "min":
        calib_start_stop         = calibration.start_stop.min
        calib_stamp              = calibration.stamp.min
        calib_nesting_start_stop = calibration.nesting_start_stop.min
    elif mode == "avg":
        calib_start_stop         = calibration.start_stop.avg
        calib_stamp              = calibration.stamp.avg
        calib_nesting_start_stop = calibration.nesting_start_stop.avg

    calib_start_stop *= factor
    calib_stamp      *= factor
    calib_nesting_start_stop *= factor

    print(bcolors.HEADER + "CALIBRATION VALUES" + bcolors.ENDC)
    Calibration().pretty_print_calibration_values(calib_start_stop, 1)
    Calibration().pretty_print_calibration_values(calib_stamp, 2)
    Calibration().pretty_print_calibration_values(calib_nesting_start_stop, 3)



    print(bcolors.HEADER + "\nTEST {} SUMMARY OF DELTAS\n".format(os.path.basename(filename)) + bcolors.ENDC)
    pretty_print_metadata(metadata)
    for index, event in enumerate(events_list):
        if event.end == None:
            logger.critical(bcolors.FAIL+"Missing STOP event for subId " + str(event.begin.subId) + bcolors.ENDC)
        else:
            print('SubId: {:<5} Seq: {:<4} Nest: {}'.format(event.key.subId, event.key.sequence, event.key.nesting), end='')
            clear_profile_value = event.end - event.begin

            #Remove START/STOP
            clear_profile_value -= calib_start_stop

            if len(event.overhead):
                for index, overhead in enumerate(event.overhead):
                    if overhead.stop.opFlag == 0:
                        logger.warning(bcolors.WARNING + "Missing stop Overhead for " + OperationFlags.get_str(overhead.start.opFlag) + bcolors.ENDC)
                    else:
                        if overhead.start.opFlag == OperationFlags.enSTART and \
                           overhead.stop.opFlag  == OperationFlags.enSTOP:
                            clear_profile_value -= calib_nesting_start_stop
                        elif overhead.start.opFlag == OperationFlags.enDISABLE and \
                             overhead.stop.opFlag  == OperationFlags.enENABLE:
                             clear_profile_value -= calib_nesting_start_stop + (overhead.stop - overhead.start)
                        elif overhead.start.opFlag == OperationFlags.enSTAMP and \
                             overhead.stop.opFlag  == OperationFlags.enSTAMP:
                             clear_profile_value -= calib_stamp
                        else:
                            logger.critical(bcolors.FAIL+"Calibration NOT found for the pair " + str(overhead.start.opFlag) + "/" + str(overhead.stop.opFlag) + bcolors.ENDC)
            
            print(bcolors.OKBLUE + '\tADJUSTED VALUES: {}, {}, {}, {}, {}, {}'.format( 
                                                                clear_profile_value.val1,
                                                                clear_profile_value.val2,
                                                                clear_profile_value.val3,
                                                                clear_profile_value.val4,
                                                                clear_profile_value.val5,
                                                                clear_profile_value.val6) + bcolors.ENDC)

"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
def unpack_file(afilename):
    global nesting
    global alive_event_list
    global listKey_test
    files_list = []
    calibration_overheads = CalibrationOverheads()

    files_list=[ os.path.realpath(i) for i in glob.glob(afilename+"*")]

    #the calibration file must be the first in alphabetical order
    for afile in sorted(files_list):
        all_events_list = []
        alive_event_list = []
        listKey_test = []
        nesting = 0

        with open(afile,'rb') as file:
            #save output file
            if outputFile != None:
                orig_stdout = sys.stdout
                outFile = open(outputFile, 'w+')
                sys.stdout = outFile
        
            print ('Unpacking %s' %afilename)

            metadata = Metadata()
            file.readinto(metadata)
            pretty_print_metadata(metadata)
            
            profileData = ProfileData()
            while file.readinto(profileData):
                list_of_data = struct.unpack("iiLLLLLLL", profileData)
                list_of_data = list_of_data[:8]
                logger.debug(list_of_data)
                process_profile_event(profileData, all_events_list)
                pretty_print_profile(profileData)

            if outputFile != None:
                sys.stdout = orig_stdout
                outFile.close()

            pretty_print_all_events_list(all_events_list)

            #Is it a calibration file?
            calibration = Calibration()
            if calibration.is_calibration_file(all_events_list) == True:
                #parse values for calibration struct
                logger.debug("Calibration file name {}".format(os.path.basename(afile)))
                calibration_overheads = calibration.calculate_overheads(calibration.list_calibration_subid_values(all_events_list))
                calibration.pretty_print_calibration_summary(calibration_overheads)
            else:
                #Test file
                test_summary(afile, metadata, calibration_overheads, all_events_list, "avg", 1)
"""
-----------------------------------------------------------------
Logger and output file configuration 
-----------------------------------------------------------------
"""
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
filename = sys.argv[1]
outputFile = None if len(sys.argv) < 3 else sys.argv[2]

"""
-----------------------------------------------------------------
main: delta.py [inputfile] [outputfile]
Do not insert an output file in order to see the output in the 
terminal
-----------------------------------------------------------------
"""
    #Directory is not supported since the calibration become activated
unpack_file(filename)
   

