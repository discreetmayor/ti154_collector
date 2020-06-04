/******************************************************************************

 @file collector.h

 @brief TIMAC 2.0 Collector Example Application Header

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
#ifndef COLLECTOR_H
#define COLLECTOR_H

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
 Constants and definitions
 *****************************************************************************/

#ifndef MAX
#define MAX(n,m)   (((n) < (m)) ? (m) : (n))
#endif

/*! Event ID - Start the device in the network */
#define COLLECTOR_START_EVT 0x0001
/*! Event ID - Tracking Timeout Event */
#define COLLECTOR_TRACKING_TIMEOUT_EVT 0x0002
/*! Event ID - Generate Configs Event */
#define COLLECTOR_CONFIG_EVT 0x0004
/*! Event ID - Broadcast Timeout Event */
#define COLLECTOR_BROADCAST_TIMEOUT_EVT 0x0008

/*! Collector Status Values */
typedef enum
{
    /*! Success */
    Collector_status_success = 0,
    /*! Device Not Found */
    Collector_status_deviceNotFound = 1,
    /*! Collector isn't in the correct state to send a message */
    Collector_status_invalid_state = 2,
    /*! Collector isn't in the correct state to send a message */
    Collector_status_invalid_file = 3,
    /*! Collector cannot locate the file_id provided */
    Collector_status_invalid_file_id = 4,
} Collector_status_t;

/* Beacon order for non beacon network */
#define NON_BEACON_ORDER      15

/* Number of superframe periods to hold a indirect packet at collector for
Sensor to poll and get the frame*/
#define BCN_MODE_INDIRECT_PERSISTENT_TIME 3

#define MIN_PERSISTENCE_TIME_USEC 2000000

#if ((CONFIG_PHY_ID >= APIMAC_MRFSK_STD_PHY_ID_BEGIN) && (CONFIG_PHY_ID <= APIMAC_MRFSK_GENERIC_PHY_ID_BEGIN))

/* MAC Indirect Persistent Timeout */
#define INDIRECT_PERSISTENT_TIME (MAX((5 * 1000 * CONFIG_POLLING_INTERVAL / 2), MIN_PERSISTENCE_TIME_USEC)/ \
                                  (BASE_SUPER_FRAME_DURATION * \
                                   SYMBOL_DURATION_50_kbps))

#elif ((CONFIG_PHY_ID >= APIMAC_200KBPS_915MHZ_PHY_132) && (CONFIG_PHY_ID <= APIMAC_200KBPS_868MHZ_PHY_133))

/* MAC Indirect Persistent Timeout */
#define INDIRECT_PERSISTENT_TIME (MAX((5 * 1000 * CONFIG_POLLING_INTERVAL / 2),MIN_PERSISTENCE_TIME_USEC) / \
                                  (BASE_SUPER_FRAME_DURATION * \
                                   SYMBOL_DURATION_200_kbps))

#elif (CONFIG_PHY_ID == APIMAC_250KBPS_IEEE_PHY_0)

/* MAC Indirect Persistent Timeout */
#define INDIRECT_PERSISTENT_TIME (MAX((5* 10 * 1000 * CONFIG_POLLING_INTERVAL / 2), MIN_PERSISTENCE_TIME_USEC)/ \
                                  (BASE_SUPER_FRAME_DURATION * \
                                   SYMBOL_DURATION_250_kbps))

#else
/* MAC Indirect Persistent Timeout */
#define INDIRECT_PERSISTENT_TIME (MAX((5 * 1000 * CONFIG_POLLING_INTERVAL / 2), MIN_PERSISTENCE_TIME_USEC)/ \
                                  (BASE_SUPER_FRAME_DURATION * \
                                    SYMBOL_DURATION_LRM))
#endif

/******************************************************************************
 Structures
 *****************************************************************************/

