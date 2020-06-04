/******************************************************************************

 @file ti_154stack_config.h

 @brief TI-15.4 Stack configuration parameters for Collector applications

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
#ifndef TI_154STACK_CONFIG_H
#define TI_154STACK_CONFIG_H

/******************************************************************************
 Includes
 *****************************************************************************/
#include "api_mac.h"

#ifdef __cplusplus
extern "C"
{
#endif

/******************************************************************************
 Constants and definitions
 *****************************************************************************/
/* config parameters */

/*
   NOTE ABOUT CONFIGURATION PARAMTERS
   ----------------------------------
   In the embedded device, these are hard coded configuration items.
   In the Linux impimentation they are configurable in 2 ways:
   Method #1 via hard coding with the _DEFAULT value.
   Method #2 via the "*.cfg" configuration file.
   This "extern bool" hidden via the macro exists to facilitate
   the linux configuration scheme.
 */

/*! Should the newtwork auto start or not? */
extern bool linux_CONFIG_AUTO_START;
#define CONFIG_AUTO_START   linux_CONFIG_AUTO_START
#define CONFIG_AUTO_START_DEFAULT true

/*! Security Enable - set to true to turn on security */
extern bool linux_CONFIG_SECURE;
#define CONFIG_SECURE                linux_CONFIG_SECURE
#define CONFIG_SECURE_DEFAULT        true
/*! PAN ID */
extern int linux_CONFIG_PAN_ID;
#define CONFIG_PAN_ID                ((uint16_t)(linux_CONFIG_PAN_ID))
#define CONFIG_PAN_ID_DEFAULT        0xffff

/*! Coordinator short address */
extern int linux_CONFIG_COORD_SHORT_ADDR;
#define CONFIG_COORD_SHORT_ADDR      ((uint16_t)(linux_CONFIG_COORD_SHORT_ADDR))
#define CONFIG_COORD_SHORT_ADDR_DEFAULT 0xAABB
/*! FH disabled as default */

extern bool linux_CONFIG_FH_ENABLE;
#define CONFIG_FH_ENABLE             linux_CONFIG_FH_ENABLE
#define CONFIG_FH_ENABLE_DEFAULT     false

/*! maximum beacons possibly received */
#define CONFIG_MAX_BEACONS_RECD 200

/*! maximum devices in association table */
#define CONFIG_MAX_DEVICES           50

/*!
 Setting beacon order to 15 will disable the beacon, 8 is a good value for
 beacon mode
 */
extern int linux_CONFIG_MAC_BEACON_ORDER;
#define CONFIG_MAC_BEACON_ORDER      linux_CONFIG_MAC_BEACON_ORDER
#define CONFIG_MAC_BEACON_ORDER_DEFAULT 15

/*!
 Setting superframe order to 15 will disable the superframe, 8 is a good value
 for beacon mode
 */
extern int linux_CONFIG_MAC_SUPERFRAME_ORDER;
#define CONFIG_MAC_SUPERFRAME_ORDER  linux_CONFIG_MAC_SUPERFRAME_ORDER
#define CONFIG_MAC_SUPERFRAME_ORDER_DEFAULT 15

/*! Setting for Phy ID */
extern int linux_CONFIG_PHY_ID;
#define CONFIG_PHY_ID (linux_CONFIG_PHY_ID)
#define CONFIG_PHY_ID_DEFAULT                (APIMAC_50KBPS_915MHZ_PHY_1)

/*! Setting for channel page */
extern int linux_CONFIG_CHANNEL_PAGE;
#define CONFIG_CHANNEL_PAGE                  linux_CONFIG_CHANNEL_PAGE

#if ((CONFIG_PHY_ID_DEFAULT >= APIMAC_MRFSK_STD_PHY_ID_BEGIN) && (CONFIG_PHY_ID_DEFAULT <= APIMAC_MRFSK_STD_PHY_ID_END))
#define CONFIG_CHANNEL_PAGE_DEFAULT          (APIMAC_CHANNEL_PAGE_9)
#elif ((CONFIG_PHY_ID_DEFAULT >= APIMAC_MRFSK_GENERIC_PHY_ID_BEGIN) && (CONFIG_PHY_ID_DEFAULT <= APIMAC_MRFSK_GENERIC_PHY_ID_END))
#define CONFIG_CHANNEL_PAGE_DEFAULT          (APIMAC_CHANNEL_PAGE_10)
#else
#error "PHY ID is wrong."
#endif

/*! MAC Parameter */
/*! Min BE - Minimum Backoff Exponent */
extern int linux_CONFIG_MIN_BE;
#define CONFIG_MIN_BE linux_CONFIG_MIN_BE
#define CONFIG_MIN_BE_DEFAULT 3

/*! Max BE - Maximum Backoff Exponent */
extern int linux_CONFIG_MAX_BE;
#define CONFIG_MAX_BE linux_CONFIG_MAX_BE
#define CONFIG_MAX_BE_DEFAULT 5

/*! MAC MAX CSMA Backoffs */
extern int linux_CONFIG_MAC_MAX_CSMA_BACKOFFS;
#define CONFIG_MAC_MAX_CSMA_BACKOFFS linux_CONFIG_MAC_MAX_CSMA_BACKOFFS
#define CONFIG_MAC_MAX_CSMA_BACKOFFS_DEFAULT 5

/*! macMaxFrameRetries - Maximum Frame Retries */
extern int linux_CONFIG_MAX_RETRIES;
#define CONFIG_MAX_RETRIES linux_CONFIG_MAX_RETRIES
#define CONFIG_MAX_RETRIES_DEFAULT 5

/* Linux variable names for Applciation traffic profile */
extern int linux_CONFIG_REPORTING_INTERVAL;
#define CONFIG_REPORTING_INTERVAL         linux_CONFIG_REPORTING_INTERVAL

extern int linux_CONFIG_POLLING_INTERVAL;
#define CONFIG_POLLING_INTERVAL         linux_CONFIG_POLLING_INTERVAL

extern int linux_TRACKING_DELAY_TIME;
#define TRACKING_DELAY_TIME         linux_TRACKING_DELAY_TIME

/*! Application traffic profile */
#if (((CONFIG_PHY_ID >= APIMAC_MRFSK_STD_PHY_ID_BEGIN) && (CONFIG_PHY_ID <= APIMAC_MRFSK_GENERIC_PHY_ID_BEGIN)) || \
    ((CONFIG_PHY_ID >= APIMAC_200KBPS_915MHZ_PHY_132) && (CONFIG_PHY_ID <= APIMAC_200KBPS_868MHZ_PHY_133)))
