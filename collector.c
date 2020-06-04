/******************************************************************************

 @file collector.c

 @brief TIMAC 2.0 Collector Example Application

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/

/******************************************************************************
 Includes
 *****************************************************************************/
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <libgen.h>

#include "mac_util.h"
#include "api_mac.h"
#include "cllc.h"
#include "csf.h"
#include "smsgs.h"
#include "collector.h"

#include "log.h"

#include "oad_protocol.h"
#include "oad_storage.h"
#include "oad_image_header.h"

/******************************************************************************
 Constants and definitions
 *****************************************************************************/

#if !defined(CONFIG_AUTO_START)
#if defined(AUTO_START)
#define CONFIG_AUTO_START 1
#else
#define CONFIG_AUTO_START 0
#endif
#endif

/* Default MSDU Handle rollover */
#define MSDU_HANDLE_MAX 0x3F

/* App marker in MSDU handle */
#define APP_MARKER_MSDU_HANDLE 0x80

/* App Config request marker for the MSDU handle */
#define APP_CONFIG_MSDU_HANDLE 0x40

/* Ramp data request marker for the MSDU handle */
#define RAMP_DATA_MSDU_HANDLE 0x20
/* App Broadcast Cmd Msg marker for the MSDU Handle */
#define APP_BROADCAST_MSDU_HANDLE 0x20
/* Default configuration frame control */
#define CONFIG_FRAME_CONTROL (Smsgs_dataFields_tempSensor | \
                              Smsgs_dataFields_lightSensor | \
                              Smsgs_dataFields_humiditySensor | \
                              Smsgs_dataFields_msgStats | \
                              Smsgs_dataFields_configSettings)

/* Delay for config request retry in busy network */
#define CONFIG_DELAY 2000
#define CONFIG_RESPONSE_DELAY 3*CONFIG_DELAY
/* Tracking timeouts */
#define TRACKING_CNF_DELAY_TIME 2000 /* in milliseconds */

#if (CONFIG_PHY_ID == APIMAC_50KBPS_915MHZ_PHY_1) || \
    (CONFIG_PHY_ID == APIMAC_50KBPS_868MHZ_PHY_3) || \
    (CONFIG_PHY_ID == APIMAC_50KBPS_433MHZ_PHY_128)
    #define SYMBOL_DURATION         (SYMBOL_DURATION_50_kbps)  //us

#elif (CONFIG_PHY_ID == APIMAC_200KBPS_915MHZ_PHY_132) || \
      (CONFIG_PHY_ID == APIMAC_200KBPS_868MHZ_PHY_133)
    #define SYMBOL_DURATION         (SYMBOL_DURATION_200_kbps) //us

#elif (CONFIG_PHY_ID == APIMAC_5KBPS_915MHZ_PHY_129) || \
      (CONFIG_PHY_ID == APIMAC_5KBPS_433MHZ_PHY_130) || \
      (CONFIG_PHY_ID == APIMAC_5KBPS_868MHZ_PHY_131)
    #define SYMBOL_DURATION         (SYMBOL_DURATION_LRM)      //us

#elif (CONFIG_PHY_ID == APIMAC_250KBPS_IEEE_PHY_0)  // 2.4g
    #define SYMBOL_DURATION         (SYMBOL_DURATION_250_kbps)  //us
#else
    #define SYMBOL_DURATION         (SYMBOL_DURATION_50_kbps)  //us
#endif

#if (CONFIG_MAC_BEACON_ORDER != NON_BEACON_ORDER)
/* This is 3 times the polling interval used in beacon mode. */
#define TRACKING_TIMEOUT_TIME ((1<<CONFIG_MAC_BEACON_ORDER) * 960 * SYMBOL_DURATION * 3 / 1000) /*in milliseconds*/
#else
#define TRACKING_TIMEOUT_TIME (CONFIG_POLLING_INTERVAL * 3) /*in milliseconds*/
#endif
/* Initial delay before broadcast transmissions are started in FH mode */
#define BROADCAST_CMD_START_TIME 60000

/* Assoc Table (CLLC) status settings */
#define ASSOC_CONFIG_SENT       0x0100    /* Config Req sent */
#define ASSOC_CONFIG_RSP        0x0200    /* Config Rsp received */
#define ASSOC_CONFIG_MASK       0x0300    /* Config mask */
#define ASSOC_TRACKING_SENT     0x1000    /* Tracking Req sent */
#define ASSOC_TRACKING_RSP      0x2000    /* Tracking Rsp received */
#define ASSOC_TRACKING_RETRY    0x4000    /* Tracking Req retried */
#define ASSOC_TRACKING_ERROR    0x8000    /* Tracking Req error */
#define ASSOC_TRACKING_MASK     0xF000    /* Tracking mask  */

#define MAX_OAD_FILES           10

#define TURBO_OAD_HEADER_LEN	64

#ifdef TIRTOS_IN_ROM
#define IMG_HDR_ADDR            0x04F0
#else
#define IMG_HDR_ADDR            0x0000
#endif

/* Delta image info segment constants  */
#define IMG_DELTA_SEG_ID                  0x05
#define DELTA_SEG_LEN                     0x14
#define DELTA_SEG_IS_DELTA_IMG_OFFSET     0x08
#define DELTA_SEG_HEADER_VERSION_OFFSET   0x09
#define DELTA_SEG_VERSION_OFFSET          0x0A
#define DELTA_SEG_MEMORY_CFG_OFFSET       0x0B
#define DELTA_SEG_OLD_IMG_CRC_OFFSET      0x0C
#define DELTA_SEG_NEW_IMG_LEN_OFFSET      0x10
#define DELTA_SEG_NOT_FOUND               -1

/* OAD header constants  */
#define OAD_FIXED_HDR_LEN            0x2C
#define OAD_SEG_ID_OFFSET            0x00
#define OAD_SEG_LEN_OFFSET           0x04

typedef struct
{
    uint8_t oad_file_id;
    char oad_file[256];
}oadFile_t;

/******************************************************************************
 Global variables
 *****************************************************************************/

/* Task pending events */
uint16_t Collector_events = 0;

/*! Collector statistics */
Collector_statistics_t Collector_statistics;

/******************************************************************************
 Local variables
 *****************************************************************************/

static void *sem;

/*! true if the device was restarted */
static bool restarted = false;

/*! CLLC State */
static Cllc_states_t cllcState = Cllc_states_initWaiting;

/*! Device's PAN ID */
static uint16_t devicePanId = 0xFFFF;

/*! Device's Outgoing MSDU Handle values */
static uint8_t deviceTxMsduHandle = 0;

static bool fhEnabled = false;

static oadFile_t oad_file_list[MAX_OAD_FILES] = {{0}};
static uint16_t oadBNumBlocks;

/******************************************************************************
 Local function prototypes
 *****************************************************************************/
static void initializeClocks(void);
static void cllcStartedCB(Llc_netInfo_t *pStartedInfo);
static ApiMac_assocStatus_t cllcDeviceJoiningCB(
                ApiMac_deviceDescriptor_t *pDevInfo,
                ApiMac_capabilityInfo_t *pCapInfo);
static void cllcStateChangedCB(Cllc_states_t state);
static void dataCnfCB(ApiMac_mcpsDataCnf_t *pDataCnf);
static void dataIndCB(ApiMac_mcpsDataInd_t *pDataInd);
static void disassocIndCB(ApiMac_mlmeDisassociateInd_t *pDisassocInd);
static void disassocCnfCB(ApiMac_mlmeDisassociateCnf_t *pDisassocCnf);
static void processStartEvent(void);
#ifndef PROCESS_JS
static void processConfigResponse(ApiMac_mcpsDataInd_t *pDataInd);
static void processSensorData(ApiMac_mcpsDataInd_t *pDataInd);
#endif
static void processTrackingResponse(ApiMac_mcpsDataInd_t *pDataInd);
static void processToggleLedResponse(ApiMac_mcpsDataInd_t *pDataInd);
static void processDeviceTypeResponse(ApiMac_mcpsDataInd_t *pDataInd);
static void processOadData(ApiMac_mcpsDataInd_t *pDataInd);
static Cllc_associated_devices_t *findDevice(ApiMac_sAddr_t *pAddr);
static Cllc_associated_devices_t *findDeviceStatusBit(uint16_t mask, uint16_t statusBit);
static uint8_t getMsduHandle(Smsgs_cmdIds_t msgType);
static bool sendMsg(Smsgs_cmdIds_t type, uint16_t dstShortAddr, bool rxOnIdle,
                    uint16_t len,
                    uint8_t *pData);
static void generateConfigRequests(void);
static void generateTrackingRequests(void);
static void generateBroadcastCmd(void);
static void sendTrackingRequest(Cllc_associated_devices_t *pDev);
static void commStatusIndCB(ApiMac_mlmeCommStatusInd_t *pCommStatusInd);
static void pollIndCB(ApiMac_mlmePollInd_t *pPollInd);

static void processDataRetry(ApiMac_sAddr_t *pAddr);
static void processConfigRetry(void);
static void processIdentifyLedRequest(ApiMac_mcpsDataInd_t *pDataInd);
static void orphanIndCb(ApiMac_mlmeOrphanInd_t *pData);

static void oadFwVersionRspCb(void* pSrcAddr, char *fwVersionStr);
static void oadImgIdentifyRspCb(void* pSrcAddr, uint8_t status);
static void oadBlockReqCb(void* pSrcAddr, uint8_t imgId, uint16_t blockNum, uint16_t multiBlockSize);

static void oadResetRspCb(void* pSrcAddr);

static void* oadRadioAccessAllocMsg(uint32_t size);
static OADProtocol_Status_t oadRadioAccessPacketSend(void* pDstAddr, uint8_t *pMsg, uint32_t msgLen);

static long findDeltaSeg(FILE* pFile);

/******************************************************************************
 Callback tables
 *****************************************************************************/

