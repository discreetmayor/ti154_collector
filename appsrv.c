/******************************************************************************
 @file appsrv.c

 @brief TIMAC 2.0 API User Interface Collector API

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
#include "compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include "debug_helpers.h"

#include "collector.h"

#include "api_mac.h"
#include "api_mac_linux.h"
#include "llc.h"
#include "cllc.h"
#include "smsgs.h"
#include "log.h"
#include "fatal.h"

#include "appsrv.h"
#include "csf_linux.h"
#include "mutex.h"
#include "threads.h"
#include "timer.h"

#include "stream.h"
#include "stream_socket.h"
#include "stream_uart.h"


/******************************************************************************
 Typedefs
*****************************************************************************/


struct appsrv_connection {
    /*! Is this item busy (broadcasting) */
    bool is_busy;
    /*! If something has gone wrong this is set to true */
    bool is_dead;
    /*! Name for us in debug logs */
    char *dbg_name;

    /* What connection number is this? */
    int  connection_id;

    /*! The socket interface to the gateway */
    struct mt_msg_interface socket_interface;

    /*! Thread id for the socket interface */
    intptr_t thread_id_s2appsrv;

    /*! Next connection in the list */
    struct appsrv_connection *pNext;
};

/******************************************************************************
 GLOBAL Variables
*****************************************************************************/

/*! The interface the API mac uses, points to either the socket or the uart */
struct mt_msg_interface *API_MAC_msg_interface;
/*! Generic template for all gateway connections */
struct socket_cfg appClient_socket_cfg;
/*! Configuration for apimac if using a socket (ie: npi server) */
struct socket_cfg npi_socket_cfg;
/*! UART configuration for apimac if talking to UART instead of npi */
struct uart_cfg   uart_cfg;

/*! Generic template for all gateway interfaces
  Note: These parameters can be modified via the ini file
*/
struct mt_msg_interface appClient_mt_interface_template = {
    .dbg_name                  = "appClient",
    .is_NPI                    = false,
    .frame_sync                = false,
    .include_chksum            = false,
    .hndl                      = 0,
    .s_cfg                     = NULL,
    .u_cfg                     = NULL,
    .rx_thread                 = 0,
    .tx_frag_size              = 0,
    .retry_max                 = 0,
    .frag_timeout_mSecs        = 10000,
    .intermsg_timeout_mSecs    = 10000,
    .intersymbol_timeout_mSecs = 100,
    .srsp_timeout_mSecs        = 300,
    .stack_id                  = 0,
    .len_2bytes                = true,
    .rx_handler_cookie         = 0,
    .is_dead                   = false,
    .flush_timeout_mSecs       = 100
};

/*! Generic template for uart interface
  Note: These parameters can be modified via the ini file
*/
struct mt_msg_interface uart_mt_interface = {
    .dbg_name                  = "uart",
    .is_NPI                    = false,
    .frame_sync                = true,
    .include_chksum            = true,
    .hndl                      = 0,
    .s_cfg                     = NULL,
    .u_cfg                     = &uart_cfg,
    .rx_thread                 = 0,
    .tx_frag_size              = 0,
    .retry_max                 = 0,
    .frag_timeout_mSecs        = 10000,
    .intermsg_timeout_mSecs    = 10000,
    .intersymbol_timeout_mSecs = 100,
    .srsp_timeout_mSecs        = 300,
    .stack_id                  = 0,
    .len_2bytes                = true,
    .rx_handler_cookie         = 0,
    .is_dead                   = false,
    .flush_timeout_mSecs       = 100
};

/*! Template for apimac connection to npi server
  Note: These parameters can be modified via the ini file
*/
struct mt_msg_interface npi_mt_interface = {
    .dbg_name                  = "npi",
    .is_NPI                    = false,
    .frame_sync                = true,
    .include_chksum            = true,
    .hndl                      = 0,
    .s_cfg                     = &npi_socket_cfg,
    .u_cfg                     = NULL,
    .rx_thread                 = 0,
    .tx_frag_size              = 0,
    .retry_max                 = 0,
    .frag_timeout_mSecs        = 10000,
    .intermsg_timeout_mSecs    = 10000,
    .intersymbol_timeout_mSecs = 100,
    .srsp_timeout_mSecs        = 300,
    .stack_id                  = 0,
    .len_2bytes                = true,
    .rx_handler_cookie         = 0,
    .is_dead                   = false,
    .flush_timeout_mSecs       = 100
};

/*******************************************************************
 * LOCAL VARIABLES
 ********************************************************************/

static intptr_t all_connections_mutex;
static struct appsrv_connection *all_connections;
static intptr_t all_connections_mutex;

/*******************************************************************
 * LOCAL FUNCTIONS
 ********************************************************************/

/*! Lock the list of gateway connections
  Often used when modifying the list
*/
static void lock_connection_list(void)
{
    MUTEX_lock(all_connections_mutex, -1);
}