/*!
 Reporting Interval - in milliseconds to be set on connected devices using
 configuration request messages
 */
#define CONFIG_REPORTING_INTERVAL_DEFAULT 90000

/*!
 Polling interval in milliseconds to be set on connected devices using
 configuration request messages. Must be greater than or equal to default
 polling interval set on sensor devices
 */
#define CONFIG_POLLING_INTERVAL_DEFAULT 6000

/*!
 Time interval in ms between tracking message intervals
 */
#define TRACKING_DELAY_TIME_DEFAULT 60000
#else
/*!
 Reporting Interval - in milliseconds to be set on connected devices using
 configuration request messages
 */
#define CONFIG_REPORTING_INTERVAL_DEFAULT 300000
/*!
 Polling interval in milliseconds to be set on connected devices using
 configuration request messages. Must be greater than or equal to default
 polling interval set on sensor devices
 */
#define CONFIG_POLLING_INTERVAL_DEFAULT 60000
/*!
 Time interval in ms between tracking message intervals
 */
#define TRACKING_DELAY_TIME_DEFAULT 300000
#endif

extern uint8_t linux_CONFIG_SCAN_DURATION;
#define CONFIG_SCAN_DURATION         linux_CONFIG_SCAN_DURATION
/*! scan duration in seconds */
#define CONFIG_SCAN_DURATION_DEFAULT 5