/*! API MAC Callback table */
ApiMac_callbacks_t Collector_macCallbacks =
    {
      /*! Associate Indicated callback */
      NULL,
      /*! Associate Confirmation callback */
      NULL,
      /*! Disassociate Indication callback */
      disassocIndCB,
      /*! Disassociate Confirmation callback */
      disassocCnfCB,
      /*! Beacon Notify Indication callback */
      NULL,
      /*! Orphan Indication callback */
      orphanIndCb,
      /*! Scan Confirmation callback */
      NULL,
      /*! Start Confirmation callback */
      NULL,
      /*! Sync Loss Indication callback */
      NULL,
      /*! Poll Confirm callback */
      NULL,
      /*! Comm Status Indication callback */
      commStatusIndCB,
      /*! Poll Indication Callback */
      pollIndCB,
      /*! Data Confirmation callback */
      dataCnfCB,
      /*! Data Indication callback */
      dataIndCB,
      /*! Reset Indication callback */
      NULL,
      /*! Purge Confirm callback */
      NULL,
      /*! WiSUN Async Indication callback */
      NULL,
      /*! WiSUN Async Confirmation callback */
      NULL,
      /*! Unprocessed message callback */
      NULL
    };

static Cllc_callbacks_t cllcCallbacks =
    {
      /*! Coordinator Started Indication callback */
      cllcStartedCB,
      /*! Device joining callback */
      cllcDeviceJoiningCB,
      /*! The state has changed callback */
      cllcStateChangedCB
    };

static OADProtocol_RadioAccessFxns_t  oadRadioAccessFxns =
    {
      oadRadioAccessAllocMsg,
      oadRadioAccessPacketSend
    };

static OADProtocol_MsgCBs_t oadMsgCallbacks =
    {
      /*! Incoming FW Req */
      NULL,
      /*! Incoming FW Version Rsp */
      oadFwVersionRspCb,
      /*! Incoming Image Identify Req */
      NULL,
      /*! Incoming Image Identify Rsp */
      oadImgIdentifyRspCb,
      /*! Incoming OAD Block Req */
      oadBlockReqCb,
      /*! Incoming OAD Block Rsp */
      NULL,
      /*! Incoming OAD Reset Req */
      NULL,
      /*! Incoming OAD Reset Rsp*/
      oadResetRspCb
    };

/******************************************************************************
 Public Functions
 *****************************************************************************/

/*!
 Initialize this application.

 Public function defined in collector.h
 */
void Collector_init(void)
{
    OADProtocol_Params_t OADProtocol_params;

    /* Initialize the collector's statistics */
    memset(&Collector_statistics, 0, sizeof(Collector_statistics_t));

    /* Initialize the MAC */
    sem = ApiMac_init(CONFIG_FH_ENABLE);

    ApiMac_mlmeSetReqUint8(ApiMac_attribute_phyCurrentDescriptorId,
                           (uint8_t)CONFIG_PHY_ID);

    ApiMac_mlmeSetReqUint8(ApiMac_attribute_channelPage,
                           (uint8_t)CONFIG_CHANNEL_PAGE);

    /* Initialize the Coordinator Logical Link Controller */
    Cllc_init(&Collector_macCallbacks, &cllcCallbacks);

    /* Register the MAC Callbacks */
    ApiMac_registerCallbacks(&Collector_macCallbacks);

    /* Initialize the platform specific functions */
    Csf_init(sem);

    /* Set the indirect persistent timeout */
    if(CONFIG_MAC_BEACON_ORDER != NON_BEACON_ORDER)
    {
        ApiMac_mlmeSetReqUint16(ApiMac_attribute_transactionPersistenceTime,
            BCN_MODE_INDIRECT_PERSISTENT_TIME);
    }
    else
    {
        ApiMac_mlmeSetReqUint16(ApiMac_attribute_transactionPersistenceTime,
            INDIRECT_PERSISTENT_TIME);
    }

    /* Initialize PA/LNA if enabled */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_rangeExtender,
                           (uint8_t)CONFIG_RANGE_EXT_MODE);

    ApiMac_mlmeSetReqUint8(ApiMac_attribute_phyTransmitPowerSigned,
                           (uint8_t)CONFIG_TRANSMIT_POWER);
    /* Set Min BE */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_backoffExponent,
                              (uint8_t)CONFIG_MIN_BE);
    /* Set Max BE */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_maxBackoffExponent,
                              (uint8_t)CONFIG_MAX_BE);
    /* Set MAC MAX CSMA Backoffs */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_maxCsmaBackoffs,
                              (uint8_t)CONFIG_MAC_MAX_CSMA_BACKOFFS);
    /* Set MAC MAX Frame Retries */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_maxFrameRetries,
                              (uint8_t)CONFIG_MAX_RETRIES);
#ifdef FCS_TYPE16
    /* Set the fcs type */
    ApiMac_mlmeSetReqBool(ApiMac_attribute_fcsType,
                           (bool)1);
#endif

    /* Initialize the app clocks */
    initializeClocks();
    if(CONFIG_FH_ENABLE && (FH_BROADCAST_DWELL_TIME > 0))
    {
        /* Start broadcast frame transmissions in FH mode if broadcast dwell time
         * is greater than zero */
        Csf_setBroadcastClock(BROADCAST_CMD_START_TIME);
    }

    if(CONFIG_AUTO_START)
    {
        /* Start the device */
        Util_setEvent(&Collector_events, COLLECTOR_START_EVT);
    }

    OADProtocol_Params_init(&OADProtocol_params);
    OADProtocol_params.pRadioAccessFxns = &oadRadioAccessFxns;
    OADProtocol_params.pProtocolMsgCallbacks = &oadMsgCallbacks;

    OADProtocol_open(&OADProtocol_params);

}

/*!
 Application task processing.

 Public function defined in collector.h
 */
void Collector_process(void)
{
    /* Start the collector device in the network */
    if(Collector_events & COLLECTOR_START_EVT)
    {
        if(cllcState == Cllc_states_initWaiting)
        {
            processStartEvent();
        }

        /* Clear the event */
        Util_clearEvent(&Collector_events, COLLECTOR_START_EVT);
    }

    /* Is it time to send the next tracking message? */
    if(Collector_events & COLLECTOR_TRACKING_TIMEOUT_EVT)
    {
        /* Process Tracking Event */
        generateTrackingRequests();

        /* Clear the event */
        Util_clearEvent(&Collector_events, COLLECTOR_TRACKING_TIMEOUT_EVT);
    }

    /*
     The generate a config request for all associated devices that need one
     */
    if(Collector_events & COLLECTOR_CONFIG_EVT)
    {
        generateConfigRequests();

        /* Clear the event */
        Util_clearEvent(&Collector_events, COLLECTOR_CONFIG_EVT);
    }

    /*
     Collector generate a broadcast command message for FH mode
     */
    if(Collector_events & COLLECTOR_BROADCAST_TIMEOUT_EVT)
    {
        /* Clear the event */
        Util_clearEvent(&Collector_events, COLLECTOR_BROADCAST_TIMEOUT_EVT);
        if(FH_BROADCAST_INTERVAL > 0 && (!CERTIFICATION_TEST_MODE))
        {
            generateBroadcastCmd();
            /* set clock for next broadcast command */
            Csf_setBroadcastClock(FH_BROADCAST_INTERVAL);
        }
    }

    /* Process LLC Events */
    Cllc_process();

    /* Allow the Specific functions to process */
    Csf_processEvents();

    /*
     Don't process ApiMac messages until all of the collector events
     are processed.
     */
    if(Collector_events == 0)
    {
        /* Wait for response message or events */
        ApiMac_processIncoming();
    }
}

/*!
 Build and send the configuration message to a device.

 Public function defined in collector.h
 */
Collector_status_t Collector_sendConfigRequest(ApiMac_sAddr_t *pDstAddr,
                                               uint16_t frameControl,
                                               uint32_t reportingInterval,
                                               uint32_t pollingInterval)
{
    Collector_status_t status = Collector_status_invalid_state;

    /* Are we in the right state? */
    if(cllcState >= Cllc_states_started)
    {
        Llc_deviceListItem_t item;

        /* Is the device a known device? */
        if(Csf_getDevice(pDstAddr, &item))
        {
            uint8_t buffer[SMSGS_CONFIG_REQUEST_MSG_LENGTH];
            uint8_t *pBuf = buffer;

            /* Build the message */
            *pBuf++ = (uint8_t)Smsgs_cmdIds_configReq;
            *pBuf++ = Util_loUint16(frameControl);
            *pBuf++ = Util_hiUint16(frameControl);
            *pBuf++ = Util_breakUint32(reportingInterval, 0);
            *pBuf++ = Util_breakUint32(reportingInterval, 1);
            *pBuf++ = Util_breakUint32(reportingInterval, 2);
            *pBuf++ = Util_breakUint32(reportingInterval, 3);
            *pBuf++ = Util_breakUint32(pollingInterval, 0);
            *pBuf++ = Util_breakUint32(pollingInterval, 1);
            *pBuf++ = Util_breakUint32(pollingInterval, 2);
            *pBuf = Util_breakUint32(pollingInterval, 3);

            if((sendMsg(Smsgs_cmdIds_configReq, item.devInfo.shortAddress,
                        item.capInfo.rxOnWhenIdle,
                        (SMSGS_CONFIG_REQUEST_MSG_LENGTH),
                         buffer)) == true)
            {
                status = Collector_status_success;
                Collector_statistics.configRequestAttempts++;
                /* set timer for retry in case response is not received */
                Csf_setConfigClock(CONFIG_DELAY);
            }
            else
            {
                processConfigRetry();
            }
        }
    }

    return (status);
}

/*!
 Update the collector statistics

 Public function defined in collector.h
 */
void Collector_updateStats( void )
{
    /* update the stats from the MAC */
    ApiMac_mlmeGetReqUint32(ApiMac_attribute_diagRxSecureFail,
                            &Collector_statistics.rxDecryptFailures);

    ApiMac_mlmeGetReqUint32(ApiMac_attribute_diagTxSecureFail,
                            &Collector_statistics.txEncryptFailures);
}

/*!
 Build and send the toggle led message to a device.

 Public function defined in collector.h
 */