/* See lock() above */
static void unlock_connection_list(void)
{
    MUTEX_unLock(all_connections_mutex);
}

/*!
 * @brief send a data confirm to the gateway
 * @param status - the status value to send
 */
static void send_AppsrvTxDataCnf(int status)
{
    int len = TX_DATA_CNF_LEN;
    uint8_t *pBuff;

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_TX_DATA_CNF);

    /* Create duplicate pointer to msg buffer for building */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Put status in the msg buffer */
    *pBuff++ = (uint8_t)(status & 0xFF);
    *pBuff++ = (uint8_t)((status >> 8) & 0xFF);
    *pBuff++ = (uint8_t)((status >> 16) & 0xFF);
    *pBuff++ = (uint8_t)((status >> 24) & 0xFF);

    /* Send msg  */
    MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
    MT_MSG_wrBuf(pMsg, NULL, len);
    appsrv_broadcast(pMsg);
        MT_MSG_free(pMsg);
        pMsg = NULL;
    }

/*!
 * @brief send a join confirm to the gateway
 */
static void send_AppSrvJoinPermitCnf(int status)
{
    int len = JOIN_PERMIT_CNF_LEN;
    uint8_t *pBuff;

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_SET_JOIN_PERMIT_CNF);

    /* Create duplicate pointer to msg buffer for building */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Put status in the msg buffer */
    *pBuff++ = (uint8_t)(status & 0xFF);
    *pBuff++ = (uint8_t)((status >> 8) & 0xFF);
    *pBuff++ = (uint8_t)((status >> 16) & 0xFF);
    *pBuff++ = (uint8_t)((status >> 24) & 0xFF);

    /* Send msg */
    MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
    MT_MSG_wrBuf(pMsg, NULL, len);
    appsrv_broadcast(pMsg);
        MT_MSG_free(pMsg);
        pMsg = NULL;
    }

/*!
 * @brief handle a data request from the gateway
 * @param pCONN - where the request came from
 * @param pIncomingMsg - the msg from the gateway
 */
static void appsrv_processTxDataReq(struct appsrv_connection *pCONN,
                                    struct mt_msg *pIncomingMsg)
{
    int status;
    int ind = HEADER_LEN;
    uint8_t msgId;
    uint16_t shortAddr;

    /* Parse msg */
    msgId = (uint8_t)pIncomingMsg->iobuf[ind];
    ind += 1;

    shortAddr = (uint16_t)(pIncomingMsg->iobuf[ind]) |
                (pIncomingMsg->iobuf[ind + 1] << 8);
    ind += 2;

    ApiMac_sAddr_t pDstAddr;
    pDstAddr.addrMode = ApiMac_addrType_short;
    pDstAddr.addr.shortAddr = shortAddr;

    if (msgId == Smsgs_cmdIds_configReq)
    {
        uint16_t framecontrol;
        uint32_t pollingInterval;
        uint32_t reportingInterval;
        uint8_t configStatus;

        pollingInterval = (uint32_t)(pIncomingMsg->iobuf[ind]) |
                          (pIncomingMsg->iobuf[ind + 1] << 8);
        ind += 2;

        reportingInterval = (uint32_t)(pIncomingMsg->iobuf[ind]) |
                            (pIncomingMsg->iobuf[ind + 1] << 8);
        ind += 2;

        framecontrol = (uint16_t)(pIncomingMsg->iobuf[ind]) |
                       (pIncomingMsg->iobuf[ind + 1] << 8);

        configStatus = Csf_sendConfigRequest(&pDstAddr, framecontrol, reportingInterval, pollingInterval);
        LOG_printf(LOG_APPSRV_MSG_CONTENT, " Config-req sent\n");
        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Config status: %x\n", configStatus);
    }
    else if (msgId == Smsgs_cmdIds_customCommand)
    {
        uint16_t length = ((pIncomingMsg->iobuf[ind]) |
                (pIncomingMsg->iobuf[ind + 1] << 8)) + sizeof(uint16_t);
        uint8_t *state = Csf_malloc(128); // fixme
        int i = 0;
        for(i=0; i < length; i++ ) {
        	*(state+i) = pIncomingMsg->iobuf[ind+i];
	}
        Csf_customCommand(&pDstAddr, state, length);
    }

    status = ApiMac_status_success;
    send_AppsrvTxDataCnf(status);
}