extern int linux_CONFIG_RANGE_EXT_MODE;
#define CONFIG_RANGE_EXT_MODE linux_CONFIG_RANGE_EXT_MODE
/*!
 Range Extender Mode setting.
 The following modes are available.
 APIMAC_NO_EXTENDER - does not have PA/LNA
 APIMAC_HIGH_GAIN_MODE - high gain mode
 To enable CC1190, use
 #define CONFIG_RANGE_EXT_MODE_DEFAULT       APIMAC_HIGH_GAIN_MODE
*/
#define CONFIG_RANGE_EXT_MODE_DEFAULT       APIMAC_NO_EXTENDER

/*! Setting Default Key*/
#define KEY_TABLE_DEFAULT_KEY \
    {0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,                    \
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
/*!
 Channel mask used when CONFIG_FH_ENABLE is false.
 Each bit indicates if the corresponding channel is to be scanned
 First byte represents channels 0 to 7 and the last byte represents
 channels 128 to 135.
 For byte zero in the bit mask, LSB representing Ch0.
 For byte 1, LSB represents Ch8 and so on.
 e.g., 0x01 0x10 represents Ch0 and Ch12 are included.
 The default of 0x0F represents channels 0-3 are selected.
 APIMAC_50KBPS_915MHZ_PHY_1 (50kbps/2-FSK/915MHz band) has channels 0 - 128.
 APIMAC_50KBPS_868MHZ_PHY_3 (50kbps/2-FSK/863MHz band) has channels 0 - 33.
 APIMAC_50KBPS_433MHZ_PHY_128 (50kbps/2-FSK/433MHz band) has channels 0 - 6.

 NOTE:
    In the linux impliementation the INI file parser callback
    uses a a function to *clear/zero* the mask, and another
    function to set various bits within the channel mask.
 */
extern uint8_t linux_CONFIG_CHANNEL_MASK[APIMAC_154G_CHANNEL_BITMAP_SIZ];
#define CONFIG_CHANNEL_MASK linux_CONFIG_CHANNEL_MASK
#define CONFIG_CHANNEL_MASK_DEFAULT   { 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                                        0x00, 0x00, 0x00, 0x00, 0x00 }
/*!
 Channel mask used when CONFIG_FH_ENABLE is true.
 Represents the list of channels on which the device can hop.
 The actual sequence used shall be based on DH1CF function.
 It is represented as a bit string with LSB representing Ch0.
 e.g., 0x01 0x10 represents Ch0 and Ch12 are included.
 */
extern uint8_t linux_CONFIG_FH_CHANNEL_MASK[APIMAC_154G_CHANNEL_BITMAP_SIZ];
#define CONFIG_FH_CHANNEL_MASK linux_CONFIG_FH_CHANNEL_MASK
#define CONFIG_FH_CHANNEL_MASK_DEFAULT { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, \
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, \
                                         0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

/*!
 List of channels to target the Async frames
 It is represented as a bit string with LSB representing Ch0
 e.g., 0x01 0x10 represents Ch0 and Ch12 are included
 It should cover all channels that could be used by a target device in its
 hopping sequence. Channels marked beyond number of channels supported by
 PHY Config will be excluded by stack. To avoid interference on a channel,
 it should be removed from Async Mask and added to exclude channels
 (CONFIG_CHANNEL_MASK).
 */
extern uint8_t linux_FH_ASYNC_CHANNEL_MASK[APIMAC_154G_CHANNEL_BITMAP_SIZ];
#define FH_ASYNC_CHANNEL_MASK linux_FH_ASYNC_CHANNEL_MASK
#define FH_ASYNC_CHANNEL_MASK_DEFAULT                                   \
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                               \
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,                         \
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

/* FH related config variables */
/*!
 The number of non sleepy channel hopping end devices to be supported.
 It is to be noted that the total number of non sleepy devices supported
  must be less than 50. Stack will allocate memory proportional
 to the number of end devices requested.
 */
extern int linux_FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS;
#define FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS  linux_FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS
#define FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS_DEFAULT 5
/*!
 The number of non sleepy fixed channel end devices to be supported.
 It is to be noted that the total number of non sleepy devices supported
  must be less than 50. Stack will allocate memory proportional
 to the number of end devices requested.
 */
extern int linux_FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS;
#define FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS  linux_FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS
#define FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS_DEFAULT 5

/*!
 Dwell time: The duration for which the collector will
 stay on a specific channel before hopping to next channel.
 */
extern int linux_CONFIG_DWELL_TIME;
#define CONFIG_DWELL_TIME            linux_CONFIG_DWELL_TIME
#define CONFIG_DWELL_TIME_DEFAULT    250

/*!
 FH Application Broadcast Msg generation interval in ms.
 Value should be set at least greater than 200 ms,
 */
extern int linux_FH_BROADCAST_INTERVAL;
#define FH_BROADCAST_INTERVAL            linux_FH_BROADCAST_INTERVAL
#define FH_BROADCAST_INTERVAL_DEFAULT    10000

/*! FH Broadcast dwell time */
extern int linux_FH_BROADCAST_DWELL_TIME;
#define FH_BROADCAST_DWELL_TIME            linux_FH_BROADCAST_DWELL_TIME
#define FH_BROADCAST_DWELL_TIME_DEFAULT    100

/*!
 Maximum number of attempts for association in FH mode
 after reception of a PAN Config frame
 */
#define CONFIG_FH_MAX_ASSOCIATION_ATTEMPTS 3

/*! PAN Advertisement Solicit trickle timer duration in milliseconds */
#define CONFIG_PAN_ADVERT_SOLICIT_CLK_DURATION 6000
/*! PAN Config Solicit trickle timer duration in milliseconds */
#define CONFIG_PAN_CONFIG_SOLICIT_CLK_DURATION 6000
/*! FH Poll/Sensor msg start time randomization window */
#define CONFIG_FH_START_POLL_DATA_RAND_WINDOW 10000

/* default FH mode coordinator short address */
#define FH_COORD_SHORT_ADDR 0xAABB

/* max data failures for the sensor side */
#define CONFIG_MAX_DATA_FAILURES 5

/*!
 The minimum trickle timer window for PAN Advertisement,
 and PAN Configuration frame transmissions.
 Recommended to set this to half of PAS/PCS MIN Timer
*/
extern int linux_CONFIG_TRICKLE_MIN_CLK_DURATION;
#define CONFIG_TRICKLE_MIN_CLK_DURATION    linux_CONFIG_TRICKLE_MIN_CLK_DURATION

/*!
 The maximum trickle timer window for PAN Advertisement,
 and PAN Configuration frame transmissions.
 */
extern int linux_CONFIG_TRICKLE_MAX_CLK_DURATION;
#define CONFIG_TRICKLE_MAX_CLK_DURATION    linux_CONFIG_TRICKLE_MAX_CLK_DURATION

#if (((CONFIG_PHY_ID_DEFAULT >= APIMAC_MRFSK_STD_PHY_ID_BEGIN) && (CONFIG_PHY_ID_DEFAULT <= APIMAC_MRFSK_GENERIC_PHY_ID_BEGIN)) || \
    ((CONFIG_PHY_ID_DEFAULT >= APIMAC_200KBPS_915MHZ_PHY_132) && (CONFIG_PHY_ID_DEFAULT <= APIMAC_200KBPS_868MHZ_PHY_133)))

