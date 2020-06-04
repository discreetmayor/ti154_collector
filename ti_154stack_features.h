/******************************************************************************

 @file  ti_154stack_features.h

 @brief TI-15.4 Stack feature definitions for Collector applications

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
#ifndef TI_154STACK_FEATURES_H
#define TI_154STACK_FEATURES_H

/******************************************************************************
 Includes
 *****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/******************************************************************************
 Definitions
 *****************************************************************************/
/* Mode of operation defines */

/* The TI-15.4 stack supports 3 modes of operation:
   Frequency Hopping mode: Devices in the network hop on the different
   frequency channels on the selected physical medium.
   Beacon mode: The coordinator device sends periodic beacon and the rest of the
   devices synchronize with the coordinator beacon for communication in the
   network
   Non Beacon mode: The coordinator does not send periodic beacons.
   The devices in the network does not have to synchronize with
   the coordinator. The device will send a beacon request and make a decision
   to join a coordinator based on the beacon responses from the coordinators.
   */

/*! If defined, builds the image with all the modes of operation
    (frequency hopping, beacon mode and non beacon mode) */
#define FEATURE_ALL_MODES

/*! If defined, builds the image with the frequency mode of operation */
#undef FEATURE_FREQ_HOP_MODE

/*! If defined, builds the image with beacon mode of operation */
#undef FEATURE_BEACON_MODE

/*! If defined, builds the image with non beacon mode of operation */
#undef FEATURE_NON_BEACON_MODE

/*! Builds the image with the full function device */
#define FEATURE_FULL_FUNCTION_DEVICE

/*! If defined, builds the image with the mac layer security turned on */
#define FEATURE_MAC_SECURITY

/*! For feature all modes, every mode is turned on */
#ifdef FEATURE_ALL_MODES
  #define FEATURE_FREQ_HOP_MODE
  #define FEATURE_BEACON_MODE
  #define FEATURE_NON_BEACON_MODE
  #define FEATURE_MAC_SECURITY
#endif /* FEATURE_ALL_MODES */

#ifndef FEATURE_ALL_MODES
#if defined(FEATURE_FREQ_HOP_MODE) && \
   (defined(FEATURE_BEACON_MODE) || defined(FEATURE_NON_BEACON_MODE))
#error "Do you want to build image with all modes or just frequency\
        hopping mode?. Un-define other modes or define FEATURE_ALL_MODES."
#endif

#if !defined(FEATURE_FREQ_HOP_MODE) && !defined(FEATURE_BEACON_MODE) && \
    !defined(FEATURE_NON_BEACON_MODE)
#error "Need to define at-least one mode of operation."
#endif
#endif /* !FEATURE_ALL_MODES*/

#if !defined(FEATURE_FULL_FUNCTION_DEVICE)
#error "Collector build needs FEATURE_FULL_FUNCTION_DEVICE."
#endif

#ifdef __cplusplus
}
#endif

#endif /* TI_154STACK_FEATURES_H */

