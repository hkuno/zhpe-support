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
def pretty_print_all_events_list(a_all_events):
    for index, event in enumerate(all_events_list):
        if event.end == None:
            logger.critical(bcolors.FAIL+"Missing STOP event for subId " + str(event.begin.subId) + bcolors.ENDC)
        else:
            print('Key: {:<5}.{:<4} Nesting: {} \n'.format(event.key.subId, event.key.sequence, event.key.nesting), end='')
            pretty_print_delta_raw(event.end, event.begin)
            print('\t\tOverheads:')
            if len(event.overhead):
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
    print('ProfileId: {} \n\n'.format(aMetadata.profileId))

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
        listKey_test.append(localKey)

    return sequence

def get_current_sequence(subid):
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
all_events_list  = []

def add_overhead_event_in_all_alive_list(profile_event):
    no_stop_overhead = False
    for index_alive_events, alive_event in reversed(list(enumerate(alive_event_list))):
        for index_all_events, event in enumerate(all_events_list):
            if event.key.subId == alive_event.key.subId and \
               event.key.sequence == alive_event.key.sequence:
                #if a begining event
                if OperationFlags.is_beginning_event(profile_event.opFlag):
                    #add in the overhead list
                    overhead = OverheadInterval(profile_event, ProfileData())
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
                        logger.error(bcolors.FAIL + OperationFlags.get_str(profile_event.opFlag) + " does not have start overhead list of subId " + str(alive_event.key.subId) + bcolors.ENDC)
#                        pretty_print_profile(profile_event)


"""
-----------------------------------------------------------------
-----------------------------------------------------------------
"""
def process_profile_event(profile_event):
    global all_events_list
    global nesting
    #Add the event in all events in the alive_event_list
    
    add_overhead_event_in_all_alive_list(profile_event)

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
def unpack_file(afilename):
    global all_events_list
    with open(afilename,'rb') as file:
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
            process_profile_event(profileData)
            pretty_print_profile(profileData)

        if outputFile != None:
            sys.stdout = orig_stdout
            outFile.close()

        pretty_print_all_events_list(all_events_list)

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
if os.path.isdir(filename):
    flist=[ os.path.basename(i) for i in os.listdir(filename)]
    for afile in sorted(flist):
        unpack_file(filename+'/'+afile)
else:
    unpack_file(filename)
   