#define CONFIG_TRICKLE_MIN_CLK_DURATION_DEFAULT 3000
#define CONFIG_TRICKLE_MAX_CLK_DURATION_DEFAULT 6000
#else
/*!
 The minimum trickle timer window for PAN Advertisement,
 and PAN Configuration frame transmissions.
 Recommended to set this to half of PAS/PCS MIN Timer
*/
#define CONFIG_TRICKLE_MIN_CLK_DURATION_DEFAULT 30000
/*!
 The maximum trickle timer window for PAN Advertisement,
 and PAN Configuration frame transmissions.
 */
#define CONFIG_TRICKLE_MAX_CLK_DURATION_DEFAULT 60000
#endif

/*!
 To enable Doubling of PA/PC trickle time,
 useful when network has non sleepy nodes and
 there is a requirement to use PA/PC to convey updated
 PAN information. Note that when using option the CONFIG_TRICKLE_MIN_CLK_DURATION
 and CONFIG_TRICKLE_MAX_CLK_DURATION should be set to a sufficiently large value.
 Recommended values are 1 min and 16 min respectively.
*/
extern bool linux_CONFIG_DOUBLE_TRICKLE_TIMER;
#define CONFIG_DOUBLE_TRICKLE_TIMER    linux_CONFIG_DOUBLE_TRICKLE_TIMER
#define CONFIG_DOUBLE_TRICKLE_TIMER_DEFAULT false