static void appsrv_processRemoveDeviceReq(struct appsrv_connection *pCONN,
                                          struct mt_msg *pIncomingMsg)
    {
    int ind = HEADER_LEN;
    uint16_t shortAddr;
    ApiMac_sAddr_t dstAddr;
    Llc_deviceListItem_t devListItem;
    ApiMac_status_t status;

    shortAddr = (uint16_t)(pIncomingMsg->iobuf[ind]) |
                (pIncomingMsg->iobuf[ind + 1] << 8);

    dstAddr.addrMode = ApiMac_addrType_short;
    dstAddr.addr.shortAddr = shortAddr;

    if (Csf_getDevice(&dstAddr, &devListItem))
    {
        /* Send disassociation request to specified sensor */
        ApiMac_mlmeDisassociateReq_t disassocReq;
        memset(&disassocReq, 0, sizeof(ApiMac_mlmeDisassociateReq_t));
        disassocReq.deviceAddress.addrMode = ApiMac_addrType_short;
        disassocReq.deviceAddress.addr.shortAddr = devListItem.devInfo.shortAddress;
        disassocReq.devicePanId = devListItem.devInfo.panID;
        disassocReq.disassociateReason = ApiMac_disassocateReason_coord;
        disassocReq.txIndirect = true;
        status = ApiMac_mlmeDisassociateReq(&disassocReq);
        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Disassociate Sent, status: %x\n", status);
    }
}

/*!
 * @brief handle a join permit request from the gateway
 * @param pCONN - where the request came from
 * @param pIncomingMsg - the msg from the gateway
 */
static void appsrv_processSetJoinPermitReq(struct appsrv_connection *pCONN,
                                           struct mt_msg *pIncomingMsg)
{
    int status;
    uint32_t duration;
    duration = (uint32_t)(pIncomingMsg->iobuf[HEADER_LEN]) |
               (pIncomingMsg->iobuf[HEADER_LEN + 1] << 8) |
               (pIncomingMsg->iobuf[HEADER_LEN + 2] << 16) |
               (pIncomingMsg->iobuf[HEADER_LEN + 3] << 24);

    /* Set the join permit */
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "\nSending duration: 0x%x\n\n", duration);
    status = Cllc_setJoinPermit(duration);

    /* Send cnf msg  */
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "\nSending permitCnf message\n\n");
    send_AppSrvJoinPermitCnf(status);
    }

/*!
 * @brief handle a getnetwork info request from the gateway
 * @param pCONN - where the request came from
 */
static void appsrv_processGetNwkInfoReq(struct appsrv_connection *pCONN)
{
    Llc_netInfo_t networkInfo;
    uint8_t *pBuff;
    int len = NWK_INFO_REQ_LEN;

    uint8_t status = (uint8_t)Csf_getNetworkInformation(&networkInfo);
    uint8_t securityEnabled = CONFIG_SECURE;
    uint8_t networkMode;
    uint8_t state = Csf_getCllcState();

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "\nSending NwkCnf STATE = %x\n\n", state);

    if (CONFIG_FH_ENABLE == true)
    {
        networkMode = FREQUENCY_HOPPING;
    }

    else
    {
        if ((CONFIG_MAC_SUPERFRAME_ORDER == 15) && (CONFIG_MAC_BEACON_ORDER == 15))
        {
            networkMode = NON_BEACON;
        }
        else
        {
            networkMode = BEACON_ENABLED;
        }
    }

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_GET_NWK_INFO_CNF);

    /* Create duplicate pointer to msg buffer for building */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Build msg */
    *pBuff++ = status;
    *pBuff++ = (uint8_t)(networkInfo.devInfo.panID & 0xFF);
    *pBuff++ = (uint8_t)((networkInfo.devInfo.panID >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.shortAddress & 0xFF);
    *pBuff++ = (uint8_t)((networkInfo.devInfo.shortAddress >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[0]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[1]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[2]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[3]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[4]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[5]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[6]);
    *pBuff++ = (uint8_t)(networkInfo.devInfo.extAddress[7]);
    *pBuff++ = (uint8_t)(networkInfo.channel);
    *pBuff++ = (uint8_t)(networkInfo.fh);
    *pBuff++ = securityEnabled;
    *pBuff++ = networkMode;
    *pBuff++ = state;

    /* Send msg */
        MT_MSG_setDestIface(pMsg, &(pCONN->socket_interface));
        MT_MSG_wrBuf(pMsg, NULL, len);
        MT_MSG_txrx(pMsg);
        MT_MSG_free(pMsg);
    pMsg = NULL;
    }

/*!
 * @brief  Process incoming getDeviceArrayReq message
 *
 * @param pConn - the connection
 */
static void appsrv_processGetDeviceArrayReq(struct appsrv_connection *pCONN)
{
    uint16_t n = 0;
    uint8_t *pBuff;
    Csf_deviceInformation_t *pDeviceInfo;

    uint8_t status = ApiMac_status_success;
    n = (uint16_t)Csf_getDeviceInformationList(&pDeviceInfo);

    int len = DEV_ARRAY_HEAD_LEN + (DEV_ARRAY_INFO_LEN * n);

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_GET_DEVICE_ARRAY_CNF);

    /* Create duplicate pointer to msg buffer for building */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Build msg */
    *pBuff++ = status;
    *pBuff++ = (uint8_t)(n & 0xFF);
    *pBuff++ = (uint8_t)((n >> 8) & 0xFF);

    uint16_t x;
    for (x = 0; x < n; x++)
    {
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.panID & 0xFF);
        *pBuff++ = (uint8_t)((pDeviceInfo[x].devInfo.panID >> 8) & 0xFF);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.shortAddress & 0xFF);
        *pBuff++ = (uint8_t)((pDeviceInfo[x].devInfo.shortAddress >> 8) & 0xFF);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[0]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[1]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[2]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[3]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[4]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[5]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[6]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].devInfo.extAddress[7]);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.panCoord);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.ffd);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.mainsPower);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.rxOnWhenIdle);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.security);
        *pBuff++ = (uint8_t)(pDeviceInfo[x].capInfo.allocAddr);
    }

    /* Send msg */
            MT_MSG_setDestIface(pMsg, &(pCONN->socket_interface));
            MT_MSG_wrBuf(pMsg, NULL, len);
            MT_MSG_txrx(pMsg);
    MT_MSG_free(pMsg);
    pMsg = NULL;

    if (pDeviceInfo)
    {
        Csf_freeDeviceInformationList(n, pDeviceInfo);
        pDeviceInfo = NULL;
    }
}

