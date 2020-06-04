/******************************************************************************

 @file cllc_linux.h

 @brief Linux Specific Coordinator Logical Link Controller

 Group: CMCU LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2019 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
#ifndef CLLC_LINUX_H
#define CLLC_LINUX_H

#ifdef __cplusplus
extern "C"
{
#endif

/******************************************************************************
 Includes
 *****************************************************************************/
#include "ti_154stack_config.h"

/******************************************************************************
 Function Prototypes
 *****************************************************************************/

/*!
 * @brief       Linux specific cllc initialization
 */
extern void CLLC_LINUX_init(uint8_t *chanMask, uint16_t *shortAddr,
                            uint32_t *fhPAtrickleTime, uint32_t *fhPCtrickleTime);

//*****************************************************************************
//*****************************************************************************

#ifdef __cplusplus
}
#endif

#endif /* CLLC_LINUX_H */

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