Collector_status_t Collector_sendToggleLedRequest(ApiMac_sAddr_t *pDstAddr)
{
    Collector_status_t status = Collector_status_invalid_state;

    /* Are we in the right state? */
    if(cllcState >= Cllc_states_started)
    {
        Llc_deviceListItem_t item;

        /* Is the device a known device? */
        if(Csf_getDevice(pDstAddr, &item))
        {
            uint8_t buffer[SMSGS_TOGGLE_LED_REQUEST_MSG_LEN];

            /* Build the message */
            buffer[0] = (uint8_t)Smsgs_cmdIds_toggleLedReq;

            sendMsg(Smsgs_cmdIds_toggleLedReq, item.devInfo.shortAddress,
                    item.capInfo.rxOnWhenIdle,
                    SMSGS_TOGGLE_LED_REQUEST_MSG_LEN,
                    buffer);

            status = Collector_status_success;
        }
        else
        {
            status = Collector_status_deviceNotFound;
        }
    }

    return(status);
}

/*!
 * @brief Build and send the device type request message to a device.
 *
 * @param pDstAddr - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 *
 * Public function defined in collector.h
 */
Collector_status_t Collector_sendDeviceTypeRequest(ApiMac_sAddr_t *pDstAddr)
{
    Collector_status_t status = Collector_status_invalid_state;

    /* Are we in the right state? */
    if(cllcState >= Cllc_states_started)
    {
        Llc_deviceListItem_t item;

        /* Is the device a known device? */
        if(Csf_getDevice(pDstAddr, &item))
        {
            uint8_t buffer[SMSGS_DEVICE_TYPE_REQUEST_MSG_LEN];

            /* Build the message */
            buffer[0] = (uint8_t)Smsgs_cmdIds_DeviceTypeReq;

            sendMsg(Smsgs_cmdIds_DeviceTypeReq, item.devInfo.shortAddress,
                    item.capInfo.rxOnWhenIdle,
                    SMSGS_DEVICE_TYPE_REQUEST_MSG_LEN,
                    buffer);

            status = Collector_status_success;
        }
        else
        {
            status = Collector_status_deviceNotFound;
        }
    }

    return(status);
}

/*!
 Stores the file name associated with the file_id provided
 in file_name
 Returns
 Public function defined in collector.h
 */
Collector_status_t Collector_getFileName(uint32_t file_id, char* file_name, size_t max_len)
{
  Collector_status_t status;
  if (file_id >= MAX_OAD_FILES)
  {
    status = Collector_status_invalid_file_id;
  }
  else
  {
    strncpy(file_name, basename(oad_file_list[file_id].oad_file), max_len);
    status = Collector_status_success;
  }

  return status;
}

/*!
 updates the FW list.

 Public function defined in collector.h
 */
uint32_t Collector_updateFwList(char *new_oad_file)
{
    uint32_t oad_file_idx;
    uint32_t oad_file_id;
    bool found = false;

    LOG_printf( LOG_DBG_COLLECTOR, "Collector_updateFwList: new oad file: %s\n",
                          new_oad_file);

    /* Does OAD file exist */
    for(oad_file_idx = 0; oad_file_idx < MAX_OAD_FILES; oad_file_idx++)
    {
        if(strcmp(new_oad_file, oad_file_list[oad_file_idx].oad_file) == 0)
        {
            LOG_printf( LOG_DBG_COLLECTOR, "Collector_updateFwList: found ID: %d\n",
                          oad_file_list[oad_file_idx].oad_file_id);
            oad_file_id = oad_file_list[oad_file_idx].oad_file_id;
            found = true;
            break;
        }
    }

    if(!found)
    {
        static uint32_t latest_oad_file_idx = 0;
        static uint32_t latest_oad_file_id = 0;

        oad_file_id = latest_oad_file_id;

        oad_file_list[latest_oad_file_idx].oad_file_id = oad_file_id;
        strncpy(oad_file_list[latest_oad_file_idx].oad_file, new_oad_file, 256);

        LOG_printf( LOG_DBG_COLLECTOR, "Collector_updateFwList: Added %s, ID %d\n",
              oad_file_list[latest_oad_file_idx].oad_file,
              oad_file_list[latest_oad_file_idx].oad_file_id);

        latest_oad_file_id++;
        latest_oad_file_idx++;
        if(latest_oad_file_idx == MAX_OAD_FILES)
        {
            latest_oad_file_idx = 0;
        }
    }

    return oad_file_id;
}


/*!
 Send OAD version request message.

 Public function defined in collector.h
 */
Collector_status_t Collector_sendFwVersionRequest(ApiMac_sAddr_t *pDstAddr)
{
    Collector_status_t status = Collector_status_invalid_state;

    if(OADProtocol_sendFwVersionReq((void*) pDstAddr) == OADProtocol_Status_Success)
    {
        status = Collector_status_success;
    }

    return status;
}


/*!
 Send OAD Target Reset Request message.

 Public function defined in collector.h
 */
Collector_status_t Collector_sendResetReq(ApiMac_sAddr_t *pDstAddr)
{
    Collector_status_t status = Collector_status_invalid_state;

    if(OADProtocol_sendOadResetReq((void*) pDstAddr) == OADProtocol_Status_Success)
    {
        status = Collector_status_success;
    }

    return status;
}


/*!
 Send OAD version request message.

 Public function defined in collector.h
 */
Collector_status_t Collector_startFwUpdate(ApiMac_sAddr_t *pDstAddr, uint32_t oad_file_id)
{
    Collector_status_t status = Collector_status_invalid_state;
    uint8_t imgInfoData[OADProtocol_AGAMA_IMAGE_HDR_LEN];
    uint32_t oad_file_idx;
    FILE *oadFile;

    for(oad_file_idx = 0; oad_file_idx < MAX_OAD_FILES; oad_file_idx++)
    {
        if(oad_file_list[oad_file_idx].oad_file_id == oad_file_id)
        {
          LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: opening file: %s\n",
                          oad_file_list[oad_file_idx].oad_file);

          oadFile = fopen(oad_file_list[oad_file_idx].oad_file, "r");
          break;
        }
    }

    if(oadFile)
    {
        const uint32_t chameleonOADHdrLen = 16;
        LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: opened file....\n");

		//Seek set to start of oad header in the image
        fseek(oadFile, IMG_HDR_ADDR, SEEK_SET);

        //Read the first chameleonOADHdrLen bytes to determine between Agama and Chameleon
        fread(imgInfoData, 1, chameleonOADHdrLen, oadFile);

        OADStorage_imgIdentifyPld_t imgIdPld;
        imgHdr_t* pImgHdr = (imgHdr_t*) imgInfoData;

        if((strncmp((const char*) pImgHdr->fixedHdr.imgID, CC26X2_OAD_IMG_ID_VAL, OAD_IMG_ID_LEN ) == 0)
            || (strncmp((const char*) pImgHdr->fixedHdr.imgID, CC13X2_OAD_IMG_ID_VAL, OAD_IMG_ID_LEN) == 0))
        {
            LOG_printf( LOG_DBG_COLLECTOR, "collector_startfwupdate: Binary identified as an Agama image\n");

            /*
             * 	Since it is known to be an Agama binary, the rest of the header must be read from the file
             * into the imgInfoData buffer.
             */
            uint32_t remReadLen = OADProtocol_AGAMA_IMAGE_HDR_LEN - chameleonOADHdrLen;
            if(fread(&imgInfoData[chameleonOADHdrLen], 1, remReadLen, oadFile) == remReadLen)
            {
                //copy image identify payload
                memcpy(imgIdPld.imgID, pImgHdr->fixedHdr.imgID, 8);
                imgIdPld.bimVer = pImgHdr->fixedHdr.bimVer;
                imgIdPld.metaVer = pImgHdr->fixedHdr.metaVer;
                imgIdPld.imgCpStat = pImgHdr->fixedHdr.imgCpStat;
                imgIdPld.crcStat = pImgHdr->fixedHdr.crcStat;
                imgIdPld.imgType = pImgHdr->fixedHdr.imgType;
                imgIdPld.imgNo = pImgHdr->fixedHdr.imgNo;
                imgIdPld.len = pImgHdr->fixedHdr.len;
                memcpy(imgIdPld.softVer, pImgHdr->fixedHdr.softVer, 4);

                // Check if binary is a delta image
                imgIdPld.isDeltaImg = false;
                long deltaSegPos = findDeltaSeg(oadFile);

                if(deltaSegPos != DELTA_SEG_NOT_FOUND)
                {
                    uint8_t oadSeg[DELTA_SEG_LEN];
                    fseek(oadFile, deltaSegPos, SEEK_SET);
                    fread(oadSeg, 1, DELTA_SEG_LEN, oadFile);

                    // Binaries may have delta segments, but not have a delta payload
                    if (oadSeg[DELTA_SEG_IS_DELTA_IMG_OFFSET])
                    {
                        imgIdPld.isDeltaImg = true;
                        imgIdPld.toadMetaVer = oadSeg[DELTA_SEG_HEADER_VERSION_OFFSET];
                        imgIdPld.toadVer = oadSeg[DELTA_SEG_VERSION_OFFSET];
                        imgIdPld.memoryCfg = oadSeg[DELTA_SEG_MEMORY_CFG_OFFSET];
                        imgIdPld.oldImgCrc = *((uint32_t*)(&oadSeg[DELTA_SEG_OLD_IMG_CRC_OFFSET]));
                        imgIdPld.newImgLen = *((uint32_t*)(&oadSeg[DELTA_SEG_NEW_IMG_LEN_OFFSET]));
                    }
                }

                LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: sending ImgIdentifyReq, Img Len 0x%x\n",
                    pImgHdr->fixedHdr.len);

                oadBNumBlocks = pImgHdr->fixedHdr.len / OAD_BLOCK_SIZE;

                if(pImgHdr->fixedHdr.len % OAD_BLOCK_SIZE)
                {
                    //there are some remaining bytes in an additional block
                    oadBNumBlocks++;
                }

                if(OADProtocol_sendImgIdentifyReq((void*) pDstAddr, oad_file_id,
                    (uint8_t*) &imgIdPld) == OADProtocol_Status_Success)
                {
                    status = Collector_status_success;
                }
            }
            else
            {
                LOG_printf(LOG_DBG_COLLECTOR, "Collector_startFwUpdate: Error reading remaining %u bytes of header",
                    remReadLen);
                status = Collector_status_invalid_file;
            }
        }
        else
        {
            /*
             * 	If it is not an Agama image, we must determine between a Turbo OAD image and a regular
             * Chameleon image. The header of a Chameleon image does not contain a unique ID val.
             * Therefor we will use the unique ID val of a Turbo OAD image to destinguish between them.
             */

            //Turbo OAD header is always placed at 0x00 no matter where TIRTOS was defined to be.
            fseek(oadFile, 0x00, SEEK_SET);
            if(fread(imgInfoData, 1, TURBO_OAD_HEADER_LEN, oadFile) == TURBO_OAD_HEADER_LEN)
            {
                if(strncmp((const char *)&imgInfoData[16], "TURBOOAD", sizeof("TURBOOAD")) != 0)
                {
                    //If it is not a TURBO OAD image we must assume it is a regular Chameleon image
                    fseek(oadFile, IMG_HDR_ADDR, SEEK_SET);
                    fread(imgInfoData, 1, chameleonOADHdrLen, oadFile);
                    LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: Binary identified as a Chameleon image\n");
                }
                else
                {
                    LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: Binary identified as a Turbo image\n");
                }

                uint32_t oadImgLen = ((imgInfoData[6]) | (imgInfoData[7] << 8));

                LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: sending ImgIdentifyReq, Img Len 0x%x\n",
                    oadImgLen);

                oadBNumBlocks =  oadImgLen / (OAD_BLOCK_SIZE >> 2);
                if(oadImgLen < (OAD_BLOCK_SIZE >> 2))
                {
                    //necessary when oadImgLen is less than one block (common in Turbo oad)
                    oadBNumBlocks++;
                }
                else if(oadImgLen % (OAD_BLOCK_SIZE >> 2))
                {
                    //there are some remaining bytes in an additional block
                    oadBNumBlocks++;
                }

                if(OADProtocol_sendImgIdentifyReq((void*) pDstAddr, oad_file_id, imgInfoData)
                    == OADProtocol_Status_Success)
                {
                    status = Collector_status_success;
                }
            }
            else
            {
                LOG_printf(LOG_DBG_COLLECTOR, "Collector_startFwUpdate: Error reading first 64 bytes of binary");
                status = Collector_status_invalid_file;
            }
        }
        fclose(oadFile);
    }
    else
    {
        LOG_printf( LOG_DBG_COLLECTOR, "Collector_startFwUpdate: could not open file: %s\n",
                        oad_file_list[oad_file_idx].oad_file);
        status = Collector_status_invalid_file;
    }

    return status;
}

