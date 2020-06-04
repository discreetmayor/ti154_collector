/******************************************************************************

 @file cllc_linux.c

 @brief Linux Specific Coordinator Logical Link Controller

 Group: CMCU LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2019 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/

/******************************************************************************
 Includes
 *****************************************************************************/
#include <string.h>
#include "cllc.h"
#include "cllc_linux.h"

/******************************************************************************
 Public Functions
 *****************************************************************************/
/*!
 Linux specific init

 Public function defined in cllc_linux.h
 */
void CLLC_LINUX_init(uint8_t *chanMask, uint16_t *shortAddr, uint32_t *fhPAtrickleTime, uint32_t *fhPCtrickleTime)
{
    if(CONFIG_FH_ENABLE)
    {
        memcpy( chanMask, linux_FH_ASYNC_CHANNEL_MASK, APIMAC_154G_CHANNEL_BITMAP_SIZ);
    }
    else
    {
        memcpy( chanMask, linux_CONFIG_CHANNEL_MASK, APIMAC_154G_CHANNEL_BITMAP_SIZ);
    }
    *shortAddr = CONFIG_COORD_SHORT_ADDR;

    if(!linux_CERTIFICATION_TEST_MODE)
    {
        linux_CONFIG_TRICKLE_MIN_CLK_DURATION = linux_CONFIG_TRICKLE_MIN_CLK_DURATION;
        linux_CONFIG_TRICKLE_MAX_CLK_DURATION = linux_CONFIG_TRICKLE_MAX_CLK_DURATION;
    }
    else if((linux_CONFIG_PHY_ID >= APIMAC_MRFSK_STD_PHY_ID_BEGIN) && (CONFIG_PHY_ID <= APIMAC_MRFSK_GENERIC_PHY_ID_BEGIN))
    {
        linux_CONFIG_TRICKLE_MIN_CLK_DURATION = 6000;
        linux_CONFIG_TRICKLE_MAX_CLK_DURATION = 6000;
    }
    else
    {
        linux_CONFIG_TRICKLE_MIN_CLK_DURATION = 30000;
        linux_CONFIG_TRICKLE_MAX_CLK_DURATION = 30000;
    }

    *fhPAtrickleTime = linux_CONFIG_TRICKLE_MIN_CLK_DURATION;
    *fhPCtrickleTime = linux_CONFIG_TRICKLE_MAX_CLK_DURATION;
}

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
