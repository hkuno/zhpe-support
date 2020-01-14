
import os
import struct
import sys
from ctypes import *
from statsstructures import *
from bcolors import *
import copy

class Calibration(Structure):

    calibration_subid_meanings = {1: "START>STOP",
                                  2: "STAMP",
                                  3: "NESTING START>STOP"}

    def pretty_print_profile(self, profile):
        print('{:<4},{:<4},{:<4},{:<4},{:<4},{:<4}'.format( profile.val1,
                                                            profile.val2,
                                                            profile.val3,
                                                            profile.val4,
                                                            profile.val5,
                                                            profile.val6))

    def pretty_print_calibration_values(self,profile, calibrationindex):
        print('{}'
              '\n\tval1:{}'
              '\n\tval2:{}'
              '\n\tval3:{}'
              '\n\tval4:{}'
              '\n\tval5:{}'
              '\n\tval6:{}\n'.format( Calibration.calibration_subid_meanings[calibrationindex],
                                      profile.val1,
                                      profile.val2,
                                      profile.val3,
                                      profile.val4,
                                      profile.val5,
                                      profile.val6))

    def pretty_print_calibration_overhead(self, calibration_result):
        print(bcolors.OKBLUE+
              'val1\t\t{}\t{}\t{}'
              '\nval2\t\t{}\t{}\t{}'
              '\nval3\t\t{}\t{}\t{}'
              '\nval4\t\t{}\t{}\t{}'
              '\nval5\t\t{}\t{}\t{}'
              '\nval6\t\t{}\t{}\t{}\n'.format(
                                      calibration_result.max.val1, calibration_result.min.val1, calibration_result.avg.val1,
                                      calibration_result.max.val2, calibration_result.min.val2, calibration_result.avg.val2,
                                      calibration_result.max.val3, calibration_result.min.val3, calibration_result.avg.val3,
                                      calibration_result.max.val4, calibration_result.min.val4, calibration_result.avg.val4,
                                      calibration_result.max.val5, calibration_result.min.val5, calibration_result.avg.val5,
                                      calibration_result.max.val6, calibration_result.min.val6, calibration_result.avg.val6)+bcolors.ENDC)


    def pretty_print_calibration_summary(self, calibration_overheads):
        print(bcolors.HEADER+"CALIBRATION SUMMARY\n"+bcolors.ENDC)

        print("START / STOP\tMAX\tMIN\tAVG")
        self.pretty_print_calibration_overhead(calibration_overheads.start_stop)

        print("STAMP\t\tMAX\tMIN\tAVG")
        self.pretty_print_calibration_overhead(calibration_overheads.stamp)

        print("NEST START/STOP\tMAX\tMIN\tAVG")
        self.pretty_print_calibration_overhead(calibration_overheads.nesting_start_stop)


    """
    -----------------------------------------------------------------
    -----------------------------------------------------------------
    """
    MAX_RESERVED_SUBIDS_VALUE = 3       #TODO: update to 6 after implementation

    def is_calibration_file(self, profile_event_list):

        #In the zhpe_stats_test output file, subids 1 - 6 have special meanings:
        #subid 1 measures basic overhead of a non-nested pair of measurements: start(1); stop(1)
        #Subid 2 measures cost a nested STAMP: start(2); stamp; stop(2)
        #Subid 3 measures basic overhead of a nested start/stop: start(3); start(0); stop(0); stop(3)
        calibration = [False] * self.MAX_RESERVED_SUBIDS_VALUE 
        for idx, event in enumerate(profile_event_list):
            if event.key.subId != 0         and \
                event.key.subId <= self.MAX_RESERVED_SUBIDS_VALUE:
                calibration[event.key.subId - 1] = True

        for idx, subIdEvent in enumerate(calibration):
            if subIdEvent == False:
                return False

        return True

    """
    -----------------------------------------------------------------
    -----------------------------------------------------------------
    """
    def list_calibration_subid_values(self, profile_event_list):
        calibration_list = []
        total_of_measures = [0] * self.MAX_RESERVED_SUBIDS_VALUE
        for x in range (self.MAX_RESERVED_SUBIDS_VALUE):
            calibration_list.append(CalibrationResults())

        for idx, event in enumerate(profile_event_list):
            if event.key.subId != 0         and \
            event.key.subId <= self.MAX_RESERVED_SUBIDS_VALUE:
                event_delta = ProfileDataMath()
                event_delta = event.end - event.begin
                if total_of_measures[event.key.subId - 1] == 0:
                    calibration_list[event.key.subId - 1].max = copy.deepcopy(event_delta)
                    calibration_list[event.key.subId - 1].min = copy.deepcopy(event_delta)
                    calibration_list[event.key.subId - 1].avg = copy.deepcopy(event_delta)
                else:
                    calibration_list[event.key.subId - 1].max.get_higher(event_delta)
                    calibration_list[event.key.subId - 1].min.get_lower(event_delta)
                    calibration_list[event.key.subId - 1].avg += event_delta

                total_of_measures[event.key.subId - 1] += 1 

        for idx, calibration in enumerate(calibration_list):
            calibration.avg /= total_of_measures[idx]

        return calibration_list


    """
    -----------------------------------------------------------------
    -----------------------------------------------------------------
    """
    def calculate_overheads(self, calibration_list):
        calibration_overheads = CalibrationOverheads()
        
        #values collected by subId 1
        calibration_overheads.start_stop.max = calibration_list[0].max
        calibration_overheads.start_stop.min = calibration_list[0].min
        calibration_overheads.start_stop.avg = calibration_list[0].avg

        #values collected by subId 2 less start_stop calibration values
        calibration_overheads.stamp.max = calibration_list[1].max - calibration_list[0].min
        calibration_overheads.stamp.min = calibration_list[1].min - calibration_list[0].max
        calibration_overheads.stamp.avg = calibration_list[1].avg - calibration_list[0].avg

        #values collected by subId 3 less start_stop calibration values
        calibration_overheads.nesting_start_stop.max = calibration_list[2].max - calibration_list[0].min
        calibration_overheads.nesting_start_stop.min = calibration_list[2].min - calibration_list[0].max
        calibration_overheads.nesting_start_stop.avg = calibration_list[2].avg - calibration_list[0].avg

        return calibration_overheads
