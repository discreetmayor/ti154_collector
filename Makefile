#############################################################
# @file Makefile
#
# @brief TIMAC 2.0 Collector Example Application Makefile
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

_default: _app

COMPONENTS_HOME=../../components
SDK_HOME=../../../

HERE=$(shell pwd)
CFLAGS += -include ${HERE}/ti_154stack_features.h
CFLAGS += -DAUTO_START
CFLAGS += -DNV_RESTORE
CFLAGS += -DPROCESS_JS
#CFLAGS += -DFCS_TYPE16
CFLAGS += -DIS_HEADLESS
#CFLAGS += -DTIRTOS_IN_ROM
CFLAGS += -DOAD_BLOCK_SIZE=128 # change this to 64 when building the colector for 2.4GHz Band
CFLAGS += -DNV_LINUX
CFLAGS += -DNVOCMP_NVPAGES=4
CFLAGS += -I.
CFLAGS += -Icommon/
CFLAGS += -I${COMPONENTS_HOME}/common/inc
CFLAGS += -I${COMPONENTS_HOME}/nv/inc
CFLAGS += -I${COMPONENTS_HOME}/api/inc
CFLAGS += -I${SDK_HOME}

include ../../scripts/front_matter.mak

APP_NAME = collector

C_SOURCES += linux_main.c
C_SOURCES += cllc.c
C_SOURCES += cllc_linux.c
C_SOURCES += collector.c
C_SOURCES += csf_linux.c
C_SOURCES += appsrv.c
C_SOURCES += mac_util.c
C_SOURCES += oad_protocol.c

APP_LIBS    += libnv.a
APP_LIBS    += libapimac.a
APP_LIBS    += libcommon.a

#APP_LIBDIRS += ${PROTOC_INSTALL_DIR}/lib
APP_LIBDIRS += ${COMPONENTS_HOME}/cllc/${OBJDIR}
APP_LIBDIRS += ${COMPONENTS_HOME}/nv/${OBJDIR}
APP_LIBDIRS += ${COMPONENTS_HOME}/api/${OBJDIR}
APP_LIBDIRS += ${COMPONENTS_HOME}/common/${OBJDIR}
APP_LIBDIRS += ${OBJDIR}

include ../../scripts/app.mak

#  ========================================
#  Texas Instruments Micro Controller Style
#  ========================================
#  Local Variables:
#  mode: makefile-gmake
#  End:
#  vim:set  filetype=make
