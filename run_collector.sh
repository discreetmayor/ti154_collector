#!/bin/bash
#############################################################
# @file run_collector.sh
#
# @brief TIMAC 2.0 run_collector.sh, used by run_demo.sh to launch collector
#
# Group: WCS LPC
# $Target Device: DEVICES $
#
#############################################################
# $License: BSD3 2016 $
#############################################################
# $Release Name: PACKAGE NAME $
# $Release Date: PACKAGE RELEASE DATE $
#############################################################

# Because this is a "quick demo" we hard code
# the device name in this check. For a production
# application, a better check is suggested.
# 
#if [ ! -c /dev/ttyACM0 ]
#then
#    echo ""
#    echo "The Launchpad (/dev/ttyACM0) does not seem to be present"
#    echo ""
#    exit 1
#fi

# This test is simple... 
arch=`uname -m`

#if [ "x${arch}x" == 'xx86_64x' ]
#then
    exe=host_collector
#fi


#if [ "x${arch}x" == 'xarmv7lx' ]
#then
    # ---------
    # This script has no way to determine how you built
    # the application.
    # --------
    # This script assumes that you have built the "collector"
    # application via the a cross compiler method and not
    # built natively on the BBB
    # ---------
    # If you build natively on the BBB, then the BBB
    # is actually the HOST... and thus the application is
    # "host_collector"
#    exe=bbb_collector
#fi

if [ "x${exe}x" == "xx" ]
then
    echo "Cannot find Collector App exe: $exe"
    exit 1
fi

if [ ! -x $exe ]
then
    echo "Cannot find EXE $exe"
    exit 1
fi

PID=`pidof $exe`

if [ "x${PID}x" != "xx" ]
then
    kill -9 ${PID}
fi

# by default, the application uses the name: "collector.cfg" as the configuration file
# or you can pass the name of the configuration file on the command line
./$exe collector.cfg &
PID=$!
# Wait 3 seconds for it to get started ...
sleep 3
if ps -p $PID > /dev/null
then
    echo "Collector Running as Process id: ${PID}"
    exit 0
else
    echo "Error starting collector application"
    exit 1
fi





