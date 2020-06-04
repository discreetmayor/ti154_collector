/******************************************************************************

 @file llc.h

 @brief Logical Link Controller shared definitions

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
#ifndef LLC_H
#define LLC_H

/******************************************************************************
 Includes
 *****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "api_mac.h"

#ifdef __cplusplus
extern "C"
{
#endif

/******************************************************************************
 Structures - Building blocks for the LLC
 *****************************************************************************/

/*! Network Information */
typedef struct
{
    /* Address information */
    ApiMac_deviceDescriptor_t devInfo;
    /*! Channel - non FH */
    uint8_t channel;
    /*! true if network is frequency hopping */
    bool fh;
} Llc_netInfo_t;

/* Structure to store a device list entry in NV */
typedef struct _llc_devicelistitem
{
    /* Address information */
    ApiMac_deviceDescriptor_t devInfo;
    /* Device capability */
    ApiMac_capabilityInfo_t capInfo;
    /* RX frame counter */
    uint32_t rxFrameCounter;
} Llc_deviceListItem_t;
#ifdef __cplusplus
}
#endif

#endif /* LLC_H */

