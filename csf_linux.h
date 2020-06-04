/******************************************************************************
 @file csf_linux.h

 @brief TIMAC 2.0 API Collector specific (linux) function definitions

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/

#if !defined(CSF_LINUX_H)
#define CSF_LINUX_H

typedef uint8_t UArg;

/*!
 * Network parameters for a non-frequency hopping coordinator.
 */

/*! Network Information */
typedef struct
{
    /*! Device Information */
    ApiMac_deviceDescriptor_t deviceInfo;
    /*! Channel */
    uint8_t channel;
} Llc_networkInfo_t;

/*!
 * Frequency Hopping Interface Settings
 */
typedef struct _Llc_fhintervalsettings_t
{
    /*! Channel dwell time (in milliseconds) */
    uint16_t dwell;
    /*! Channel interval time (in milliseconds) */
    uint16_t interval;
} Llc_fhIntervalSettings_t;

/*!
 * Device frequency hopping information
 */
typedef struct _Llc_deviceinfofh_t
{
/*! Broadcast Interval settings */
    Llc_fhIntervalSettings_t bcIntervals;
    /*! Broadcast number of channels used */
    uint8_t bcNumChans;
    /*!
    Broadcast channels used.  Pointer to an array of bytes, Each byte
    is a channel number and the order is the sequence to hop.
    */
    uint8_t *pBcChans;
    /*! Unicast Rx Interval settings */
    Llc_fhIntervalSettings_t unicastIntervals;
    /*! Unicast Rx number of channels used */
    uint8_t unicastNumChans;
    /*!
     * Unicast channels used.  Pointer to an array of bytes, Each byte
     *  is a channel number and the order is the sequence to hop.
     */
    uint8_t *pUnicastChans;
} Llc_deviceInfoFh_t;

/*!
 * Network parameters for a frequency hopping coordinator.
 */
typedef struct _Llc_networkinfofh_t
{
    /*! Device Information */
    /* Address information */
    ApiMac_deviceDescriptor_t devInfo;
    /*! Device Frequency Hopping Information */
    Llc_deviceInfoFh_t fhInfo;
} Llc_networkInfoFh_t;

/*! Stored network information */
typedef struct
{
    /*! true if network is frequency hopping */
    bool fh;

    /*! union to hold network information */
    union
    {
        Llc_netInfo_t netInfo;
        Llc_networkInfoFh_t fhNetInfo;
    } info;
} Csf_networkInformation_t;

/*! for use by web interface */
typedef struct
{
    /*! Address information */
    ApiMac_deviceDescriptor_t devInfo;
    /*! Device capability */
    ApiMac_capabilityInfo_t  capInfo;
} Csf_deviceInformation_t;

/*
 * @brief Get the device list
 *
 * Note: Memory must be released via Csf_freeDeviceList()
 */
int Csf_getDeviceInformationList(Csf_deviceInformation_t **ppDeviceInfo);

/*
 * @brief Release memory from the getDeviceList call
 */
void Csf_freeDeviceInformationList(size_t n, Csf_deviceInformation_t *p);

/*
 * @brief given a state, return the ascii text name of this state (for dbg)
 * @param s - the state.
 */
const char *CSF_cllc_statename(Cllc_states_t s);

/*
 * @brief return the last known state of the CLLC.
 */
Cllc_states_t Csf_getCllcState(void);

/*!
 * @brief Send the configuration message to a collector module to be
 *        sent OTA.
 *
 * @param pDstAddr - destination address of the device to send the message
 * @param frameControl - configure what to the device is to report back.
 *                       Ref. Smsgs_dataFields_t.
 * @param reportingInterval - in millseconds- how often to report, 0
 *                            means to turn off automated reporting, but will
 *                            force the sensor device to send the Sensor Data
 *                            message once.
 * @param pollingInterval - in millseconds- how often to the device is to
 *                          poll its parent for data (for sleeping devices
 *                          only.
 *
 * @return status(uint8_t) - Success (0), Failure (1)
 */
extern uint8_t Csf_sendConfigRequest( ApiMac_sAddr_t *pDstAddr,
                uint16_t frameControl,
                uint32_t reportingInterval,
                uint32_t pollingInterval);