/*! value for ApiMac_FHAttribute_netName */
extern char linux_CONFIG_FH_NETNAME[32];
#define CONFIG_FH_NETNAME            linux_CONFIG_FH_NETNAME
#define CONFIG_FH_NETNAME_DEFAULT    {"FHTest"}

/*!
 Value for Transmit Power in dBm
 For US and ETSI band, Default value is 10, allowed values are
 -10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 and 14dBm.
 For China band, allowed values are 6, 10, 13, 14 and 15dBm.
 For CC1190, allowed values are between 18, 23, 25, 26 and 27dBm.
 When the nodes in the network are close to each other
 lowering this value will help reduce saturation
*/
extern int linux_CONFIG_TRANSMIT_POWER;
#define CONFIG_TRANSMIT_POWER   linux_CONFIG_TRANSMIT_POWER

/*!
In Range extension mode the CCFG_FORCE_VDDR_HH must be set to 0 (Default)
in the COPROCESSSOR project's predefined symbols.

For Generic and Longrange 433 MHz, if CCFG_FORCE_VDDR_HH is 0 in the COPROCESSSOR
predefined symbols, then the transmit power must be less than 15. When CCFG_FORCE_VDDR_HH is 1
it must be exactly 15.

In US and ETSI band when CCFG_FORCE_VDDR_HH = 1, only possible value of transmit power is 14
*/
#if CONFIG_RANGE_EXT_MODE_DEFAULT
#define CONFIG_TRANSMIT_POWER_DEFAULT        27
#else
#if ((CONFIG_PHY_ID_DEFAULT == APIMAC_50KBPS_433MHZ_PHY_128) || (CONFIG_PHY_ID_DEFAULT == APIMAC_5KBPS_433MHZ_PHY_130))
#define CONFIG_TRANSMIT_POWER_DEFAULT        14
#else
#define CONFIG_TRANSMIT_POWER_DEFAULT        12
#endif
#endif


/*!
* Enable this mode for certfication.
* For FH certification, CONFIG_FH_ENABLE should
* also be enabled.
*/
#define CERTIFICATION_TEST_MODE linux_CERTIFICATION_TEST_MODE
extern int linux_CERTIFICATION_TEST_MODE;
#define CERTIFICATION_TEST_MODE_DEFAULT false

extern int linux_CONFIG_POLLING_INTERVAL;
extern int linux_CONFIG_REPORTING_INTERVAL;


#if 0
/* This test cannot be done on linux because these
 * are not implemented as "#defines" instead they
 * are implemented as variables
 */

/* Check if all the necessary parameters have been set for FH mode */
#if CONFIG_FH_ENABLE
#if !defined(FEATURE_ALL_MODES) && !defined(FEATURE_FREQ_HOP_MODE)
#error "Do you want to build image with frequency hopping mode? \
        Define either FEATURE_FREQ_HOP_MODE or FEATURE_ALL_MODES in ti_154stack_features.h"
#endif
#endif

/* Check if stack level security is enabled if application security is enabled */
#if CONFIG_SECURE
#if !defined(FEATURE_MAC_SECURITY)
#error "Define FEATURE_MAC_SECURITY or FEATURE_ALL_MODES in ti_154stack_features.h to \
        be able to use security at application level"
#endif
#endif

#endif /* if 0 */

#ifdef __cplusplus
}
#endif

#endif /* TI_154STACK_CONFIG_H */

/*
 *  ========================================
 *  Texas Instruments Micro Controller Style
 *  ========================================
 *  Local Variables:
 *  mode: c
 *  c-file-style: "bsd"
 *  tab-width: 4
 *  c-basic-offset: 4
 *  indent-tabs-mode: nil
 *  End:
 *  vim:set  filetype=c tabstop=4 shiftwidth=4 expandtab=true
 */