/******************************************************************************
 Function Implementation
*****************************************************************************/

/*
  Broadcast a message to all connections.
  Public function in appsrv.h
*/
void appsrv_broadcast(struct mt_msg *pMsg)
{
    struct appsrv_connection *pCONN;
    struct mt_msg *pClone;

    /* mark all connections as "ready to broadcast" */
    lock_connection_list();

    for(pCONN = all_connections ; pCONN ; pCONN = pCONN->pNext)
    {
        pCONN->is_busy = false;
    }

    unlock_connection_list();

next_connection:
    /* find first connection in "ready-state"
     * NOTE: this loop is funny we goto the top
     * and we restart scanning from the head
     * because ... while broadcasting a new
     * connection may appear, or one may go away
     */
    lock_connection_list();
    for(pCONN = all_connections ; pCONN ; pCONN = pCONN->pNext)
    {
        /* this one is dead */
        if(pCONN->is_dead)
        {
            continue;
        }
        /* is this one ready? */

        if(pCONN->is_busy == false)
        {
            /* Yes we have found one */
            pCONN->is_busy = true;
            break;
        }
    }
    unlock_connection_list();

    /* Did we find a connection? */
    if(pCONN)
    {
        /* we have a connection we can send */
        pClone = MT_MSG_clone(pMsg);
        if(pClone)
        {
            MT_MSG_setDestIface(pClone, &(pCONN->socket_interface));
            MT_MSG_txrx(pClone);
            MT_MSG_free(pClone);
        }
        /* leave this connection as 'busy'
         * busy really means: "done"
         * so that we do not repeat this connection
         * we clean up the list later
         *---
         * Go back to the *FIRST* connection
         * And search again.... from the top..
         * Why? Because connections may have died...
         */
        goto next_connection;
    }

    /* if we get here - we have no more connections to broadcast to */
    /* mark all items as idle */
    lock_connection_list();
    for(pCONN = all_connections ; pCONN ; pCONN = pCONN->pNext)
    {
        pCONN->is_busy = false;
    }
    unlock_connection_list();
}

/*!
  Csf module calls this function to inform the user/appClient
  that the application has either started/restored the network

  Public function defined in appsrv_Collector.h
*/
void appsrv_networkUpdate(bool restored, Llc_netInfo_t *networkInfo)
{
    int len = NWK_INFO_IND_LEN;
    uint8_t *pBuff;

    uint8_t securityEnabled = CONFIG_SECURE;
    uint8_t networkMode;
    uint8_t state = Csf_getCllcState();

    if (CONFIG_FH_ENABLE == true)
    {
        networkMode = FREQUENCY_HOPPING;
    }

    else
    {
        if ((CONFIG_MAC_SUPERFRAME_ORDER == 15) && (CONFIG_MAC_BEACON_ORDER == 15))
        {
            networkMode = NON_BEACON;
        }
        else
        {
            networkMode = BEACON_ENABLED;
        }
    }

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_NWK_INFO_IND);

    /* Create duplicate pointer to msg buffer for building purposes */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Build msg */
    *pBuff++ = (uint8_t)(networkInfo->devInfo.panID & 0xFF);
    *pBuff++ = (uint8_t)((networkInfo->devInfo.panID >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.shortAddress & 0xFF);
    *pBuff++ = (uint8_t)((networkInfo->devInfo.shortAddress >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[0]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[1]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[2]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[3]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[4]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[5]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[6]);
    *pBuff++ = (uint8_t)(networkInfo->devInfo.extAddress[7]);
    *pBuff++ = (uint8_t)(networkInfo->channel);
    *pBuff++ = (uint8_t)(networkInfo->fh);
    *pBuff++ = securityEnabled;
    *pBuff++ = networkMode;
    *pBuff++ = state;

    /* Send msg */
            MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
            MT_MSG_wrBuf(pMsg, NULL, len);
            appsrv_broadcast(pMsg);
            MT_MSG_free(pMsg);
            pMsg = NULL;
        }

/*!

  Csf module calls this function to inform the user/appClient
  that a device has joined the network

  Public function defined in appsrv_Collector.h
*/
void appsrv_deviceUpdate(Llc_deviceListItem_t *pDevListItem)
{
    int len = DEVICE_JOINED_IND_LEN;
    uint8_t *pBuff;

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_DEVICE_JOINED_IND);

    /* Create duplicate pointer to msg buffer for building purposes */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Build msg */
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.panID & 0xFF);
    *pBuff++ = (uint8_t)((pDevListItem->devInfo.panID >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.shortAddress & 0xFF);
    *pBuff++ = (uint8_t)((pDevListItem->devInfo.shortAddress >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[0]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[1]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[2]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[3]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[4]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[5]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[6]);
    *pBuff++ = (uint8_t)(pDevListItem->devInfo.extAddress[7]);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.panCoord);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.ffd);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.mainsPower);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.rxOnWhenIdle);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.security);
    *pBuff++ = (uint8_t)(pDevListItem->capInfo.allocAddr);

    /* Send msg */
            MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
            MT_MSG_wrBuf(pMsg, NULL, len);
            appsrv_broadcast(pMsg);
            MT_MSG_free(pMsg);
            pMsg = NULL;
        }