/*!
 Find if a device is present.

 Public function defined in collector.h
 */
Collector_status_t Collector_findDevice(ApiMac_sAddr_t *pAddr)
{
    Collector_status_t status = Collector_status_deviceNotFound;

    if(findDevice(pAddr))
    {
        status = Collector_status_success;
    }

    return status;
}

/******************************************************************************
 Local Functions
 *****************************************************************************/

/*!
 * @brief       Initialize the clocks.
 */
static void initializeClocks(void)
{
    /* Initialize the tracking clock */
    Csf_initializeTrackingClock();
    Csf_initializeConfigClock();
    Csf_initializeBroadcastClock();
    Csf_initializeIdentifyClock();

}

/*!
 * @brief      CLLC Started callback.
 *
 * @param      pStartedInfo - pointer to network information
 */
static void cllcStartedCB(Llc_netInfo_t *pStartedInfo)
{
    devicePanId = pStartedInfo->devInfo.panID;
    if(pStartedInfo->fh == true)
    {
        fhEnabled = true;
    }

    /* updated the user */
    Csf_networkUpdate(restarted, pStartedInfo);

    /* Start the tracking clock */
    Csf_setTrackingClock(TRACKING_DELAY_TIME);
}

/*!
 * @brief      Device Joining callback from the CLLC module (ref.
 *             Cllc_deviceJoiningFp_t in cllc.h).  This function basically
 *             gives permission that the device can join with the return
 *             value.
 *
 * @param      pDevInfo - device information
 * @param      capInfo - device's capability information
 *
 * @return     ApiMac_assocStatus_t
 */
static ApiMac_assocStatus_t cllcDeviceJoiningCB(
                ApiMac_deviceDescriptor_t *pDevInfo,
                ApiMac_capabilityInfo_t *pCapInfo)
{
    ApiMac_assocStatus_t status;

    /* Make sure the device is in our PAN */
    if(pDevInfo->panID == devicePanId)
    {
        /* Update the user that a device is joining */
        status = Csf_deviceUpdate(pDevInfo, pCapInfo);

        if(status==ApiMac_assocStatus_success)
        {
#ifdef FEATURE_MAC_SECURITY
            /* Add device to security device table */
            Cllc_addSecDevice(pDevInfo->panID,
                            pDevInfo->shortAddress,
                            &pDevInfo->extAddress, 0);
#endif /* FEATURE_MAC_SECURITY */

            /* Set event for sending collector config packet */
            Util_setEvent(&Collector_events, COLLECTOR_CONFIG_EVT);
        }
    }
    else
    {
        status = ApiMac_assocStatus_panAccessDenied;
    }

    return (status);
}

/*!
 * @brief     CLLC State Changed callback.
 *
 * @param     state - CLLC new state
 */
static void cllcStateChangedCB(Cllc_states_t state)
{
    /* Save the state */
    cllcState = state;

    /* Notify the user interface */
    Csf_stateChangeUpdate(cllcState);
}

/*!
 * @brief      MAC Data Confirm callback.
 *
 * @param      pDataCnf - pointer to the data confirm information
 */
static void dataCnfCB(ApiMac_mcpsDataCnf_t *pDataCnf)
{
    /* Record statistics */
    if(pDataCnf->status == ApiMac_status_channelAccessFailure)
    {
        Collector_statistics.channelAccessFailures++;
    }
    else if(pDataCnf->status == ApiMac_status_noAck)
    {
        Collector_statistics.ackFailures++;
    }
    else if(pDataCnf->status == ApiMac_status_transactionExpired)
    {
        Collector_statistics.txTransactionExpired++;
    }
    else if(pDataCnf->status == ApiMac_status_transactionOverflow)
    {
        Collector_statistics.txTransactionOverflow++;
    }
    else if(pDataCnf->status == ApiMac_status_success)
    {
        Csf_updateFrameCounter(NULL, pDataCnf->frameCntr);
    }
    else if(pDataCnf->status != ApiMac_status_success)
    {
        Collector_statistics.otherTxFailures++;
    }

    /* Make sure the message came from the app */
    if(pDataCnf->msduHandle & APP_MARKER_MSDU_HANDLE)
    {
        /* What message type was the original request? */
        if(pDataCnf->msduHandle & APP_CONFIG_MSDU_HANDLE)
        {
            /* Config Request */
            Cllc_associated_devices_t *pDev;
            pDev = findDeviceStatusBit(ASSOC_CONFIG_MASK, ASSOC_CONFIG_SENT);
            if(pDev != NULL)
            {
                if(pDataCnf->status != ApiMac_status_success)
                {
                    /* Try to send again */
                    pDev->status &= ~ASSOC_CONFIG_SENT;
                    Csf_setConfigClock(CONFIG_DELAY);
                }
                else
                {
                    pDev->status |= ASSOC_CONFIG_SENT;
                    pDev->status |= ASSOC_CONFIG_RSP;
                    pDev->status |= CLLC_ASSOC_STATUS_ALIVE;
                    Csf_setConfigClock(CONFIG_RESPONSE_DELAY);
                }
            }

            /* Update stats */
            if(pDataCnf->status == ApiMac_status_success)
            {
                Collector_statistics.configReqRequestSent++;
            }
        }
        else if(pDataCnf->msduHandle & APP_BROADCAST_MSDU_HANDLE)
        {
            if(pDataCnf->status == ApiMac_status_success)
            {
                Collector_statistics.broadcastMsgSentCnt++;
            }
        }
        else
        {
            /* Tracking Request */
            Cllc_associated_devices_t *pDev;
            pDev = findDeviceStatusBit(ASSOC_TRACKING_SENT,
                                       ASSOC_TRACKING_SENT);
            if(pDev != NULL)
            {
                if(pDataCnf->status == ApiMac_status_success)
                {
                    /* Make sure the retry is clear */
                    pDev->status &= ~ASSOC_TRACKING_RETRY;
                }
                else
                {
                    if(pDev->status & ASSOC_TRACKING_RETRY)
                    {
                        /* We already tried to resend */
                        pDev->status &= ~ASSOC_TRACKING_RETRY;
                        pDev->status |= ASSOC_TRACKING_ERROR;
                    }
                    else
                    {
                        /* Go ahead and retry */
                        pDev->status |= ASSOC_TRACKING_RETRY;
                    }

                    pDev->status &= ~ASSOC_TRACKING_SENT;

                    /* Try to send again or another */
                    Csf_setTrackingClock(TRACKING_CNF_DELAY_TIME);
                }
            }

            /* Update stats */
            if(pDataCnf->status == ApiMac_status_success)
            {
                Collector_statistics.trackingReqRequestSent++;
            }
        }
    }
}

/*!
 * @brief      MAC Data Indication callback.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void dataIndCB(ApiMac_mcpsDataInd_t *pDataInd)
{
    int i;

    if((pDataInd != NULL) && (pDataInd->msdu.p != NULL)
       && (pDataInd->msdu.len > 0))
    {
        Smsgs_cmdIds_t cmdId = (Smsgs_cmdIds_t)*(pDataInd->msdu.p);

#ifdef FEATURE_MAC_SECURITY
        if(Cllc_securityCheck(&(pDataInd->sec)) == false)
        {
            /* Reject the message */
            return;
        }