/*! Collector Statistics */
typedef struct
{
    /*!
     Total number of tracking request messages attempted
     */
    uint32_t trackingRequestAttempts;
    /*!
     Total number of tracking request messages sent
     */
    uint32_t trackingReqRequestSent;
    /*!
     Total number of tracking response messages received
     */
    uint32_t trackingResponseReceived;
    /*!
     Total number of config request messages attempted
     */
    uint32_t configRequestAttempts;
    /*!
     Total number of config request messages sent
     */
    uint32_t configReqRequestSent;
    /*!
     Total number of config response messages received
     */
    uint32_t configResponseReceived;
    /*!
     Total number of sensor messages received
     */
    uint32_t sensorMessagesReceived;
    /*!
     Total number of failed messages because of channel access failure
     */
    uint32_t channelAccessFailures;
    /*!
     Total number of failed messages because of ACKs not received
     */
    uint32_t ackFailures;
    /*!
     Total number of failed transmit messages that are not channel access
     failure and not ACK failures
     */
    uint32_t otherTxFailures;
    /*! Total number of RX Decrypt failures. */
    uint32_t rxDecryptFailures;
    /*! Total number of TX Encrypt failures. */
    uint32_t txEncryptFailures;
    /* Total Transaction Expired Count */
    uint32_t txTransactionExpired;
    /* Total transaction Overflow error */
    uint32_t txTransactionOverflow;
    /* Total broadcast messages sent */
    uint16_t broadcastMsgSentCnt;
} Collector_statistics_t;

/******************************************************************************
 Global Variables
 *****************************************************************************/

/*! Collector events flags */
extern uint16_t Collector_events;

/*! Collector statistics */
extern Collector_statistics_t Collector_statistics;

extern ApiMac_callbacks_t Collector_macCallbacks;

/******************************************************************************
 Function Prototypes
 *****************************************************************************/

/*!
 * @brief Initialize this application.
 */
extern void Collector_init(void);

/*!
 * @brief Application task processing.
 */
extern void Collector_process(void);

/*!
 * @brief Build and send the configuration message to a device.
 *
 * @param pDstAddr - destination address of the device to send the message
 * @param frameControl - configure what to the device is to report back.
 *                       Ref. Smsgs_dataFields_t.
 * @param reportingInterval - in milliseconds- how often to report, 0
 *                            means to turn off automated reporting, but will
 *                            force the sensor device to send the Sensor Data
 *                            message once.
 * @param pollingInterval - in milliseconds- how often to the device is to
 *                          poll its parent for data (for sleeping devices
 *                          only.
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_sendConfigRequest(ApiMac_sAddr_t *pDstAddr,
                uint16_t frameControl,
                uint32_t reportingInterval,
                uint32_t pollingInterval);

/*!
 * @brief Update the collector statistics
 */
extern void Collector_updateStats( void );

/*!
 * @brief Build and send the toggle led message to a device.
 *
 * @param pDstAddr - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_sendToggleLedRequest(
                ApiMac_sAddr_t *pDstAddr);


extern Collector_status_t Collector_sendCustomCommand(
                ApiMac_sAddr_t *pDstAddr,
                uint8_t *payload,
               uint16_t length);

/*!
 * @brief Build and send the device type request message to a device.
 *
 * @param pDstAddr - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_sendDeviceTypeRequest(
        ApiMac_sAddr_t *pDstAddr);

/*!
 * @brief Get the file name associated with file_id
 *
 * @param file_id - id of file who's name you wish to get
 *
 * @param file_name - buffer to store null terminating file_name into.
 *
 * @return Collector_status_success or Collector_status_invalid_file_id
 */
extern Collector_status_t Collector_getFileName(uint32_t file_id, char* file_name, size_t max_len);

/*!
 * @brief Adds a new file to the OAd file list.
 *
 * @param new_oad_file - path to OAD file
 *
 * @return OAD file ID
 */
extern uint32_t Collector_updateFwList(char *new_oad_file);

/*!
 * @brief Send OAD version request message.
 *
 * @param pDstAddr - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_sendFwVersionRequest(
                ApiMac_sAddr_t *pDstAddr);

/*!
 * @brief Check if a device exists
 *
 * @param pAddr - destination address of the device
 *
 * @return Collector_status_success or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_findDevice(
                ApiMac_sAddr_t *pAddr);

/*!
 * @brief Send OAD update request message.
 *
 * @param pDstAddr    - destination address of the device to send the message
 * @param oad_file_id - OAD file ID
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_startFwUpdate(ApiMac_sAddr_t *pDstAddr, uint32_t oad_file_id);

/*!
 * @brief Send OAD target reset request message.
 *
 * @param pDstAddr    - destination address of the device to send the message
 *
 * @return Collector_status_success, Collector_status_invalid_state
 *         or Collector_status_deviceNotFound
 */
extern Collector_status_t Collector_sendResetReq(ApiMac_sAddr_t *pDstAddr);


#ifdef __cplusplus
}
#endif

#endif /* COLLECTOR_H */

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