/*
 * @brief common code to handle sensor data messages
 * @param pSrcAddr - address related to this message
 *
 * In the end, the message is sent to the gateway
 */
void appsrv_deviceRawDataUpdate(ApiMac_mcpsDataInd_t *pDataInd)
{
    // Get the length (srcAddr + rssi + msdu.len)
    uint16_t bufferLength = pDataInd->msdu.len + sizeof(pDataInd->rssi) + sizeof(ApiMac_sAddr_t);
    uint8_t buffer[bufferLength];
    memset(buffer, 0, bufferLength);
    uint16_t idx = 0;
    buffer[idx++] = pDataInd->srcAddr.addrMode;
    if (pDataInd->srcAddr.addrMode == ApiMac_addrType_short)
    {
        buffer[idx++] = (uint8_t)(pDataInd->srcAddr.addr.shortAddr & 0xFF);
        buffer[idx++] = (uint8_t)((pDataInd->srcAddr.addr.shortAddr >> 8) & 0xFF);
    }
    else if (pDataInd->srcAddr.addrMode == ApiMac_addrType_extended)
    {
        memcpy((buffer + idx), pDataInd->srcAddr.addr.extAddr, APIMAC_SADDR_EXT_LEN);
        idx += APIMAC_SADDR_EXT_LEN;
    }
    buffer[idx++] = pDataInd->rssi;
    memcpy((buffer + idx), pDataInd->msdu.p, pDataInd->msdu.len);

    // initalize the message struct
    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        bufferLength,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_DEVICE_DATA_RX_IND);

    //add the ApiMac data to the message iobuf
    uint16_t i;
    for (i = 0; i < bufferLength; i++)
    {
        pMsg->iobuf[i + HEADER_LEN] = buffer[i];
    }

    // send the message buffer over a socket to the appclient
    MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
    MT_MSG_wrBuf(pMsg, NULL, bufferLength);
    appsrv_broadcast(pMsg);
    MT_MSG_free(pMsg);
    pMsg = NULL;
}


void appsrv_send_removeDeviceRsp(void)
{
    int len = REMOVE_DEVICE_RSP_LEN;

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_RMV_DEVICE_RSP);

    /* Send msg */
    MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
    MT_MSG_wrBuf(pMsg, NULL, len);
    appsrv_broadcast(pMsg);
    MT_MSG_free(pMsg);
    pMsg = NULL;
}

/*!
  Csf module calls this function to inform the user/appClient
  that a device is no longer active in the network

  Public function defined in appsrv_Collector.h
*/
void appsrv_deviceNotActiveUpdate(ApiMac_deviceDescriptor_t *pDevInfo,
                                  bool timeout)
{
    int len = DEVICE_NOT_ACTIVE_LEN;
    uint8_t *pBuff;

    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_DEVICE_NOTACTIVE_UPDATE_IND);

    /* Create duplicate pointer to msg buffer for building purposes */
    pBuff = pMsg->iobuf + HEADER_LEN;

    /* Build msg */
    *pBuff++ = (uint8_t)(pDevInfo->panID & 0xFF);
    *pBuff++ = (uint8_t)((pDevInfo->panID >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(pDevInfo->shortAddress & 0xFF);
    *pBuff++ = (uint8_t)((pDevInfo->shortAddress >> 8) & 0xFF);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[0]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[1]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[2]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[3]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[4]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[5]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[6]);
    *pBuff++ = (uint8_t)(pDevInfo->extAddress[7]);
    *pBuff++ = (uint8_t)timeout;

    /* Send msg */
            MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
            MT_MSG_wrBuf(pMsg, NULL, len);
            appsrv_broadcast(pMsg);
            MT_MSG_free(pMsg);
            pMsg = NULL;
        }