#endif /* FEATURE_MAC_SECURITY */

        if(pDataInd->srcAddr.addrMode == ApiMac_addrType_extended)
        {
            uint16_t shortAddr = Csf_getDeviceShort(
                            &pDataInd->srcAddr.addr.extAddr);
            if(shortAddr != CSF_INVALID_SHORT_ADDR)
            {
                /* Switch to the short address for internal tracking */
                pDataInd->srcAddr.addrMode = ApiMac_addrType_short;
                pDataInd->srcAddr.addr.shortAddr = shortAddr;

                LOG_printf(LOG_DBG_COLLECTOR_RAW,"Sensor MSG Short: 0x%04x\
                    Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n Data Len: %d Data: ",
                    shortAddr,
                    pDataInd->srcAddr.addr.extAddr[7],
                    pDataInd->srcAddr.addr.extAddr[6],
                    pDataInd->srcAddr.addr.extAddr[5],
                    pDataInd->srcAddr.addr.extAddr[4],
                    pDataInd->srcAddr.addr.extAddr[3],
                    pDataInd->srcAddr.addr.extAddr[2],
                    pDataInd->srcAddr.addr.extAddr[1],
                    pDataInd->srcAddr.addr.extAddr[0], pDataInd->msdu.len);

                for (i = 0; i < pDataInd->msdu.len; i++)
                {
                  LOG_printf(LOG_DBG_COLLECTOR_RAW, "%02X ", pDataInd->msdu.p[i]);
                }
                LOG_printf(LOG_DBG_COLLECTOR_RAW, "\n");
            }
            else
            {
                /* Can't accept the message - ignore it */
                return;
            }
        }
        /* Log using short address */
        else {
            LOG_printf(LOG_DBG_COLLECTOR_RAW,"Sensor MSG Short: 0x%04x Data Len: %d Data: ",
             pDataInd->srcAddr.addr.shortAddr, pDataInd->msdu.len);

            for (i = 0; i < pDataInd->msdu.len; i++)
            {
              LOG_printf(LOG_DBG_COLLECTOR_RAW, "%02X ", pDataInd->msdu.p[i]);
            }
            LOG_printf(LOG_DBG_COLLECTOR_RAW, "\n");
        }

        switch(cmdId)
        {
            case Smsgs_cmdIds_configRsp:
                Collector_statistics.configResponseReceived++;
                Cllc_associated_devices_t *pDev = findDevice(&pDataInd->srcAddr);
                if (pDev != NULL)
                {
                    /* Clear the sent flag and set the response flag */
                    pDev->status &= ~ASSOC_CONFIG_SENT;
                    pDev->status |= ASSOC_CONFIG_RSP;
                }
                Csf_deviceConfigDisplay(&pDataInd->srcAddr);
                Util_setEvent(&Collector_events, COLLECTOR_CONFIG_EVT);
                Csf_deviceRawDataUpdate(pDataInd);
                break;

            case Smsgs_cmdIds_trackingRsp:
                processTrackingResponse(pDataInd);
                break;

            case Smsgs_cmdIds_IdentifyLedReq:
                processIdentifyLedRequest(pDataInd);
                break;

            case Smsgs_cmdIds_toggleLedRsp:
                processToggleLedResponse(pDataInd);
                break;

            case Smsgs_cmdIds_sensorData:
                Collector_statistics.sensorMessagesReceived++;
				Csf_deviceSensorDisplay(pDataInd);
                processDataRetry(&(pDataInd->srcAddr));
                Csf_deviceRawDataUpdate(pDataInd);
                break;

            case Smsgs_cmdIds_rampdata:
                Collector_statistics.sensorMessagesReceived++;
                break;

            case Smsgs_cmdIds_DeviceTypeRsp:
                processDeviceTypeResponse(pDataInd);
                break;

            case Smsgs_cmdIds_oad:
                processOadData(pDataInd);
                break;

            default:
                /* Should not receive other messages */
                Csf_deviceRawDataUpdate(pDataInd);
                break;
        }
    }
}

/*!
 * @brief      Process the start event
 */
static void processStartEvent(void)
{
    Llc_netInfo_t netInfo;
    uint32_t frameCounter = 0;

    Csf_getFrameCounter(NULL, &frameCounter);
    /* See if there is existing network information */
    if(Csf_getNetworkInformation(&netInfo))
    {
        uint16_t numDevices = 0;

#ifdef FEATURE_MAC_SECURITY
        /* Initialize the MAC Security */
        Cllc_securityInit(frameCounter, NULL);
#endif /* FEATURE_MAC_SECURITY */

        numDevices = Csf_getNumDeviceListEntries();

        /* Restore with the network and device information */
        Cllc_restoreNetwork(&netInfo, (uint16_t)numDevices, NULL);

        restarted = true;
    }
    else
    {
        restarted = false;

#ifdef FEATURE_MAC_SECURITY
        /* Initialize the MAC Security */
        Cllc_securityInit(frameCounter, NULL);
#endif /* FEATURE_MAC_SECURITY */

        /* Start a new netork */
        Cllc_startNetwork();
    }
}

#ifndef PROCESS_JS
/*!
 * @brief      Process the Config Response message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processConfigResponse(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* Make sure the message is the correct size */
    if(pDataInd->msdu.len == SMSGS_CONFIG_RESPONSE_MSG_LENGTH)
    {
        Cllc_associated_devices_t *pDev;
        Smsgs_configRspMsg_t configRsp;
        uint8_t *pBuf = pDataInd->msdu.p;

        /* Parse the message */
        configRsp.cmdId = (Smsgs_cmdIds_t)*pBuf++;

        configRsp.status = (Smsgs_statusValues_t)Util_buildUint16(pBuf[0],
                                                                  pBuf[1]);
        pBuf += 2;

        configRsp.frameControl = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;

        configRsp.reportingInterval = Util_buildUint32(pBuf[0], pBuf[1],
                                                       pBuf[2],
                                                       pBuf[3]);
        pBuf += 4;

        configRsp.pollingInterval = Util_buildUint32(pBuf[0], pBuf[1], pBuf[2],
                                                     pBuf[3]);

        pDev = findDevice(&pDataInd->srcAddr);
        if(pDev != NULL)
        {
            /* Clear the sent flag and set the response flag */
            pDev->status &= ~ASSOC_CONFIG_SENT;
            pDev->status |= ASSOC_CONFIG_RSP;
        }

        /* Report the config response */
        Csf_deviceConfigUpdate(&pDataInd->srcAddr, pDataInd->rssi,
                               &configRsp);

        Util_setEvent(&Collector_events, COLLECTOR_CONFIG_EVT);

        Collector_statistics.configResponseReceived++;
    }
}

#endif

/*!
 * @brief      Process the Tracking Response message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processTrackingResponse(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* Make sure the message is the correct size */
    if(pDataInd->msdu.len == SMSGS_TRACKING_RESPONSE_MSG_LENGTH)
    {
        Cllc_associated_devices_t *pDev;

        pDev = findDevice(&pDataInd->srcAddr);
        if(pDev != NULL)
        {
            if(pDev->status & ASSOC_TRACKING_SENT)
            {
                pDev->status &= ~ASSOC_TRACKING_SENT;
                pDev->status |= ASSOC_TRACKING_RSP;

                /* Setup for next tracking */
                Csf_setTrackingClock( TRACKING_DELAY_TIME);

                /* Retry config request */
                processConfigRetry();
            }
        }

        /* Update stats */
        Collector_statistics.trackingResponseReceived++;
    }
}

/*!
 * @brief      Process the Identify Led Request message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processIdentifyLedRequest(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* Make sure the message is the correct size */
    if(pDataInd->msdu.len == SMSGS_INDENTIFY_LED_REQUEST_MSG_LEN)
    {
        Llc_deviceListItem_t item;

        /* Is the device a known device? */
        if(Csf_getDevice(&(pDataInd->srcAddr), &item))
        {
            uint8_t cmdBytes[SMSGS_INDENTIFY_LED_RESPONSE_MSG_LEN];
            Csf_identifyLED((pDataInd->msdu.p[1]), item.devInfo.shortAddress);

            /* send the response message directly */
            cmdBytes[0] = (uint8_t) Smsgs_cmdIds_IdentifyLedRsp;
            cmdBytes[1] = 0;
            sendMsg(Smsgs_cmdIds_IdentifyLedRsp,
                    item.devInfo.shortAddress,
                    item.capInfo.rxOnWhenIdle,
                    SMSGS_INDENTIFY_LED_RESPONSE_MSG_LEN,
                    cmdBytes);
        }
    }
}

/*!
 * @brief      Process the Toggle Led Response message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processToggleLedResponse(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* Make sure the message is the correct size */
    if(pDataInd->msdu.len == SMSGS_TOGGLE_LED_RESPONSE_MSG_LEN)
    {
        bool ledState;
        uint8_t *pBuf = pDataInd->msdu.p;

        /* Skip past the command ID */
        pBuf++;

        ledState = (bool)*pBuf;

        /* Notify the user */
        Csf_toggleResponseReceived(&pDataInd->srcAddr, ledState);
    }
}

/*!
 * @brief      Process the device type response message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processDeviceTypeResponse(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* Make sure the message is the correct size */
    if(pDataInd->msdu.len == SMSGS_DEVICE_TYPE_RESPONSE_MSG_LEN)
    {
        uint8_t *pBuf = pDataInd->msdu.p;

        /* Command format
         * pBuf[0] = Command ID
         * pBuf[1] = DeviceFamily_ID
         * pBuf[2] = DeviceType_ID
         */
        uint8_t deviceFamilyID = pBuf[1];
        uint8_t deviceTypeID = pBuf[2];

        /* Notify the user */
        Csf_deviceSensorDeviceTypeResponseUpdate(&pDataInd->srcAddr, deviceFamilyID,
                                                 deviceTypeID);
    }
}