/*!
 * @brief Build and send the toggle led message to a device.
 *
 * @param pDstAddr - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern uint8_t Csf_sendToggleLedRequest(
                ApiMac_sAddr_t *pDstAddr);

/*!
 * @brief       The application calls this function to indicate that a device
 *              disassociated.
 *
 * @param       pSrcAddr - short address of the device that disassociated
 */
extern void Csf_deviceDisassocUpdate( ApiMac_sAddr_t *pSrcAddr );

/*!
 * @brief       Display divice short address when config data is received
 *
 * @param       pSrcAddr - short address of the device that sent the message
 */
extern void Csf_deviceConfigDisplay(ApiMac_sAddr_t *pSrcAddr);

/*!
 * @brief       The application calls this function to print out the reported
 *              device type
 *
 * @param       pSrcAddr - short address of the device that sent the message
 * @param       deviceFamilyID - the integer ID of the device family
 * @param       deviceTypeID - the integer ID of the board/device
 */
extern void Csf_deviceSensorDeviceTypeResponseUpdate(ApiMac_sAddr_t *pSrcAddr, uint8_t deviceFamilyID,
                                                     uint8_t deviceTypeID);

/*!
 * @brief       Display Sensor device and data
 *
 * @param       pDataInd - pointer to the data indication information
 */
extern void Csf_deviceSensorDisplay(ApiMac_mcpsDataInd_t *pDataInd);

/*!
 The application calls this function to indicate that a device
 has reported its FW version.

 Public function defined in csf.h
 */
 /*!
 * @brief       The application calls this function to indicate that a device
 *              has reported its FW version.
 *
 * @param       pSrcAddr - short address of the device that sent the message
 * @param       fwVerStr - the FW version string
 */
extern void Csf_deviceSensorFwVerUpdate(uint16_t srcAddr, char *fwVerStr);

/*!
 The application calls this function to indicate that a device
 has requested an OAD block.

 Public function defined in csf.h
 */
 /*!
 * @brief       The application calls this function to indicate that a device
 *              has reported its FW version.
 *
 * @param       pSrcAddr  - short address of the device that sent the message
 * @param       blockNum  - block requested
 * @param       NumBlocks - Total number of block
 */
extern void Csf_deviceSensorOadUpdate( uint16_t srcAddr, uint16_t imgId, uint16_t blockNum, uint16_t NumBlocks);

/*!
 The application calls this function to continue with FW update for on-chip OAD

 Public function defined in csf.h
 */
 /*!
 * @brief       The application calls this function to icontinue with
 *              FW update for on-chip OAD
 *
 * @param       pSrcAddr - short address of the device that sent the message
 */
extern void Csf_deviceSensorOadResetRspRcvd(uint16_t srcAddr);

/*!
 * @brief       The application calls this function to blink the identify LED.
 *
 * @param       identifyTime - time in seconds to identify for
 */
extern void Csf_identifyLED(uint16_t identifyTime, uint16_t shortAddr);

/*!
 * @brief       The application calls this function to reinitialize
 *              the MAC attributes on the CoProcessor after a reset
 */
extern void Csf_restoreMacAttributes(void);

/*!
 * @brief       The application calls this function tostart the device
 */
extern void Csf_processCoPReset(void);

/*!
 * @brief       Find the extended address froma given short address
 *
 * @param       shortAddr - short address used to find the device
 * @param       pExtAddr - Memory location to copy the found extended address to.
 *
 * @return      true if found and extended address is copied to pExtAddr, false if not
 */
bool Csf_getDeviceExtended(uint16_t shortAddr, ApiMac_sAddrExt_t *pExtAddr);

/*!
 * @brief       The application calls this function to indicate that a device
 *              has reported raw sensor data.
 *
 *              The information will be saved.
 *
 * @param       pDataInd - raw inbound data
 */
extern void Csf_deviceRawDataUpdate(ApiMac_mcpsDataInd_t *pDataInd);

/*!
 * @brief       Handles printing that the orphaned device joined back
 *
 * @return      none
 */
extern void Csf_IndicateOrphanReJoin(uint16_t shortAddr);

#endif

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