/*!
  TBD

  Public function defined in appsrv_Collector.h
*/
void appsrv_stateChangeUpdate(Cllc_states_t state)
{
    int len = STATE_CHG_IND_LEN;
    struct mt_msg *pMsg;
    pMsg = MT_MSG_alloc(
        len,
        MT_MSG_cmd0_areq(APPSRV_SYS_ID_RPC),
        APPSRV_COLLECTOR_STATE_CNG_IND);

    /* Build msg, no need for duplicate pointer*/
    pMsg->iobuf[HEADER_LEN] = (uint8_t)(state & 0xFF);

        MT_MSG_setDestIface(pMsg, &appClient_mt_interface_template);
        MT_MSG_wrBuf(pMsg, NULL, len);
        appsrv_broadcast(pMsg);
        MT_MSG_free(pMsg);
        pMsg = NULL;
    }

/*********************************************************************
 * Local Functions
 *********************************************************************/

/*!
 * @brief handle a request from a client.
 * @param pCONN - the client connection details
 * @param pMsg  - the message we received.
 */
static void appsrv_handle_appClient_request( struct appsrv_connection *pCONN,
                                             struct mt_msg *pMsg )
{
    int subsys = _bitsXYof(pMsg->cmd0 , 4, 0);
    int handled = true;
    if (subsys != APPSRV_SYS_ID_RPC)
    {
        handled = false;
    }
    else
    {
        switch(pMsg->cmd1)
        {
        default:
            handled = false;
            break;
            /*
             * NOTE: ADD MORE ITEMS HERE TO EXTEND THE EXAMPLE
             */
        case APPSRV_GET_DEVICE_ARRAY_REQ:
            /* Rcvd data from Client */
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "rcvd get device array msg\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            appsrv_processGetDeviceArrayReq(pCONN);
            break;

        case APPSRV_GET_NWK_INFO_REQ:
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "getnwkinfo req message\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            appsrv_processGetNwkInfoReq(pCONN);
            break;

        case APPSRV_SET_JOIN_PERMIT_REQ:
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "rcvd join premit message\n ");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            appsrv_processSetJoinPermitReq(pCONN, pMsg);
            break;

        case APPSRV_TX_DATA_REQ:
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "rcvd req to send message to a device\n ");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            appsrv_processTxDataReq(pCONN, pMsg);
            break;
        case APPSRV_RMV_DEVICE_REQ:
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "rcvd req to remove a device\n ");
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "______________________________\n");
            appsrv_processRemoveDeviceReq(pCONN, pMsg);
            break;
        }
    }
    if(!handled)
    {
        MT_MSG_log(LOG_ERROR, pMsg, "unknown msg\n");
    }
}

/*
 * @brief specific connection thread
 * @param cookie - opaque parameter that is the connection details.
 *
 * The server thread creates these as needed
 *
 * This thread lives until the connection dies.
 */