#ifndef PROCESS_JS
/*!
 * @brief      Process the Sensor Data message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processSensorData(ApiMac_mcpsDataInd_t *pDataInd)
{
    Smsgs_sensorMsg_t sensorData;
    uint8_t *pBuf = pDataInd->msdu.p;

    memset(&sensorData, 0, sizeof(Smsgs_sensorMsg_t));

    /* Parse the message */
    sensorData.cmdId = (Smsgs_cmdIds_t)*pBuf++;

    memcpy(sensorData.extAddress, pBuf, SMGS_SENSOR_EXTADDR_LEN);
    pBuf += SMGS_SENSOR_EXTADDR_LEN;

    sensorData.frameControl = Util_buildUint16(pBuf[0], pBuf[1]);
    pBuf += 2;

    /* Parse data in order of frameControl mask, starting with LSB */
    if(sensorData.frameControl & Smsgs_dataFields_tempSensor)
    {
        sensorData.tempSensor.ambienceTemp = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.tempSensor.objectTemp = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
    }

    if(sensorData.frameControl & Smsgs_dataFields_lightSensor)
    {
        sensorData.lightSensor.rawData = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
    }

    if(sensorData.frameControl & Smsgs_dataFields_humiditySensor)
    {
        sensorData.humiditySensor.temp = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.humiditySensor.humidity = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
    }

    if(sensorData.frameControl & Smsgs_dataFields_msgStats)
    {
        sensorData.msgStats.joinAttempts = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.joinFails = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.msgsAttempted = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.msgsSent = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.trackingRequests = Util_buildUint16(pBuf[0],
                                                                pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.trackingResponseAttempts = Util_buildUint16(
                        pBuf[0],
                        pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.trackingResponseSent = Util_buildUint16(pBuf[0],
                                                                    pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.configRequests = Util_buildUint16(pBuf[0],
                                                              pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.configResponseAttempts = Util_buildUint16(
                        pBuf[0],
                        pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.configResponseSent = Util_buildUint16(pBuf[0],
                                                                  pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.channelAccessFailures = Util_buildUint16(pBuf[0],
                                                                     pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.macAckFailures = Util_buildUint16(pBuf[0], pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.otherDataRequestFailures = Util_buildUint16(
                        pBuf[0],
                        pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.syncLossIndications = Util_buildUint16(pBuf[0],
                                                                   pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.rxDecryptFailures = Util_buildUint16(pBuf[0],
                                                                 pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.txEncryptFailures = Util_buildUint16(pBuf[0],
                                                                 pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.resetCount = Util_buildUint16(pBuf[0],
                                                          pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.lastResetReason = Util_buildUint16(pBuf[0],
                                                               pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.joinTime = Util_buildUint16(pBuf[0],
                                                        pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.interimDelay = Util_buildUint16(pBuf[0],
                                                            pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.numBroadcastMsgRcvd = Util_buildUint16(pBuf[0],
                                                                   pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.numBroadcastMsglost = Util_buildUint16(pBuf[0],
                                                                   pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.avgE2EDelay = Util_buildUint16(pBuf[0],pBuf[1]);
        pBuf += 2;
        sensorData.msgStats.worstCaseE2EDelay = Util_buildUint16(pBuf[0],pBuf[1]);
        pBuf += 2;
    }

    if(sensorData.frameControl & Smsgs_dataFields_configSettings)
    {
        sensorData.configSettings.reportingInterval = Util_buildUint32(pBuf[0],
                                                                       pBuf[1],
                                                                       pBuf[2],
                                                                       pBuf[3]);
        pBuf += 4;
        sensorData.configSettings.pollingInterval = Util_buildUint32(pBuf[0],
                                                                     pBuf[1],
                                                                     pBuf[2],
                                                                     pBuf[3]);
#ifdef LPSTK
        pBuf += 4;
    }

    if(sensorData.frameControl & Smsgs_dataFields_hallEffectSensor)
    {
        sensorData.hallEffectSensor.flux = (float) Util_buildUint32(pBuf[0],
                                                                    pBuf[1],
                                                                    pBuf[2],
                                                                    pBuf[3]);
        pBuf += 4;
    }

    if(sensorData.frameControl & Smsgs_dataFields_accelSensor)
    {
        sensorData.accelerometerSensor.xAxis = (int16_t)Util_buildUint16(pBuf[0],
                                                                pBuf[1]);
        pBuf += 2;
        sensorData.accelerometerSensor.yAxis = (int16_t)Util_buildUint16(pBuf[0],
                                                                pBuf[1]);
        pBuf += 2;
        sensorData.accelerometerSensor.zAxis = (int16_t)Util_buildUint16(pBuf[0],
                                                                pBuf[1]);
        pBuf += 2;
        sensorData.accelerometerSensor.xTiltDet = *pBuf++;
        sensorData.accelerometerSensor.yTiltDet = *pBuf++;

#endif /* LPSTK */
    }

    Collector_statistics.sensorMessagesReceived++;

    /* Report the sensor data */
    Csf_deviceSensorDataUpdate(&pDataInd->srcAddr, pDataInd->rssi,
                               &sensorData);

    processDataRetry(&(pDataInd->srcAddr));
}
#endif


/*!
 * @brief      Process the OAD Data message.
 *
 * @param      pDataInd - pointer to the data indication information
 */
static void processOadData(ApiMac_mcpsDataInd_t *pDataInd)
{
    //Index past the Smsgs_cmdId
    OADProtocol_ParseIncoming((void*) &(pDataInd->srcAddr), &(pDataInd->msdu.p[1]));

    Collector_statistics.sensorMessagesReceived++;
}

/*!
 * @brief      Find the associated device table entry matching pAddr.
 *
 * @param      pAddr - pointer to device's address
 *
 * @return     pointer to the associated device table entry,
 *             NULL if not found.
 */
static Cllc_associated_devices_t *findDevice(ApiMac_sAddr_t *pAddr)
{
    int x;
    Cllc_associated_devices_t *pItem = NULL;

    /* Check for invalid parameters */
    if((pAddr == NULL) || (pAddr->addrMode == ApiMac_addrType_none))
    {
        return (NULL);
    }

    for(x = 0; x < CONFIG_MAX_DEVICES; x++)
    {
        /* Make sure the entry is valid. */
        if(Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
        {
            if(pAddr->addrMode == ApiMac_addrType_short)
            {
                if(pAddr->addr.shortAddr == Cllc_associatedDevList[x].shortAddr)
                {
                    pItem = &Cllc_associatedDevList[x];
                    break;
                }
            }
        }
    }

    return (pItem);
}

/*!
 * @brief      Find the associated device table entry matching status bit.
 *
 * @param      statusBit - what status bit to find
 *
 * @return     pointer to the associated device table entry,
 *             NULL if not found.
 */
static Cllc_associated_devices_t *findDeviceStatusBit(uint16_t mask, uint16_t statusBit)
{
    int x;
    Cllc_associated_devices_t *pItem = NULL;

    for(x = 0; x < CONFIG_MAX_DEVICES; x++)
    {
        /* Make sure the entry is valid. */
        if(Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
        {
            if((Cllc_associatedDevList[x].status & mask) == statusBit)
            {
                pItem = &Cllc_associatedDevList[x];
                break;
            }
        }
    }

    return (pItem);
}

/*!
 * @brief      Get the next MSDU Handle
 *             <BR>
 *             The MSDU handle has 3 parts:<BR>
 *             - The MSBit(7), when set means the the application sent the message
 *             - Bit 6, when set means that the app message is a config request
 *             - Bits 0-5, used as a message counter that rolls over.
 *
 * @param      msgType - message command id needed
 *
 * @return     msdu Handle
 */
static uint8_t getMsduHandle(Smsgs_cmdIds_t msgType)
{
    uint8_t msduHandle = deviceTxMsduHandle;

    /* Increment for the next msdu handle, or roll over */
    if(deviceTxMsduHandle >= MSDU_HANDLE_MAX)
    {
        deviceTxMsduHandle = 0;
    }
    else
    {
        deviceTxMsduHandle++;
    }

    /* Add the message type bit for ramp data */
    if(msgType == Smsgs_cmdIds_rampdata)
    {
        msduHandle |= RAMP_DATA_MSDU_HANDLE;
        return (msduHandle);
    }

    /* Add the App specific bit */
    msduHandle |= APP_MARKER_MSDU_HANDLE;

    /* Add the message type bit */
    if(msgType == Smsgs_cmdIds_configReq)
    {
        msduHandle |= APP_CONFIG_MSDU_HANDLE;
    }
    else if(msgType == Smgs_cmdIds_broadcastCtrlMsg)
    {
        msduHandle |= APP_BROADCAST_MSDU_HANDLE;
    }

    return (msduHandle);
}

/*!
 * @brief      Send MAC data request
 *
 * @param      type - message type
 * @param      dstShortAddr - destination short address
 * @param      rxOnIdle - true if not a sleepy device
 * @param      len - length of payload
 * @param      pData - pointer to the buffer
 *
 * @return  true if sent, false if not
 */
static bool sendMsg(Smsgs_cmdIds_t type, uint16_t dstShortAddr, bool rxOnIdle,
                    uint16_t len,
                    uint8_t *pData)
{
    ApiMac_mcpsDataReq_t dataReq;

    /* Fill the data request field */
    memset(&dataReq, 0, sizeof(ApiMac_mcpsDataReq_t));

    dataReq.dstAddr.addrMode = ApiMac_addrType_short;
    dataReq.dstAddr.addr.shortAddr = dstShortAddr;
    dataReq.srcAddrMode = ApiMac_addrType_short;

    if(fhEnabled && rxOnIdle)
    {
        Llc_deviceListItem_t item;

        if(Csf_getDevice(&(dataReq.dstAddr), &item))
        {
            /* Switch to the long address */
            dataReq.dstAddr.addrMode = ApiMac_addrType_extended;
            memcpy(&dataReq.dstAddr.addr.extAddr, &item.devInfo.extAddress,
                   (APIMAC_SADDR_EXT_LEN));
            dataReq.srcAddrMode = ApiMac_addrType_extended;
        }
        else
        {
            /* Can't send the message */
            return (false);
        }
    }

    dataReq.dstPanId = devicePanId;

    dataReq.msduHandle = getMsduHandle(type);

    dataReq.txOptions.ack = true;
    if(rxOnIdle == false)
    {
        dataReq.txOptions.indirect = true;
    }

    dataReq.msdu.len = len;
    dataReq.msdu.p = pData;

#ifdef FEATURE_MAC_SECURITY
    /* Fill in the appropriate security fields */
    Cllc_securityFill(&dataReq.sec);
#endif /* FEATURE_MAC_SECURITY */

    /* Send the message */
    if(ApiMac_mcpsDataReq(&dataReq) != ApiMac_status_success)
    {
        /*  Transaction overflow occurred */
        return (false);
    }
    else
    {
        return (true);
    }
}

/*!
 * @brief      Send MAC broadcast data request.
 *             This function can be used to send broadcast messages
 *             in all modes for non sleepy devices only.
 *
 * @param      type - message type
 * @param      len - length of payload
 * @param      pData - pointer to the buffer
 */
static void sendBroadcastMsg(Smsgs_cmdIds_t type, uint16_t len,
                    uint8_t *pData)
{
    ApiMac_mcpsDataReq_t dataReq;

    /* Current example is only implemented for FH mode */
    if(!fhEnabled)
    {
        return;
    }
    /* Fill the data request field */
    memset(&dataReq, 0, sizeof(ApiMac_mcpsDataReq_t));

    dataReq.dstAddr.addrMode = ApiMac_addrType_none;
    dataReq.srcAddrMode = ApiMac_addrType_short;

    dataReq.dstPanId = devicePanId;

    dataReq.msduHandle = getMsduHandle(type);

    dataReq.txOptions.ack = false;
    dataReq.txOptions.indirect = false;


    dataReq.msdu.len = len;
    dataReq.msdu.p = pData;

#ifdef FEATURE_MAC_SECURITY
    /* Fill in the appropriate security fields */
    Cllc_securityFill(&dataReq.sec);
#endif /* FEATURE_MAC_SECURITY */

    /* Send the message */
    ApiMac_mcpsDataReq(&dataReq);
}

/*!
 * @brief      Generate Config Requests for all associate devices
 *             that need one.
 */
static void generateConfigRequests(void)
{
    int x;

    if(CERTIFICATION_TEST_MODE)
    {
        /* In Certification mode only back to back uplink
         * data traffic shall be supported*/
        return;
    }

    /* Clear any timed out transactions */
    for(x = 0; x < CONFIG_MAX_DEVICES; x++)
    {
        if((Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
           && (Cllc_associatedDevList[x].status & CLLC_ASSOC_STATUS_ALIVE))
        {
            if((Cllc_associatedDevList[x].status &
               (ASSOC_CONFIG_SENT | ASSOC_CONFIG_RSP))
               == (ASSOC_CONFIG_SENT | ASSOC_CONFIG_RSP))
            {
                Cllc_associatedDevList[x].status &= ~(ASSOC_CONFIG_SENT
                                | ASSOC_CONFIG_RSP);
            }
        }
    }

    /* Make sure we are only sending one config request at a time */
    if(findDeviceStatusBit(ASSOC_CONFIG_MASK, ASSOC_CONFIG_SENT) == NULL)
    {
        /* Run through all of the devices */
        for(x = 0; x < CONFIG_MAX_DEVICES; x++)
        {
            /* Make sure the entry is valid. */
            if((Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
               && (Cllc_associatedDevList[x].status & CLLC_ASSOC_STATUS_ALIVE))
            {
                uint16_t status = Cllc_associatedDevList[x].status;

                /*
                 Has the device been sent or already received a config request?
                 */
                if(((status & (ASSOC_CONFIG_SENT | ASSOC_CONFIG_RSP)) == 0))
                {
                    ApiMac_sAddr_t dstAddr;
                    Collector_status_t stat;

                    /* Set up the destination address */
                    dstAddr.addrMode = ApiMac_addrType_short;
                    dstAddr.addr.shortAddr =
                        Cllc_associatedDevList[x].shortAddr;

                    /* Send the Config Request */
                    stat = Collector_sendConfigRequest(
                                    &dstAddr, (CONFIG_FRAME_CONTROL),
                                    (CONFIG_REPORTING_INTERVAL),
                                    (CONFIG_POLLING_INTERVAL));
                    if(stat == Collector_status_success)
                    {
                        /*
                         Mark as the message has been sent and expecting a response
                         */
                        Cllc_associatedDevList[x].status |= ASSOC_CONFIG_SENT;
                        Cllc_associatedDevList[x].status &= ~ASSOC_CONFIG_RSP;
                    }

                    /* Only do one at a time */
                    break;
                }
            }
        }
    }
}


/*!
 * @brief      Generate Tracking Requests for all associate devices
 *             that need one.
 */
static void generateTrackingRequests(void)
{
    int x;

    /* Run through all of the devices, looking for previous activity */
    for(x = 0; x < CONFIG_MAX_DEVICES; x++)
    {
        if(CERTIFICATION_TEST_MODE)
        {
            /* In Certification mode only back to back uplink
             * data traffic shall be supported*/
            return;
        }
        /* Make sure the entry is valid. */
        if((Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
             && (Cllc_associatedDevList[x].status & CLLC_ASSOC_STATUS_ALIVE))
        {
            uint16_t status = Cllc_associatedDevList[x].status;

            /*
             Has the device been sent a tracking request or received a
             tracking response?
             */
            if(status & ASSOC_TRACKING_RETRY)
            {
                sendTrackingRequest(&Cllc_associatedDevList[x]);
                return;
            }
            else if((status & (ASSOC_TRACKING_SENT | ASSOC_TRACKING_RSP
                               | ASSOC_TRACKING_ERROR)))
            {
                Cllc_associated_devices_t *pDev = NULL;
                int y;

                if(status & (ASSOC_TRACKING_SENT | ASSOC_TRACKING_ERROR))
                {
                    ApiMac_deviceDescriptor_t devInfo;
                    Llc_deviceListItem_t item;
                    ApiMac_sAddr_t devAddr;

                    /*
                     Timeout occured, notify the user that the tracking
                     failed.
                     */
                    memset(&devInfo, 0, sizeof(ApiMac_deviceDescriptor_t));

                    devAddr.addrMode = ApiMac_addrType_short;
                    devAddr.addr.shortAddr =
                        Cllc_associatedDevList[x].shortAddr;

                    if(Csf_getDevice(&devAddr, &item))
                    {
                        memcpy(&devInfo.extAddress,
                               &item.devInfo.extAddress,
                               sizeof(ApiMac_sAddrExt_t));
                    }
                    devInfo.shortAddress = Cllc_associatedDevList[x].shortAddr;
                    devInfo.panID = devicePanId;
                    Csf_deviceNotActiveUpdate(&devInfo,
                        ((status & ASSOC_TRACKING_SENT) ? true : false));

                    /* Not responding, so remove the alive marker */
                    Cllc_associatedDevList[x].status
                            &= ~(CLLC_ASSOC_STATUS_ALIVE
                                | ASSOC_CONFIG_SENT | ASSOC_CONFIG_RSP);
                }

                /* Clear the tracking bits */
                Cllc_associatedDevList[x].status  &= ~(ASSOC_TRACKING_ERROR
                                | ASSOC_TRACKING_SENT | ASSOC_TRACKING_RSP);

                /* Find the next valid device */
                y = x;
                while(pDev == NULL)
                {
                    /* Get the very first active and alive device */
                    if((Cllc_associatedDevList[y].shortAddr != CSF_INVALID_SHORT_ADDR)
                            && (Cllc_associatedDevList[y].status
                                    & CLLC_ASSOC_STATUS_ALIVE))
                    {
                        pDev = &Cllc_associatedDevList[y];
                    }

                    /* Check for rollover */
                    if(y == (CONFIG_MAX_DEVICES-1))
                    {
                        /* Move to the beginning */
                        y = 0;
                    }
                    else
                    {
                        /* Move the the next device */
                        y++;
                    }

                    if(y == x)
                    {
                        /* We've come back around */
                        break;
                    }
                }

                /* Make sure a sensor actually exists before sending */
                if(pDev != NULL)
                {
                    /* Only send the tracking request if you are in the commissioned state for SM only
                    * this is handled inside of the sendTrackingRequest function */
                    sendTrackingRequest(pDev);
                }

                /* Only do one at a time */
                return;
            }
        }
    }

    /* If no activity found, find the first active device */
    for(x = 0; x < CONFIG_MAX_DEVICES; x++)
    {
        /* Make sure the entry is valid. */
        if((Cllc_associatedDevList[x].shortAddr != CSF_INVALID_SHORT_ADDR)
              && (Cllc_associatedDevList[x].status & CLLC_ASSOC_STATUS_ALIVE))
        {
            sendTrackingRequest(&Cllc_associatedDevList[x]);
            break;
        }
    }

    if(x == CONFIG_MAX_DEVICES)
    {
        /* No device found, Setup delay for next tracking message */
        Csf_setTrackingClock(TRACKING_DELAY_TIME);
    }
}

/*!
 * @brief      Generate Broadcast Cmd Request Message
 */
static void generateBroadcastCmd(void)
{
    uint8_t buffer[SMSGS_BROADCAST_CMD_LENGTH];
    uint8_t *pBuf = buffer;

    /* Build the message */
    *pBuf++ = (uint8_t)Smgs_cmdIds_broadcastCtrlMsg;
    *pBuf++ = Util_loUint16(Collector_statistics.broadcastMsgSentCnt);
    *pBuf++ = Util_hiUint16(Collector_statistics.broadcastMsgSentCnt);

    sendBroadcastMsg(Smgs_cmdIds_broadcastCtrlMsg, SMSGS_BROADCAST_CMD_LENGTH,
                     buffer);
}

/*!
 * @brief      Generate Tracking Requests for a device
 *
 * @param      pDev - pointer to the device's associate device table entry
 */
static void sendTrackingRequest(Cllc_associated_devices_t *pDev)
{
    uint8_t cmdId = Smsgs_cmdIds_trackingReq;

    /* Send the Tracking Request */
   if((sendMsg(Smsgs_cmdIds_trackingReq, pDev->shortAddr,
            pDev->capInfo.rxOnWhenIdle,
            (SMSGS_TRACKING_REQUEST_MSG_LENGTH),
            &cmdId)) == true)
    {
        /* Mark as Tracking Request sent */
        pDev->status |= ASSOC_TRACKING_SENT;

        /* Setup Timeout for response */
        Csf_setTrackingClock(TRACKING_TIMEOUT_TIME);

        /* Update stats */
        Collector_statistics.trackingRequestAttempts++;
    }
    else
    {
        ApiMac_sAddr_t devAddr;
        devAddr.addrMode = ApiMac_addrType_short;
        devAddr.addr.shortAddr = pDev->shortAddr;
        processDataRetry(&devAddr);
    }
}

/*!
 * @brief      Process the MAC Comm Status Indication Callback
 *
 * @param      pCommStatusInd - Comm Status indication
 */
static void commStatusIndCB(ApiMac_mlmeCommStatusInd_t *pCommStatusInd)
{
    if(pCommStatusInd->reason == ApiMac_commStatusReason_assocRsp)
    {
        if(pCommStatusInd->status != ApiMac_status_success)
        {
            Cllc_associated_devices_t *pDev;

            pDev = findDevice(&pCommStatusInd->dstAddr);
            if(pDev)
            {
                /* Mark as inactive and clear config and tracking states */
                pDev->status = 0;
            }
        }
    }
}

/*!
 * @brief      Process the MAC Poll Indication Callback
 *
 * @param      pPollInd - poll indication
 */
static void pollIndCB(ApiMac_mlmePollInd_t *pPollInd)
{
    ApiMac_sAddr_t addr;

    addr.addrMode = ApiMac_addrType_short;
    if (pPollInd->srcAddr.addrMode == ApiMac_addrType_short)
    {
        addr.addr.shortAddr = pPollInd->srcAddr.addr.shortAddr;
    }
    else
    {
        addr.addr.shortAddr = Csf_getDeviceShort(
                        &pPollInd->srcAddr.addr.extAddr);
    }

    processDataRetry(&addr);
}

/*!
 * @brief      Process the disassoc Indication Callback
 *
 * @param      disassocIndCB - disassoc indication
 */
static void disassocIndCB(ApiMac_mlmeDisassociateInd_t *pDisassocInd)
{
    ApiMac_sAddr_t addr;

    addr.addrMode = ApiMac_addrType_extended;
    memcpy(&addr.addr.extAddr, &pDisassocInd->deviceAddress,
                   (APIMAC_SADDR_EXT_LEN));

    Csf_deviceDisassocUpdate(&addr);
}

/*!
 * @brief      Process the disassoc cofirmation Callback
 *
 * @param      disassocCnfCB - disassoc cofirmation
 */
static void disassocCnfCB(ApiMac_mlmeDisassociateCnf_t *pDisassocCnf)
{
    Csf_deviceDisassocUpdate(&pDisassocCnf->deviceAddress);
}

/*!
 * @brief      Process retries for config and tracking messages
 *
 * @param      addr - MAC address structure */
static void processDataRetry(ApiMac_sAddr_t *pAddr)
{
    if(pAddr->addr.shortAddr != CSF_INVALID_SHORT_ADDR)
    {
        Cllc_associated_devices_t *pItem;
        pItem = findDevice(pAddr);
        if(pItem)
        {
            /* Set device status to alive */
            pItem->status |= CLLC_ASSOC_STATUS_ALIVE;

            /* Check to see if we need to send it a config */
            if((pItem->status & (ASSOC_CONFIG_RSP | ASSOC_CONFIG_SENT)) == 0)
            {
                processConfigRetry();
            }
            /* Check to see if we need to send it a tracking message */
            if((pItem->status & (ASSOC_TRACKING_SENT| ASSOC_TRACKING_RETRY)) == 0)
            {
                /* Make sure we aren't already doing a tracking message */
                if(((Collector_events & COLLECTOR_TRACKING_TIMEOUT_EVT) == 0)
                    && (Csf_isTrackingTimerActive() == false)
                    && (findDeviceStatusBit(ASSOC_TRACKING_MASK,
                                            ASSOC_TRACKING_SENT) == NULL))
                {
                    /* Setup for next tracking */
                    Csf_setTrackingClock(TRACKING_DELAY_TIME);
                }
            }
        }
    }
}

/*!
 * @brief      Process retries for config messages
 */
static void processConfigRetry(void)
{
    /* Retry config request if not already sent */
    if(((Collector_events & COLLECTOR_CONFIG_EVT) == 0)
        && (Csf_isConfigTimerActive() == false))
    {
        /* Set config event */
        Csf_setConfigClock(CONFIG_DELAY);
    }
}

/*!
 * @brief      Process FW version response
 */
static void oadFwVersionRspCb(void* pSrcAddr, char *fwVersionStr)
{
    LOG_printf( LOG_DBG_COLLECTOR, "oadFwVersionRspCb from %x\n", ((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr);
    Csf_deviceSensorFwVerUpdate(((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr, fwVersionStr);
}

/*!
 * @brief      Process OAD image identify response
 */
static void oadImgIdentifyRspCb(void* pSrcAddr, uint8_t status)
{

}

static void oadBlockReqCb(void* pSrcAddr, uint8_t imgId, uint16_t blockNum, uint16_t multiBlockSize)
{
    uint8_t blockBuf[OAD_BLOCK_SIZE] = {0};
    int byteRead = 0;
    uint32_t oad_file_idx;
    FILE *oadFile = NULL;

    LOG_printf( LOG_DBG_COLLECTOR, "oadBlockReqCb[%d:%x] from %x\n", imgId, blockNum, ((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr);

    Csf_deviceSensorOadUpdate( ((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr, imgId, blockNum, oadBNumBlocks);

    for(oad_file_idx = 0; oad_file_idx < MAX_OAD_FILES; oad_file_idx++)
    {
        if(oad_file_list[oad_file_idx].oad_file_id == imgId)
        {
            LOG_printf( LOG_DBG_COLLECTOR, "oadBlockReqCb: openinging %d:%d:%s\n", oad_file_idx,
                                    oad_file_list[oad_file_idx].oad_file_id,
                                    oad_file_list[oad_file_idx].oad_file);

            oadFile = fopen(oad_file_list[oad_file_idx].oad_file, "r");

            break;
        }
    }

    if(oadFile != NULL)
    {
        fseek(oadFile, (blockNum * OAD_BLOCK_SIZE), SEEK_SET);
        byteRead = (int) fread(blockBuf, 1, OAD_BLOCK_SIZE, oadFile);

        LOG_printf( LOG_DBG_COLLECTOR, "oadBlockReqCb: read %d bytes from position %d of %p\n",
                                                    byteRead, (blockNum * OAD_BLOCK_SIZE), oadFile);

        if(byteRead == 0)
        {
            LOG_printf( LOG_ERROR, "oadBlockReqCb: Read 0 Bytes");
        }

        fclose(oadFile);

        OADProtocol_sendOadImgBlockRsp(pSrcAddr, imgId, blockNum, blockBuf);
    }
    else
    {
      LOG_printf( LOG_DBG_COLLECTOR, "imgId %d file not found\n", imgId);
    }
}

/*!
 * @brief      Process OAD Reset response
 */
static void oadResetRspCb(void* pSrcAddr)
{
    LOG_printf( LOG_DBG_COLLECTOR, "oadResetRspCb from %x\n", ((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr);
    Csf_deviceSensorOadResetRspRcvd(((ApiMac_sAddr_t*)pSrcAddr)->addr.shortAddr);
}


/*!
 * @brief      Radio access function for OAD module to send messages
 */
void* oadRadioAccessAllocMsg(uint32_t msgLen)
{
    uint8_t *msgBuffer;

    /* allocate buffer for CmdId + message */
    msgBuffer = malloc(msgLen + 1);

    return msgBuffer + 1;
}

/*!
 * @brief      Radio access function for OAD module to send messages
 */
static OADProtocol_Status_t oadRadioAccessPacketSend(void* pDstAddr, uint8_t *pMsg, uint32_t msgLen)
{
    OADProtocol_Status_t status = OADProtocol_Failed;
    uint8_t* pMsduPayload;
    Cllc_associated_devices_t* pDev;

    pDev  = findDevice(pDstAddr);

    if( (pDev) && (pMsg) )
    {
        /* Buffer should have been allocated with oadRadioAccessAllocMsg,
         * so 1 byte before the oad msg buffer was allocated for the Smsgs_cmdId
         */
        pMsduPayload = pMsg - 1;
        pMsduPayload[0] = Smsgs_cmdIds_oad;

        /* Send the Tracking Request */
       if((sendMsg(Smsgs_cmdIds_oad, ((ApiMac_sAddr_t*)pDstAddr)->addr.shortAddr,
                pDev->capInfo.rxOnWhenIdle,
                (msgLen + 1),
                pMsduPayload)) == true)
       {
           status = OADProtocol_Status_Success;
       }
    }

    if( (pDev) && (pMsg) )
    {
        /* Free the memory allocated in oadRadioAccessAllocMsg. */
        free(pMsg - 1);
    }

    return status;
}

/*!
 * @brief       Process Orphan indication callback
 *
 * @param       pData - pointer to orphan indication callback structure
 */
static void orphanIndCb(ApiMac_mlmeOrphanInd_t *pData)
{
    uint16_t shortAddr = Csf_getDeviceShort(&pData->orphanAddress);
    /* get the short address of the device */
    if(CSF_INVALID_SHORT_ADDR != shortAddr)
    {
        Csf_IndicateOrphanReJoin(shortAddr);
    }

}

/*!
 * @brief   Returns the location of a delta header segment
 *
 * @param   pFile - file pointer to the OAD binary
 *
 * @return  The location of the delta segment.
 *          Otherwise returns DELTA_SEG_NOT_FOUND
 */
static long findDeltaSeg(FILE* pFile)
{
    // Save current file position to restore upon completion
    long currentPos = ftell(pFile);
    long deltaSegPos = DELTA_SEG_NOT_FOUND;

    fseek(pFile, OAD_FIXED_HDR_LEN, SEEK_SET);
    uint8_t oadSeg[DELTA_SEG_LEN];
    long prevReadPos = OAD_FIXED_HDR_LEN;

    // Find delta segment
    while (true)
    {
        fread(oadSeg, 1, DELTA_SEG_LEN, pFile);

        /* In addition to exiting on read errors and EOF, this handles the case
         * where a corrupt image could cause an infinite loop if the file read
         * pointer does not increase after every iteration.
         */
        if (ferror(pFile) != 0 || feof(pFile) != 0 || ftell(pFile) <= prevReadPos)
        {
            break;
        }
        else if (oadSeg[OAD_SEG_ID_OFFSET] != IMG_DELTA_SEG_ID)
        {
            prevReadPos = ftell(pFile);

            // Advance to the next segment
            uint32_t seekLen = *((uint32_t*)(oadSeg + OAD_SEG_LEN_OFFSET));
            fseek(pFile, seekLen - DELTA_SEG_LEN, SEEK_CUR);
        }
        else
        {
            deltaSegPos = ftell(pFile) - DELTA_SEG_LEN;
            break;
        }
    }

    // Restore old file position
    fseek(pFile, currentPos, SEEK_SET);

    return deltaSegPos;
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
