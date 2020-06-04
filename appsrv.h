/******************************************************************************
 @file appsrv.h

 @brief TIMAC 2.0 API Application Server API

 Group: WCS LPC
 $Target Device: DEVICES $

 ******************************************************************************
 $License: BSD3 2016 $
 ******************************************************************************
 $Release Name: PACKAGE NAME $
 $Release Date: PACKAGE RELEASE DATE $
 *****************************************************************************/
 #ifndef APPINTERFACE_H
 #define APPINTERFACE_H

#ifdef __cplusplus
extern "C"
{
#endif
/******************************************************************************
 Includes
 *****************************************************************************/
#include <stdbool.h>

#include "csf.h"
#include "csf_linux.h"
#include "mt_msg.h"
#include "log.h"

#define LOG_APPSRV_CONNECTIONS  _bitN(LOG_DBG_APP_bitnum_first+0)
#define LOG_APPSRV_BROADCAST    _bitN(LOG_DBG_APP_bitnum_first+1)
#define LOG_APPSRV_MSG_CONTENT  _bitN(LOG_DBG_APP_bitnum_first+2)

/******************************************************************************
 Typedefs
 *****************************************************************************/

extern struct mt_msg_interface appClient_mt_interface_template;
extern struct socket_cfg       appClient_socket_cfg;

/*
 * The API_MAC_msg_interface will point to either
 * the *npi* or the *uart* interface.
 */
extern struct mt_msg_interface npi_mt_interface;
extern struct socket_cfg       npi_socket_cfg;

extern struct mt_msg_interface uart_mt_interface;
extern struct uart_cfg         uart_cfg;

/******************************************************************************
 Function Prototypes
 *****************************************************************************/

#define APPSRV_SYS_ID_RPC 10
#define APPSRV_DEVICE_JOINED_IND 0
#define APPSRV_DEVICE_LEFT_IND 1
#define APPSRV_NWK_INFO_IND 2
#define APPSRV_GET_NWK_INFO_REQ 3
#define APPSRV_GET_NWK_INFO_RSP 4
#define APPSRV_GET_NWK_INFO_CNF 5
#define APPSRV_GET_DEVICE_ARRAY_REQ 6
#define APPSRV_GET_DEVICE_ARRAY_CNF 7
#define APPSRV_DEVICE_NOTACTIVE_UPDATE_IND 8
#define APPSRV_DEVICE_DATA_RX_IND 9
#define APPSRV_COLLECTOR_STATE_CNG_IND 10
#define APPSRV_SET_JOIN_PERMIT_REQ 11
#define APPSRV_SET_JOIN_PERMIT_CNF 12
#define APPSRV_TX_DATA_REQ 13
#define APPSRV_TX_DATA_CNF 14
#define APPSRV_RMV_DEVICE_REQ 15
#define APPSRV_RMV_DEVICE_RSP 16

#define HEADER_LEN 4
#define TX_DATA_CNF_LEN 4
#define JOIN_PERMIT_CNF_LEN 4
#define NWK_INFO_REQ_LEN 18
#define NWK_INFO_IND_LEN 17
#define DEV_ARRAY_HEAD_LEN 3
#define DEV_ARRAY_INFO_LEN 18
#define DEVICE_JOINED_IND_LEN 18
#define DEVICE_NOT_ACTIVE_LEN 13
#define STATE_CHG_IND_LEN 1
#define REMOVE_DEVICE_RSP_LEN 0

#define BEACON_ENABLED 1
#define NON_BEACON 2
#define FREQUENCY_HOPPING 3

#define RMV_STATUS_SUCCESS 0
#define RMV_STATUS_FAIL 1
/*!
 * @brief        Csf module calls this function to inform the applicaiton client
                         of the reported sensor data from a network device
 *
 * @param
 *
 * @return
 */
void appsrv_deviceRawDataUpdate(ApiMac_mcpsDataInd_t *pDataInd);

/*
 * Sets defaults for the application.
 */
void APP_defaults(void);

/*
 * Main application function.
 */
void APP_main(void);

/*!
 * @brief  Csf module calls this function to initialize the application server
 *         interface
 *
 *
 * @param
 *
 * @return
 */
 void appsrv_Init(void *param);

/*!
 * @brief        Csf module calls this function to inform the applicaiton client
 *                     that the application has either started/restored the network
 *
 * @param
 *
 * @return
 */
 void appsrv_networkUpdate(bool restored, Llc_netInfo_t *networkInfo);

/*!
 * @brief        Csf module calls this function to inform the applicaiton clientr
 *                     that a device has joined the network
 *
 * @param
 *
 * @return
 */
 void appsrv_deviceUpdate(Llc_deviceListItem_t *pDevInfo);

 /*!
 * @brief        Csf module calls this function to inform the applicaiton client
 *                     that the device has responded to the configuration request
 *
 * @param
 *
 * @return
 */
 void appsrv_deviceConfigUpdate(ApiMac_sAddr_t *pSrcAddr, int8_t rssi,
                                   Smsgs_configRspMsg_t *pMsg);

 /*!
 * @brief        Csf module calls this function to inform the applicaiton client
 *                     that a device is no longer active in the network
 *
 * @param
 *
 * @return
 */
 void appsrv_deviceNotActiveUpdate(ApiMac_deviceDescriptor_t *pDevInfo,
                                      bool timeout);
  /*!
 * @brief        Csf module calls this function to inform the applicaiton client
                         of the reported sensor data from a network device
 *
 * @param
 *
 * @return
 */
 void appsrv_deviceSensorDataUpdate(ApiMac_sAddr_t *pSrcAddr, int8_t rssi,
                                       Smsgs_sensorMsg_t *pMsg);

/*!
 * @brief        TBD
 *
 * @param
 *
 * @return
 */
 void appsrv_stateChangeUpdate(Cllc_states_t state);

/*!
 * @brief Broadcast a message to all connections
 * @param pMsg - msg to broadcast
 */
extern void appsrv_broadcast(struct mt_msg *pMsg);

/*!
 * @brief Send remove device response to gateway
 */
extern void appsrv_send_removeDeviceRsp(void);

#ifdef __cplusplus
}
#endif

#endif /* APPINTERFACE_H */

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