static intptr_t s2appsrv_thread(intptr_t cookie)
{
    struct appsrv_connection *pCONN;
    struct mt_msg *pMsg;
    int r;
    char iface_name[30];
    char star_line[30];
    int star_line_char;

    pCONN = (struct appsrv_connection *)(cookie);
    if( pCONN == NULL )
    {
        BUG_HERE("pCONN is null?\n");
    }

    /* create our upstream interface */
    (void)snprintf(iface_name,
                   sizeof(iface_name),
                   "s2u-%d-iface",
                   pCONN->connection_id);
    pCONN->socket_interface.dbg_name = iface_name;

    /* Create our interface */
    r = MT_MSG_interfaceCreate(&(pCONN->socket_interface));
    if(r != 0)
    {
        BUG_HERE("Cannot create socket interface?\n");
    }

    /* Add this connection to the list. */
    lock_connection_list();
    pCONN->pNext = all_connections;
    pCONN->is_busy = false;
    all_connections = pCONN;
    unlock_connection_list();

    star_line_char = 0;
    /* Wait for messages to come in from the socket.. */
    for(;;)
    {
        /* Did the other guy die? */
        if(pCONN->is_dead)
        {
            break;
        }

        /* did the socket die? */
        if(pCONN->socket_interface.is_dead)
        {
            pCONN->is_dead = true;
            continue;
        }

        /* get our message */
        pMsg = MT_MSG_LIST_remove(&(pCONN->socket_interface),
                                  &(pCONN->socket_interface.rx_list), 1000);
        if(pMsg == NULL)
        {
            /* must have timed out. */
            continue;
        }

        pMsg->pLogPrefix = "web-request";

        /* Print a *MARKER* line in the log to help trace this message */
        star_line_char++;
        /* Cycle through the letters AAAA, BBBB, CCCC .... */
        star_line_char = star_line_char % 26;
        memset((void *)(star_line),
               star_line_char + 'A',
               sizeof(star_line) - 1);
        star_line[sizeof(star_line) - 1] = 0;
        LOG_printf(LOG_DBG_MT_MSG_traffic, "START MSG: %s\n", star_line);

        /* Actually process the request */
        appsrv_handle_appClient_request(pCONN, pMsg);

        /* Same *MARKER* line at the end of the message */
        LOG_printf(LOG_DBG_MT_MSG_traffic, "END MSG: %s\n", star_line);
        MT_MSG_free(pMsg);
        pMsg = NULL;
    }

    /* There is an interock here.
     * FIRST: We set "is_dead"
     *   The broadcast code will skip this item.
     *   if and only if it is dead.
     * HOWEVER
     *   Q: What happens if we die in the middle of broadcasting?
     *   A: We must wait until the broad cast is complete
     *      We know this by checking the bcast_state.
     */
    while(pCONN->is_busy)
    {
        /* we must wait ... a broadcast is active. */
        /* so sleep alittle and try again */
        TIMER_sleep(10);
    }

    /* we can now remove this DEAD connection from the list. */
    lock_connection_list();
    {
        struct appsrv_connection **ppTHIS;

        /* find our self in the list of connections. */
        for(ppTHIS = &all_connections ;
            (*ppTHIS) != NULL ;
            ppTHIS = &((*ppTHIS)->pNext))
        {
            /* found? */
            if((*ppTHIS) == pCONN)
            {
                /* yes we are done */
                break;
            }
        }

        /* did we find this one? */
        if(*ppTHIS)
        {
            /* remove this one from the list */
            (*ppTHIS) = pCONN->pNext;
            pCONN->pNext = NULL;
        }
    }
    unlock_connection_list();
    /* socket is dead */
    /* we need to destroy the interface */
    MT_MSG_interfaceDestroy(&(pCONN->socket_interface));

    /* destroy the socket. */
    SOCKET_ACCEPT_destroy(pCONN->socket_interface.hndl);

    /* we can free this now */
    free((void *)pCONN);

    /* we do not destroy free the connection */
    /* this is done in the uart thread */
    return 0;
}

/*
 * @brief This thread handles all connections from the nodeJS/gateway client.
 *
 * This server thread spawns off threads that handle
 * each connection from each gateway app.
 */

static intptr_t appsrv_server_thread(intptr_t _notused)
{
    (void)(_notused);

    int r;
    intptr_t server_handle;
    struct appsrv_connection *pCONN;
    int connection_id;
    char buf[30];

    pCONN = NULL;
    connection_id = 0;

    server_handle = SOCKET_SERVER_create(&appClient_socket_cfg);
    if(server_handle == 0)
    {
        BUG_HERE("cannot create socket to listen\n");
    }

    r = SOCKET_SERVER_listen(server_handle);
    if(r != 0)
    {
        BUG_HERE("cannot set listen mode\n");
    }

    /* Wait for connections :-) */
    for(;;)
    {
        if(STREAM_isError(server_handle))
        {
            LOG_printf(LOG_ERROR, "server (accept) socket is dead\n");
            break;
        }
        if(pCONN == NULL)
        {
            pCONN = calloc(1, sizeof(*pCONN));
            if(pCONN == NULL)
            {
                BUG_HERE("No memory\n");
            }
            pCONN->connection_id = connection_id++;

            /* clone the connection details */
            pCONN->socket_interface = appClient_mt_interface_template;

            (void)snprintf(buf,sizeof(buf),
                           "connection-%d",
                           pCONN->connection_id);
            pCONN->dbg_name = strdup(buf);
            if(pCONN->dbg_name == NULL)
            {
                BUG_HERE("no memory\n");
            }
        }

        /* wait for a connection.. */
        r = SOCKET_SERVER_accept(&(pCONN->socket_interface.hndl),
                                 server_handle,
                                 appClient_socket_cfg.connect_timeout_mSecs);
        if(r < 0)
        {
            BUG_HERE("cannot accept!\n");
        }
        if(r == 0)
        {
            LOG_printf(LOG_APPSRV_CONNECTIONS, "no connection yet\n");
            continue;
        }
        /* we have a connection */
        pCONN->is_dead = false;

        /* create our connection threads */

        /* set name final use */
        (void)snprintf(buf, sizeof(buf),
                       "connection-%d",
                       pCONN->connection_id);
        pCONN->dbg_name = strdup(buf);
        if(pCONN->dbg_name == NULL)
        {
            BUG_HERE("no memory\n");
        }

        (void)snprintf(buf, sizeof(buf),
                       "thread-u2s-%d",
                       pCONN->connection_id);
        pCONN->thread_id_s2appsrv = THREAD_create(buf,
                                                  s2appsrv_thread,
                                                  (intptr_t)(pCONN),
                                                  THREAD_FLAGS_DEFAULT);

        pCONN = NULL;

    }
    return 0;
}


/*
 * @brief the primary "collector thread"
 *
 * This thread never exists and performs all of
 * the 'collector' application tasks.
 */

static intptr_t collector_thread(intptr_t dummy)
{
    (void)(dummy);
    for(;;)
    {
        /* this will "pend" on a semaphore */
        /* waiting for messages to come */
        Collector_process();
    }
#if defined(__linux__)
    /* gcc complains, unreachable.. */
    /* other analisys tools do not .. Grrr. */
    return 0;
#endif
}

/*
  This is the main application, "linux_main.c" calls this.
*/
void APP_main(void)
{
    int r;
    intptr_t server_thread_id;
    intptr_t collector_thread_id;
    struct appsrv_connection *pCONN;

    all_connections_mutex = MUTEX_create("all-connections");

    if(all_connections_mutex == 0)
    {
        BUG_HERE("cannot create connection list mutex\n");
    }

    Collector_init();
    r = MT_DEVICE_version_info.transport |
        MT_DEVICE_version_info.product |
        MT_DEVICE_version_info.major |
        MT_DEVICE_version_info.minor |
        MT_DEVICE_version_info.maint;
    if( r == 0 )
    {
        FATAL_printf( "Did not get device version info at startup - Bailing out\n");
    }


    LOG_printf( LOG_ALWAYS, "Found Mac Co-Processor Version info is:\n");
    LOG_printf( LOG_ALWAYS, "Transport: %d\n", MT_DEVICE_version_info.transport );
    LOG_printf( LOG_ALWAYS, "  Product: %d\n", MT_DEVICE_version_info.product   );
    LOG_printf( LOG_ALWAYS, "    Major: %d\n", MT_DEVICE_version_info.major     );
    LOG_printf( LOG_ALWAYS, "    Minor: %d\n", MT_DEVICE_version_info.minor     );
    LOG_printf( LOG_ALWAYS, "    Maint: %d\n", MT_DEVICE_version_info.maint     );

#ifdef IS_HEADLESS
    fprintf( stdout, "Found Mac Co-Processor Version info is:\n");
    fprintf( stdout, "Transport: %d\n", MT_DEVICE_version_info.transport );
    fprintf( stdout, "  Product: %d\n", MT_DEVICE_version_info.product   );
    fprintf( stdout, "    Major: %d\n", MT_DEVICE_version_info.major     );
    fprintf( stdout, "    Minor: %d\n", MT_DEVICE_version_info.minor     );
    fprintf( stdout, "    Maint: %d\n", MT_DEVICE_version_info.maint     );
    fprintf( stdout, "----------------------------------------\n");
    fprintf( stdout, "Start the gateway application\n");
#endif //IS_HEADLESS

    server_thread_id = THREAD_create("server-thread",
                                     appsrv_server_thread, 0,
                                     THREAD_FLAGS_DEFAULT);

    collector_thread_id = THREAD_create("collector-thread",
                                        collector_thread, 0, THREAD_FLAGS_DEFAULT);


    for(;;)
    {
        /* every 10 seconds.. */
        /* 10 seconds is an arbitrary value */
        TIMER_sleep(10 * 1000);
        r = 0;

        if(THREAD_isAlive(collector_thread_id))
        {
            r += 1;
        }

        if(THREAD_isAlive(server_thread_id))
        {
            r += 1;
        }
        /* we stay here while both *2* threads are alive */
        if(r != 2)
        {
            break;
        }
    }

    /* wait at most (N) seconds then we just 'die' */
    r = 0;
    while(r < 10)
    {

        lock_connection_list();
        pCONN = all_connections;
        /* mark them all as dead */
        while(pCONN)
        {
            pCONN->is_dead = true;
            LOG_printf(LOG_APPSRV_CONNECTIONS,
                       "Connection: %s is still alive\n",
                       pCONN->dbg_name);
            pCONN = pCONN->pNext;
        }
        unlock_connection_list();

        /* still have connections? */
        if(all_connections)
        {
            /* wait a second.. */
            TIMER_sleep(10000);
            /* and try again */
            r++;
            continue;
        }
        else
        {
            break;
        }
    }
    /* thread exit */
}

/*!
 * Set default items for the application.
 * These are set before the ini file is read.
 */
void APP_defaults(void)
{
    memset( (void *)(&appClient_socket_cfg), 0, sizeof(appClient_socket_cfg) );
    /*
      NOTE: All of these settings can be modified by ini file.
    */
    appClient_socket_cfg.inet_4or6 = 4;
    /*! this is a 's'=server, not a 'c'client */
    appClient_socket_cfg.ascp = 's';
    appClient_socket_cfg.host = NULL;
    /*! we listen on port 5000 */
    appClient_socket_cfg.service = strdup( "5000" );
    /*! and only allow one connection */
    appClient_socket_cfg.server_backlog = 1;
    appClient_socket_cfg.device_binding = NULL;
    /*! print a 'non-connect' every minute */
    appClient_socket_cfg.connect_timeout_mSecs = 60 * 1000;
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

