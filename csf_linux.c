/******************************************************************************

 @file csf_linux.c [Linux version of csf.c]

 @brief Collector Specific Functions

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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include "nvintf.h"
#include "nv_linux.h"
#include "log.h"
#include "mutex.h"
#include "ti_semaphore.h"
#include "timer.h"
#include "appsrv.h"
#include "time.h"
#include "mac_util.h"
#include "ti_154stack_config.h"
#include "api_mac.h"
#include "api_mac_linux.h"
#include "collector.h"
#include "cllc.h"
#include "csf.h"

#if defined(MT_CSF)
#include "mt_csf.h"
#endif

/* additional linux information */
#include "csf_linux.h"

/******************************************************************************
 Constants and definitions
 *****************************************************************************/

/* Initial timeout value for the tracking clock */
#define TRACKING_INIT_TIMEOUT_VALUE 100

/* NV Item ID - the device's network information */
#define CSF_NV_NETWORK_INFO_ID 0x0001
/* NV Item ID - the number of device list entries */
#define CSF_NV_DEVICELIST_ENTRIES_ID 0x0004
/* NV Item ID - the device list, use sub ID for each record in the list */
#define CSF_NV_DEVICELIST_ID 0x0005
/* NV Item ID - this devices frame counter */
#define CSF_NV_FRAMECOUNTER_ID 0x0006
/* NV Item ID - reset reason */
#define CSF_NV_RESET_REASON_ID 0x0007

/* Maximum number of device list entries */
#define CSF_MAX_DEVICELIST_ENTRIES CONFIG_MAX_DEVICES

/*
 Maximum sub ID for a device list item, this is failsafe.  This is
 not the maximum number of items in the list
 */
#define CSF_MAX_DEVICELIST_IDS (2*CONFIG_MAX_DEVICES)

/* timeout value for trickle timer initialization */
#define TRICKLE_TIMEOUT_VALUE       20

/* timeout value for join timer */
#define JOIN_TIMEOUT_VALUE       20

/* timeout value for config request delay */
#define CONFIG_TIMEOUT_VALUE 1000

/* timeout value for config request delay */
#define IDENTIFY_TIMEOUT_VALUE 10

/* Timeout value in ms for sending the next OAD request request if not received */
#define OAD_RESET_REQ_RETRY_TIMEOUT_VALUE 12000

/* Amount of times the collector will sent a reset reqest if not received */
#define OAD_RESET_REQ_MAX_RETRIES 3

/*
 The increment value needed to save a frame counter. Example, setting this
 constant to 100, means that the frame counter will be saved when the new
 frame counter is 100 more than the last saved frame counter.  Also, when
 the get frame counter function reads the value from NV it will add this value
 to the read value.
 */
#define FRAME_COUNTER_SAVE_WINDOW     25

/* Value returned from findDeviceListIndex() when not found */
#define DEVICE_INDEX_NOT_FOUND  -1

/*! NV driver item ID for reset reason */
#define NVID_RESET {NVINTF_SYSID_APP, CSF_NV_RESET_REASON_ID, 0}

/* DeviceType_ID_XYZ values */
#define DeviceType_ID_GENERIC       0
#define DeviceType_ID_CC1310        1
#define DeviceType_ID_CC1350        2
#define DeviceType_ID_CC2640R2      3
#define DeviceType_ID_CC1312R1      4
#define DeviceType_ID_CC1352R1      5
#define DeviceType_ID_CC1352P1      6
#define DeviceType_ID_CC1352P_2     7
#define DeviceType_ID_CC1352P_4     8
#define DeviceType_ID_CC2642R1      9
#define DeviceType_ID_CC2652R1      10
#define DeviceType_ID_CC2652RB      11

#define board_led_type_LED1 0
#define board_led_type_LED2 0
#define board_led_state_ON  0
#define Board_Led_toggle(led)(void)led;
#define Board_Led_control(led, action) \
(void)led; \
(void)action;

#define Board_Lcd_printf(line, ...) \
/*move curser to line*/ \
fprintf(stderr,"\033[%d;0H", line+1); \
/*clear line */ \
fprintf(stderr,"\033[2K"); \
fprintf(stderr,__VA_ARGS__); \
fprintf(stderr,"\n");

#define Board_LCD_open() \
fprintf(stderr,"\033[2J");

#include <termios.h>

/* Alternatively, board type */
#define KEY_DEVICE_TYPE 'b'
#define KEY_DISASSOCIATE_DEVICE 'd'
#define KEY_GET_OAD_FILE 'f'
#define KEY_LIST_DEVICES 'l'
#define KEY_PERMIT_JOIN 'o'
#define KEY_SELECT_DEVICE 's'
#define KEY_TOGGLE_REQ 't'
#define KEY_FW_UPDATE_REQ_OFFCHIP 'u'
#define KEY_FW_VER_REQ 'v'
#define KEY_FW_UPDATE_REQ_ONCHIP 'w'

#define DEFAULT_OAD_FILE "../../firmware/oad/cc13x0/sensor_oad_cc13x0lp_app.bin"
#define MAX_FILENAME_LENGTH 256
#define SEC_PER_MIN 		60

/******************************************************************************
 External variables
 *****************************************************************************/

/* handle for tracking timeout */
static intptr_t trackingClkHandle;
/* handle for PA trickle timeout */
static intptr_t tricklePAClkHandle;
/* handle for PC timeout */
static intptr_t tricklePCClkHandle;
/* handle for join permit timeout */
static intptr_t joinClkHandle;
/* handle for config request delay */
static intptr_t configClkHandle;
/* handle for broadcast interval */
static intptr_t broadcastClkHandle;

#ifndef IS_HEADLESS
/* Handle for OAD reset request retries timeout */
static intptr_t oadResetReqRetryClkHandle;
#endif

extern intptr_t semaphore0;
/* Non-volatile function pointers */
NVINTF_nvFuncts_t nvFps;

/******************************************************************************
 Local variables
 *****************************************************************************/

static intptr_t collectorSem;
#define Semaphore_post(S)  SEMAPHORE_put(S)

/* NV Function Pointers */
static NVINTF_nvFuncts_t *pNV = NULL;

/* Permit join setting */
static bool permitJoining = false;

static bool started = false;

/* The last saved coordinator frame counter */
static uint32_t lastSavedCoordinatorFrameCounter = 0;

#if defined(MT_CSF)
/*! NV driver item ID for reset reason */
static const NVINTF_itemID_t nvResetId = NVID_RESET;
#endif

#ifndef IS_HEADLESS
enum {
    DisplayLine_product = 0,
    DisplayLine_nwk,
    DisplayLine_sensorStart,
    DisplayLine_sensorEnd = 6,
    DisplayLine_info,
    DisplayLine_cmd,
} DisplayLine;
#else
#endif //IS_HEADLESS

static uint32_t selected_oad_file_id = 0;

/* variables to control offchip and onchip oad flow */
static volatile uint8_t ResetRspNeeded = 0;
static volatile uint8_t ResetRspRcvd = 0;
static volatile uint8_t ResetReqSent = 0;

#ifndef IS_HEADLESS
static uint8_t ResetRetryCount = 0;
static uint8_t ResetReqSendRetry = 0;
static char UpdateKeySim[] = {KEY_FW_UPDATE_REQ_ONCHIP};
#endif


/******************************************************************************
 Global variables
 *****************************************************************************/
/* Key press parameters */
uint8_t Csf_keys;

/* pending Csf_events */
uint16_t Csf_events = 0;

/* Saved CLLC state */
Cllc_states_t savedCllcState = Cllc_states_initWaiting;

/* OAD Duration Timer */
double oadDurationTimer = 0;
/******************************************************************************
 Local function prototypes
 *****************************************************************************/

static void processTrackingTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
static void processPATrickleTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
static void processPCTrickleTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
static void processJoinTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
static void processConfigTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
static void processBroadcastTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);

#ifndef IS_HEADLESS
static void processOadResetReqRetryTimeoutCallback_WRAPPER(intptr_t thandle, intptr_t cookie);
#endif

#ifndef IS_HEADLESS
char* getConsoleCmd(void);
void initConsoleCmd(void);
#endif //!IS_HEADLESS

static void processTrackingTimeoutCallback(UArg a0);
static void processBroadcastTimeoutCallback(UArg a0);
static void processKeyChangeCallback(uint8_t keysPressed);
static void processPATrickleTimeoutCallback(UArg a0);
static void processPCTrickleTimeoutCallback(UArg a0);
static void processJoinTimeoutCallback(UArg a0);
static void processConfigTimeoutCallback(UArg a0);

#ifndef IS_HEADLESS
static void processOadResetReqRetryTimeoutCallback(UArg a0);
#endif

static bool addDeviceListItem(Llc_deviceListItem_t *pItem, bool *pNewDevice);
static void updateDeviceListItem(Llc_deviceListItem_t *pItem);
static int findDeviceListIndex(ApiMac_sAddrExt_t *pAddr);
static int findUnusedDeviceListIndex(void);
static void saveNumDeviceListEntries(uint16_t numEntries);

#ifndef IS_HEADLESS
static bool removeDevice(ApiMac_sAddr_t addr);
#endif

#if defined(TEST_REMOVE_DEVICE)
static void removeTheFirstDevice(void);
#endif

static double getUnixTime(void);

#ifndef IS_HEADLESS
static void startOADResetReqRetryTimer(void);
static void stopOADResetReqRetryTimer(void);
#endif

/******************************************************************************
 Public Functions
 *****************************************************************************/

/*!
 The application calls this function during initialization

 Public function defined in csf.h
 */
void Csf_init(void *sem)
{
    char default_oad_file[256] = DEFAULT_OAD_FILE;

    /* Set default FW image */
    selected_oad_file_id = Collector_updateFwList(default_oad_file);

    /* Initialize the LCD */
    Board_LCD_open();

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_product, "TI Collector");

#if !defined(AUTO_START)
    Board_Lcd_printf(DisplayLine_nwk, "Nwk: Starting");
#endif /* AUTO_START */

#endif //!IS_HEADLESS

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "TI Collector");
#if !defined(AUTO_START)
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Nwk: Starting\n");
#endif /* AUTO_START */

#ifndef IS_HEADLESS
    initConsoleCmd();
#endif //!HEADLESS

    /* Save off the semaphore */
    collectorSem = (intptr_t)sem;

    /* save the application semaphore here */
    /* load the NV function pointers */
    // printf("   >> Initialize the NV Function pointers \n");
    NVOCMP_loadApiPtrs(&nvFps);

    /* Suyash - the code is using pNV var. Using that for now. */
    /* config nv pointer will be read from the mac_config_t... */
    pNV = &nvFps;

    /* Init NV */
    nvFps.initNV(NULL);
}

/*!
 The application must call this function periodically to
 process any Csf_events that this module needs to process.

 Public function defined in csf.h
 */
void Csf_processEvents(void)
{

#if !defined(IS_HEADLESS)

    char *cmdBuff;
    static uint16_t selected_device = 0;
    cmdBuff = getConsoleCmd();

    if(1 == ResetRspNeeded) //Onchip case
    {
        /* simulate the key stoke for fw update */
        if((!cmdBuff) && (((1 == ResetRspRcvd) && (1 == ResetReqSent)) || 1 == ResetReqSendRetry))
        {
            cmdBuff = UpdateKeySim;
        }
    }

    if(cmdBuff)
    {
        Csf_keys = cmdBuff[0];

        if(Csf_keys == KEY_DEVICE_TYPE)
        {
            ApiMac_sAddr_t sAddr;
            Collector_status_t status;

            Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x device type req", selected_device);
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x device type req\n", selected_device);

            sAddr.addr.shortAddr = selected_device;
            sAddr.addrMode = ApiMac_addrType_short;
            status = Collector_sendDeviceTypeRequest(&sAddr);

            if(status == Collector_status_deviceNotFound)
            {
                Board_Lcd_printf(DisplayLine_info, "Info: Device 0x%04x not found", selected_device);
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Device 0x%04x not found\n", selected_device);
            }
            else if(status != Collector_status_success)
            {
                Board_Lcd_printf(DisplayLine_info, "Info: Device type req failed");
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Device type req failed\n");
            }
        }

        if(Csf_keys == KEY_PERMIT_JOIN)
        {
            /* Toggle the permit joining */
            if (permitJoining == true)
            {
                Csf_closeNwk();
            }
            else
            {
                Csf_openNwk();
            }
        }

        if(Csf_keys == KEY_SELECT_DEVICE)
        {
            if(sscanf(cmdBuff, "s0x%hx", &selected_device) < 1)
            {
                sscanf(cmdBuff, "s%hd", &selected_device);
            }

            Board_Lcd_printf(DisplayLine_info, "Info: Selected device 0x%04x", selected_device);
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Selected device 0x%04x\n", selected_device);
        }

        if(Csf_keys == KEY_FW_VER_REQ)
        {
            ApiMac_sAddr_t sAddr;

            Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x FW version req", selected_device);
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x FW version req\n", selected_device);

            sAddr.addr.shortAddr = selected_device;
            sAddr.addrMode = ApiMac_addrType_short;
            Collector_sendFwVersionRequest(&sAddr);
        }

        if((Csf_keys == KEY_FW_UPDATE_REQ_OFFCHIP) || (Csf_keys == KEY_FW_UPDATE_REQ_ONCHIP))
        {
            ApiMac_sAddr_t sAddr;
            Collector_status_t status;

            sAddr.addr.shortAddr = selected_device;
            sAddr.addrMode = ApiMac_addrType_short;

            if(Csf_keys == KEY_FW_UPDATE_REQ_ONCHIP)
            {
                ResetRspNeeded = 1;

            } //onchip_case
            else //offchip_case
            {
                ResetRspNeeded = 0;
            }

            /* for onchip OAD case: we need Reset Rsp to proceed further */
            if((1 == ResetRspNeeded) && (0 == ResetRspRcvd))
            {
                if(ResetReqSent == 1)
                {
                    ResetRetryCount++;
                    Board_Lcd_printf(DisplayLine_info, "Info: Retrying 0x%04x Target Reset - Attempt %i", selected_device, ResetRetryCount);
                    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Retrying 0x%04x Target Reset - Attempt %i\n", selected_device, ResetRetryCount);
                }
                else
                {
                    Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x Target Reset Req", selected_device);
                    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x Target Reset Req\n", selected_device);
                }

                status = Collector_sendResetReq(&sAddr);

                if(status != Collector_status_success || ResetRetryCount >= OAD_RESET_REQ_MAX_RETRIES)
                {
                    ResetReqSent = 0;
                    ResetRetryCount = 0;
                    ResetReqSendRetry = 0;
                    stopOADResetReqRetryTimer();

                    if(status != Collector_status_success)
                    {
                        Board_Lcd_printf(DisplayLine_info, "Info: Sending Target Reset Req failed");
                        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending Target Reset Req failed\n");
                    }
                    else
                    {
                        Board_Lcd_printf(DisplayLine_info, "Info: OAD Failed");
                        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: OAD Failed\n");
                    }

                }
                else if(ResetReqSent == 0)
                {
                    ResetReqSent = 1;
                    startOADResetReqRetryTimer();
                }
                else //ResetReqSent == 1
                {
                    ResetReqSendRetry = 0;
                }

            }


            if((!ResetRspNeeded) || ((ResetRspNeeded)&&(1 == ResetRspRcvd)))
            {
            	/* clear the Reset Req sent and Reset Rsp Received flag to facilate next OAD */
                /* No harm in doing it for offchip oad case as well*/
            	ResetRspRcvd = 0;
            	ResetReqSent = 0;
                ResetRetryCount = 0;
                ResetReqSendRetry = 0;
                stopOADResetReqRetryTimer();

            	Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x FW Update Req", selected_device);
	            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x FW Update Req\n", selected_device);


	            oadDurationTimer = getUnixTime();
	            status = Collector_startFwUpdate(&sAddr, selected_oad_file_id);

	            if(status == Collector_status_invalid_file)
	            {
	                Board_Lcd_printf(DisplayLine_info, "Info: Update req file not found ID:%d", selected_oad_file_id);
	                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Update req file not found ID:%d\n", selected_oad_file_id);
	            }
	            else if(status != Collector_status_success)
	            {
	                Board_Lcd_printf(DisplayLine_info, "Info: Update req failed");
	                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Update req failed\n");
	            }
            }
        }

        if(Csf_keys == KEY_GET_OAD_FILE)
        {
            static char new_oad_file[256] = DEFAULT_OAD_FILE;
            char temp_oad_file[256] = "";

            sscanf(cmdBuff, "f %s", temp_oad_file);

            if(strlen(temp_oad_file) > 0)
            {
                // Verify file exists and we have read permissions
                if(access(temp_oad_file, F_OK | R_OK) != -1)
                {
                    // If file exists, then copy to the static filename and call updateFwList
                    strncpy(new_oad_file, temp_oad_file, strlen(temp_oad_file) + 1);
                    selected_oad_file_id = Collector_updateFwList(new_oad_file);

                    Board_Lcd_printf(DisplayLine_info, "Info: OAD file %s", new_oad_file);
                    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: OAD file %s\n", new_oad_file);
                }
                else
                {
                    Board_Lcd_printf(DisplayLine_info, "Info: Can not read file %s", temp_oad_file);
                    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Can not read file %s\n", temp_oad_file);
                }
            }
            else
            {
                // User is asking what the current file is
                Board_Lcd_printf(DisplayLine_info, "Info: OAD file %s", new_oad_file);
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: OAD file %s\n", new_oad_file);
            }
        }

        if(Csf_keys == KEY_TOGGLE_REQ)
        {
            ApiMac_sAddr_t sAddr;
            Collector_status_t status;

            Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x LED toggle req", selected_device);
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x LED toggle req\n", selected_device);

            sAddr.addr.shortAddr = selected_device;
            sAddr.addrMode = ApiMac_addrType_short;
            status = Csf_sendToggleLedRequest(&sAddr);

            if(status == Collector_status_deviceNotFound)
            {
                Board_Lcd_printf(DisplayLine_info, "Info: Toggle Req device 0x%04x not found", selected_device);
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Toggle Req device 0x%04x not found\n", selected_device);
            }
            else if(status != Collector_status_success)
            {
                Board_Lcd_printf(DisplayLine_info, "Info: Update Req failed");
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Update Req failed\n");
            }
        }

        if(Csf_keys == KEY_LIST_DEVICES)
        {
            ApiMac_sAddr_t sAddr;
            uint16_t devIdx;
            ApiMac_sAddrExt_t pExtAddr;

            for(devIdx = 1; devIdx < CONFIG_MAX_DEVICES; devIdx++)
            {
                sAddr.addr.shortAddr = devIdx;
                sAddr.addrMode = ApiMac_addrType_short;

                if(Collector_findDevice(&sAddr) == Collector_status_success)
                {
                    if(Csf_getDeviceExtended(devIdx, &pExtAddr)) {

                        Board_Lcd_printf(DisplayLine_info,
                            "Short: 0x%04x Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                            devIdx,
                            pExtAddr[7],
                            pExtAddr[6],
                            pExtAddr[5],
                            pExtAddr[4],
                            pExtAddr[3],
                            pExtAddr[2],
                            pExtAddr[1],
                            pExtAddr[0]);

                        LOG_printf(LOG_DBG_COLLECTOR,"Short: 0x%04x Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                            devIdx,
                            pExtAddr[7],
                            pExtAddr[6],
                            pExtAddr[5],
                            pExtAddr[4],
                            pExtAddr[3],
                            pExtAddr[2],
                            pExtAddr[1],
                            pExtAddr[0]);
                    }
                }
            }
        }

        if(Csf_keys == KEY_DISASSOCIATE_DEVICE)
        {
            ApiMac_sAddr_t sAddr;

            Board_Lcd_printf(DisplayLine_info, "Info: Sending 0x%04x disassociation req", selected_device);
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Sending 0x%04x disassociation req\n", selected_device);

            sAddr.addr.shortAddr = selected_device;
            sAddr.addrMode = ApiMac_addrType_short;

            if(!removeDevice(sAddr))
            {
                Board_Lcd_printf(DisplayLine_info, "Info: disassociation req device 0x%04x not found", selected_device);
                LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: disassociation req device 0x%04x not found\n", selected_device);
            }
        }

        /* Clear the key press indication */
        Csf_keys = 0;
    }

    /* Clear the event */
    Util_clearEvent(&Csf_events, CSF_KEY_EVENT);

#endif /* !defined(IS_HEADLESS) */

#if defined(MT_CSF)
    MTCSF_displayStatistics();
#endif
}

#if !defined(IS_HEADLESS)
void initConsoleCmd(void)
{
    struct termios term_attr;

    /* set the terminal to raw mode */
    tcgetattr(fileno(stdin), &term_attr);
    term_attr.c_lflag &= ~(ECHO|ICANON);
    term_attr.c_cc[VTIME] = 0;
    term_attr.c_cc[VMIN] = 0;
    tcsetattr(fileno(stdin), TCSANOW, &term_attr);

}

char* getConsoleCmd(void)
{
    static bool cmdComplete = false;
    static char cmd[256] = {0};
    static int ch;
    static uint8_t cmdIdx = 0;

    if(cmdComplete)
    {
        memset(cmd, 0, 256);
        cmdIdx = 0;
        cmdComplete = false;
    }

    /* read a character from the stdin stream without blocking */
    /*   returns EOF (-1) if no character is available */
    ch = getchar();

    if(ch != -1)
    {
         /* Discard non-ascii characters except new lines */
        if(ch == 0xa || (ch >= 0x20 && ch < 0x7F))
        {
            cmd[cmdIdx] = ch;
        }

        Board_Lcd_printf(DisplayLine_cmd, "cmd: %s", cmd);
        /* cmdIdx will wrap around for the 256Byte buffer */
        if(cmd[cmdIdx] == 0xa)
        {
            cmdComplete = true;
        }
        else
        {
            cmdIdx++;
        }
    }

    if(cmdComplete)
    {
        Board_Lcd_printf(DisplayLine_cmd, "CMD: %s", cmd);
        LOG_printf(LOG_APPSRV_MSG_CONTENT, "CMD: %s\n", cmd);

        return cmd;
    }
    else
    {
        return 0;
    }
}
#endif // !defined(HEADLESS)

/*!
 The application calls this function to retrieve the stored
 network information.

 Public function defined in csf.h
 */
bool Csf_getNetworkInformation(Llc_netInfo_t *pInfo)
{
    if((pNV != NULL) && (pNV->readItem != NULL) && (pInfo != NULL))
    {
        NVINTF_itemID_t id;

        /* Setup NV ID */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_NETWORK_INFO_ID;
        id.subID = 0;

        /* Read Network Information from NV */
        if(pNV->readItem(id, 0, sizeof(Llc_netInfo_t), pInfo) == NVINTF_SUCCESS)
        {
            return(true);
        }
    }
    return(false);
}

/*!
 The application calls this function to indicate that it has
 started or restored the device in a network

 Public function defined in csf.h
 */
void Csf_networkUpdate(bool restored, Llc_netInfo_t *pNetworkInfo)
{
    /* check for valid structure ponter, ignore if not */
    if(pNetworkInfo != NULL)
    {
        if((pNV != NULL) && (pNV->writeItem != NULL))
        {
            NVINTF_itemID_t id;

            /* Setup NV ID */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_NETWORK_INFO_ID;
            id.subID = 0;

            /* Write the NV item */
            pNV->writeItem(id, sizeof(Llc_netInfo_t), pNetworkInfo);
        }

        /* Send info to appClient */
        if(pNetworkInfo != NULL)
        {
             appsrv_networkUpdate(restored, pNetworkInfo);
        }

        started = true;

#ifndef IS_HEADLESS
        Board_Lcd_printf(DisplayLine_nwk, "Nwk: Started");
#endif //IS_HEADLESS
        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Nwk: Started");

        if(pNetworkInfo->fh == false)
        {
#ifndef IS_HEADLESS
            Board_Lcd_printf(DisplayLine_info, "Info: Channel %d", pNetworkInfo->channel);
#endif //IS_HEADLESS
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Channel %d", pNetworkInfo->channel);
        }
        else
        {
#ifndef IS_HEADLESS
            Board_Lcd_printf(DisplayLine_info, "Info: Freq. Hopping");
#endif //IS_HEADLESS
            LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Freq. Hopping");
        }

        Board_Led_control(board_led_type_LED1, board_led_state_ON);

#if defined(MT_CSF)
        MTCSF_networkUpdateIndCB();
#endif
    }
}

/*!
 The application calls this function to indicate that a device
 has joined the network.

 Public function defined in csf.h
 */
ApiMac_assocStatus_t Csf_deviceUpdate(ApiMac_deviceDescriptor_t *pDevInfo,
                                      ApiMac_capabilityInfo_t *pCapInfo)
{
    ApiMac_assocStatus_t status = ApiMac_assocStatus_success;
    ApiMac_sAddr_t extAddr;

    /* flag which will be updated based on if the device joining is
     a new device or already existing one */
    bool newDevice;
    extAddr.addrMode = ApiMac_addrType_extended;
    memcpy(&extAddr.addr.extAddr, &pDevInfo->extAddress, APIMAC_SADDR_EXT_LEN);

    /* Save the device information */
    Llc_deviceListItem_t dev;

    memcpy(&dev.devInfo, pDevInfo, sizeof(ApiMac_deviceDescriptor_t));
    memcpy(&dev.capInfo, pCapInfo, sizeof(ApiMac_capabilityInfo_t));
    dev.rxFrameCounter = 0;

    if(addDeviceListItem(&dev, &newDevice) == false)
    {
#ifdef NV_RESTORE
        status = ApiMac_assocStatus_panAtCapacity;

        LOG_printf(LOG_ERROR,"Failed: 0x%04x\n", pDevInfo->shortAddress);

#ifndef IS_HEADLESS
        Board_Lcd_printf(DisplayLine_info, "Info: Join Failed 0x%04x", pDevInfo->shortAddress);
#endif //!IS_HEADLESS

#else /* NV_RESTORE */
        status = ApiMac_assocStatus_success;

        LOG_printf(LOG_DBG_COLLECTOR_RAW, "Joined: Short: 0x%04x Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
            pDevInfo->shortAddress,
            extAddr.addr.extAddr[7],
            extAddr.addr.extAddr[6],
            extAddr.addr.extAddr[5],
            extAddr.addr.extAddr[4],
            extAddr.addr.extAddr[3],
            extAddr.addr.extAddr[2],
            extAddr.addr.extAddr[1],
            extAddr.addr.extAddr[0]);

#ifndef IS_HEADLESS
        Board_Lcd_printf(DisplayLine_info, "Info: Joined 0x%04x", pDevInfo->shortAddress);
#endif //!IS_HEADLESS

#endif /* NV_RESTORE */
    }
    else //addDeviceListItem() returned true
    {
        if(true == newDevice)
        {
            /* Send update to the appClient */
            LOG_printf(LOG_APPSRV_MSG_CONTENT,
                       "sending device update info to appsrv\n");
            appsrv_deviceUpdate(&dev);

            LOG_printf(LOG_DBG_COLLECTOR_RAW, "Joined: Short: 0x%04x Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                pDevInfo->shortAddress,
                extAddr.addr.extAddr[7],
                extAddr.addr.extAddr[6],
                extAddr.addr.extAddr[5],
                extAddr.addr.extAddr[4],
                extAddr.addr.extAddr[3],
                extAddr.addr.extAddr[2],
                extAddr.addr.extAddr[1],
                extAddr.addr.extAddr[0]);

#ifndef IS_HEADLESS
            Board_Lcd_printf(DisplayLine_info, "Info: Joined 0x%04x", pDevInfo->shortAddress);
#endif //!IS_HEADLESS
        } //end of if newDevice == true
        else // not a new device : device is rejoining
        {
            /* Send update to the appClient */
            LOG_printf(LOG_APPSRV_MSG_CONTENT,
                       "sending device update info to appsrv\n");
            appsrv_deviceUpdate(&dev);

            LOG_printf(LOG_DBG_COLLECTOR_RAW, "Re-Joined: Short: 0x%04x Extended: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                pDevInfo->shortAddress,
                extAddr.addr.extAddr[7],
                extAddr.addr.extAddr[6],
                extAddr.addr.extAddr[5],
                extAddr.addr.extAddr[4],
                extAddr.addr.extAddr[3],
                extAddr.addr.extAddr[2],
                extAddr.addr.extAddr[1],
                extAddr.addr.extAddr[0]);

#ifndef IS_HEADLESS
            Board_Lcd_printf(DisplayLine_info, "Info: Re-Joined 0x%04x", pDevInfo->shortAddress);
#endif //!IS_HEADLESS

        }//end of not a new device
    }//end of addDeviceListItem() returned true

#if defined(MT_CSF)
    MTCSF_deviceUpdateIndCB(pDevInfo, pCapInfo);
#endif

    /* Return the status of the joining device */
    return (status);
}


/*!
 The application calls this function to indicate that a device
 is no longer active in the network.

 Public function defined in csf.h
 */
void Csf_deviceNotActiveUpdate(ApiMac_deviceDescriptor_t *pDevInfo,
bool timeout)
{

    /* send update to the appClient */
    LOG_printf(LOG_APPSRV_MSG_CONTENT,
               "!Responding: 0x%04x\n",
               pDevInfo->shortAddress);

    appsrv_deviceNotActiveUpdate(pDevInfo, timeout);

    LOG_printf(LOG_DBG_COLLECTOR,
                "inactive: pan: 0x%04x short: 0x%04x ext: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                pDevInfo->panID,
                pDevInfo->shortAddress,
                pDevInfo->extAddress[0],
                pDevInfo->extAddress[1],
                pDevInfo->extAddress[2],
                pDevInfo->extAddress[3],
                pDevInfo->extAddress[4],
                pDevInfo->extAddress[5],
                pDevInfo->extAddress[6],
                pDevInfo->extAddress[7]);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: No response 0x%04x", pDevInfo->shortAddress);
#endif //IS_HEADLESS

#if defined(MT_CSF)
    MTCSF_deviceNotActiveIndCB(pDevInfo, timeout);
#endif

}

/*!
 The application calls this function to indicate that a device
 has responded to a Config Request.

 Public function defined in csf.h
 */
void Csf_deviceConfigUpdate(ApiMac_sAddr_t *pSrcAddr, int8_t rssi,
                            Smsgs_configRspMsg_t *pMsg)
{
    /* send update to the appClient */
    appsrv_deviceConfigUpdate(pSrcAddr,rssi,pMsg);
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "ConfigRsp: 0x%04x\n", pSrcAddr->addr.shortAddr);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: ConfigRsp 0x%04x", pSrcAddr->addr.shortAddr);
#endif //IS_HEADLESS

#if defined(MT_CSF)
    MTCSF_configResponseIndCB(pSrcAddr, rssi, pMsg);
#endif

}

/*!
 * @brief       Display divice short address when config data is received
 *
 * @param       pSrcAddr - short address of the device that sent the message
 */
void Csf_deviceConfigDisplay(ApiMac_sAddr_t *pSrcAddr)
{
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "ConfigRsp: 0x%04x\n", pSrcAddr->addr.shortAddr);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: ConfigRsp 0x%04x", pSrcAddr->addr.shortAddr);
#endif //IS_HEADLESS

}


/*!
 The application calls this function to indicate that a device
 has reported sensor data.

 Public function defined in csf.h
 */
void Csf_deviceSensorDataUpdate(ApiMac_sAddr_t *pSrcAddr, int8_t rssi,
                                Smsgs_sensorMsg_t *pMsg)
{
#ifndef IS_HEADLESS
    uint16_t sensorData = pMsg->tempSensor.ambienceTemp;

    if((DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1)) < DisplayLine_sensorEnd)
    {
        Board_Lcd_printf(DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1), "Sensor 0x%04x: Temp %d, RSSI %d",
                        pSrcAddr->addr.shortAddr, sensorData, rssi);
    }
    else
    {
        Board_Lcd_printf(DisplayLine_sensorEnd, "Sensor 0x%04x: Temp %d, RSSI %d",
                        pSrcAddr->addr.shortAddr, sensorData, rssi);
    }
#endif //!IS_HEADLESS

    /* send data to the appClient */
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x\n", pSrcAddr->addr.shortAddr);

    appsrv_deviceSensorDataUpdate(pSrcAddr, rssi, pMsg);

    Board_Led_toggle(board_led_type_LED2);

#if defined(MT_CSF)
    MTCSF_sensorUpdateIndCB(pSrcAddr, rssi, pMsg);
#endif
}

/*!
 * @brief       The application calls this function to print out the reported
 *              device type
 *
 * @param       pSrcAddr - short address of the device that sent the message
 * @param       deviceFamilyID - the integer ID of the device family
 * @param       deviceTypeID - the integer ID of the board/device
 *
 * Public function defined in csf.h
 */
void Csf_deviceSensorDeviceTypeResponseUpdate(ApiMac_sAddr_t *pSrcAddr, uint8_t deviceFamilyID,
                                              uint8_t deviceTypeID)
{
    char* deviceStr;

    switch (deviceTypeID)
    {
        case DeviceType_ID_CC1310:
            deviceStr = "cc1310";
            break;
        case DeviceType_ID_CC1350:
            deviceStr = "cc1350";
            break;
        case DeviceType_ID_CC2640R2:
            deviceStr = "cc2640r2";
            break;
        case DeviceType_ID_CC1312R1:
            deviceStr = "cc1312r1";
            break;
        case DeviceType_ID_CC1352R1:
            deviceStr = "cc1352r1";
            break;
        case DeviceType_ID_CC1352P1:
            deviceStr = "cc1352p1";
            break;
        case DeviceType_ID_CC1352P_2:
            deviceStr = "cc1352p2";
            break;
        case DeviceType_ID_CC1352P_4:
            deviceStr = "cc1352p4";
            break;
        case DeviceType_ID_CC2642R1:
            deviceStr = "cc2642r1";
            break;
        case DeviceType_ID_CC2652R1:
            deviceStr = "cc2652r1";
            break;
        case DeviceType_ID_CC2652RB:
            deviceStr = "cc2652rb";
            break;
        default:
            deviceStr = "generic";
            break;
    }

#ifndef IS_HEADLESS
    if((DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1)) < DisplayLine_sensorEnd)
    {
        Board_Lcd_printf(DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1),
                         "Sensor 0x%04x: Device=%s, DeviceFamilyID=%i, DeviceTypeID=%i",
                         pSrcAddr->addr.shortAddr, deviceStr, deviceFamilyID, deviceTypeID);
    }
    else
    {
        Board_Lcd_printf(DisplayLine_sensorEnd,
                         "Sensor 0x%04x: Device=%s, DeviceFamilyID=%i, DeviceTypeID=%i",
                         pSrcAddr->addr.shortAddr, deviceStr, deviceFamilyID, deviceTypeID);
    }
#endif //!IS_HEADLESS

    /* send data to the appClient */
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x: Device=%s, DeviceFamilyID=%i, DeviceTypeID=%i\n",
               pSrcAddr->addr.shortAddr, deviceStr, deviceFamilyID, deviceTypeID);
}


/*!
 * @brief       Display Sensor device and data
 *
 * @param       pDataInd - pointer to the data indication information
 */
void Csf_deviceSensorDisplay(ApiMac_mcpsDataInd_t *pDataInd)
{

    ApiMac_sAddr_t *pSrcAddr = &pDataInd->srcAddr;

#ifndef IS_HEADLESS
    int8_t rssi = pDataInd->rssi;
    uint8_t *pBuf = pDataInd->msdu.p;
    uint16_t SensorData;

    // go to the first sensor data from msdu.p
    pBuf ++;   // cmdId
    pBuf += SMGS_SENSOR_EXTADDR_LEN;
    pBuf += 2;  // frameControl
    SensorData = Util_buildUint16(pBuf[0], pBuf[1]);  // first sensor data



    if((DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1)) < DisplayLine_sensorEnd)
    {
        Board_Lcd_printf(DisplayLine_sensorStart + (pSrcAddr->addr.shortAddr - 1), "Sensor 0x%04x: Temp %d, RSSI %d",
                        pSrcAddr->addr.shortAddr, SensorData, rssi);
    }
    else
    {
        Board_Lcd_printf(DisplayLine_sensorEnd, "Sensor 0x%04x: Temp %d, RSSI %d",
                        pSrcAddr->addr.shortAddr, SensorData, rssi);
    }
#endif //!IS_HEADLESS

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x\n", pSrcAddr->addr.shortAddr);
}

/*!
 The application calls this function to indicate that a device
 has disassociated.

 Public function defined in csf.h
 */
void Csf_deviceDisassocUpdate( ApiMac_sAddr_t *pSrcAddr )
{
    if(pSrcAddr->addrMode == ApiMac_addrType_extended)
    {
#ifndef IS_HEADLESS
        Board_Lcd_printf(DisplayLine_info, "Info: Disassociate ind from %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                            pSrcAddr->addr.extAddr[7], pSrcAddr->addr.extAddr[6],
                            pSrcAddr->addr.extAddr[5], pSrcAddr->addr.extAddr[4],
                            pSrcAddr->addr.extAddr[3], pSrcAddr->addr.extAddr[2],
                            pSrcAddr->addr.extAddr[1], pSrcAddr->addr.extAddr[0]);
#endif

        LOG_printf(LOG_DBG_COLLECTOR,
                    "Info: Disassociate ind from %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                     pSrcAddr->addr.extAddr[0],
                     pSrcAddr->addr.extAddr[1],
                     pSrcAddr->addr.extAddr[2],
                     pSrcAddr->addr.extAddr[3],
                     pSrcAddr->addr.extAddr[4],
                     pSrcAddr->addr.extAddr[5],
                     pSrcAddr->addr.extAddr[6],
                     pSrcAddr->addr.extAddr[7]);
    }
    else // Use Short address
    {
#ifndef IS_HEADLESS
        Board_Lcd_printf(DisplayLine_info, "Info: Disassociate ind from 0x%04x", pSrcAddr->addr.shortAddr);
#endif //!IS_HEADLESS

        LOG_printf(LOG_DBG_COLLECTOR, "Info: Disassociate ind from 0x%04x\n", pSrcAddr->addr.shortAddr);
    }
}

/*!
 The application calls this function to indicate that a device
 has reported its FW version.

 Public function defined in csf.h
 */
void Csf_deviceSensorFwVerUpdate( uint16_t srcAddr, char *fwVerStr)
{
    Board_Led_toggle(board_led_type_LED2);

#ifndef IS_HEADLESS
    if((DisplayLine_sensorStart + (srcAddr - 1)) < DisplayLine_sensorEnd)
    {
        Board_Lcd_printf(DisplayLine_sensorStart + (srcAddr - 1), "Sensor 0x%04x: FW Ver %s",
                        srcAddr, fwVerStr);
    }
    else
    {
        Board_Lcd_printf(DisplayLine_sensorEnd, "Sensor 0x%04x: FW Ver %s",
                        srcAddr, fwVerStr);
    }
#endif //!IS_HEADLESS

        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x: FW Ver %s\n",
                        srcAddr, fwVerStr);
}

/*!
 The application calls this function to indicate that a device
 has reported its FW version.

 Public function defined in csf.h
 */
void Csf_deviceSensorOadUpdate( uint16_t srcAddr, uint16_t imgId, uint16_t blockNum, uint16_t NumBlocks)
{
    Board_Led_toggle(board_led_type_LED2);

	static char fileName[MAX_FILENAME_LENGTH];
	static int32_t currImgId = -1;

	if (currImgId != imgId)
	{
		currImgId = imgId;
		Collector_getFileName(currImgId, fileName, MAX_FILENAME_LENGTH);
	}

#ifndef IS_HEADLESS
    uint8_t displayLine = 0;

    if((DisplayLine_sensorStart + (srcAddr - 1)) < DisplayLine_sensorEnd)
    {
        displayLine = (DisplayLine_sensorStart + (srcAddr - 1));
    }
    else
    {
        displayLine = DisplayLine_sensorEnd;
    }
#endif

    double currentTime = getUnixTime();
    double totalSeconds = currentTime - oadDurationTimer;
    int seconds = (int)totalSeconds % SEC_PER_MIN;
    int minutes = (int)totalSeconds / SEC_PER_MIN;

    if((blockNum + 1) >= NumBlocks)
    {
#ifndef IS_HEADLESS
        Board_Lcd_printf(displayLine, "Sensor 0x%04x: OAD completed. Total transfer duration: %02d:%02d",
                        srcAddr, minutes, seconds);
#endif

        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x: OAD completed. Total transfer duration: %02d:%02d",
                        srcAddr, minutes, seconds);
    }
    else
    {
#ifndef IS_HEADLESS
        Board_Lcd_printf(displayLine, "Sensor 0x%04x: Transfering %s - block %d of %d | Duration %02d:%02d",
                srcAddr, fileName, blockNum + 1, NumBlocks, minutes, seconds);
#endif

        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x: Transfering %s, block %d of %d\n",
                srcAddr, fileName, blockNum + 1, NumBlocks);
    }
}

/*!
  The application calls this function to continue with FW update for on-chip OAD

 Public function defined in csf.h
 */
void Csf_deviceSensorOadResetRspRcvd(uint16_t srcAddr)
{
    if(ResetReqSent)
    {
#ifndef IS_HEADLESS
        if((DisplayLine_sensorStart + (srcAddr - 1)) < DisplayLine_sensorEnd)
        {
            Board_Lcd_printf(DisplayLine_sensorStart + (srcAddr - 1), "Sensor 0x%04x: Reset Rsp Rxed",
                            srcAddr);
        }
        else
        {
            Board_Lcd_printf(DisplayLine_sensorEnd, "Sensor 0x%04x: Reset Rsp Rxed",
                            srcAddr);
        }
#endif //!IS_HEADLESS

        LOG_printf(LOG_APPSRV_MSG_CONTENT, "Sensor 0x%04x: Reset Rsp Rxed",
                        srcAddr);

        ResetRspRcvd = 1;
        Util_setEvent(&Csf_events, CSF_KEY_EVENT);
    }
}


/*!
 The application calls this function to toggle an LED.

 Public function defined in csf.h
 */
void Csf_identifyLED(uint16_t identifyTime, uint16_t shortAddr)
{
#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Identify LED Request Received. Sensor Address: 0x%04x", shortAddr);
#endif //!IS_HEADLESS

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Identify LED Request Received. Sensor Address: 0x%04x", shortAddr);
}

/*!
 The application calls this function to indicate that a device
 set a Toggle LED Response message.

 Public function defined in csf.h
 */
void Csf_toggleResponseReceived(ApiMac_sAddr_t *pSrcAddr, bool ledState)
{
#if defined(MT_CSF)
    uint16_t shortAddr = 0xFFFF;

    Board_Lcd_printf(DisplayLine_info, "Info: Device 0x%04x LED toggle rsp received", selected_device);
    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: Device 0x%04x LED toggle rsp received\n", selected_device);

    if(pSrcAddr)
    {
        if(pSrcAddr->addrMode == ApiMac_addrType_short)
        {
            shortAddr = pSrcAddr->addr.shortAddr;
        }
        else
        {
            /* Convert extended to short addr */
            shortAddr = Csf_getDeviceShort(&pSrcAddr->addr.extAddr);
        }
    }
    MTCSF_deviceToggleIndCB(shortAddr, ledState);
#endif
}

/*!
 The application calls this function to indicate that the
 Coordinator's state has changed.

 Public function defined in csf.h
 */
void Csf_stateChangeUpdate(Cllc_states_t state)
{
    /* Save the state to be used later */
    savedCllcState = state;

#if defined(MT_CSF)
    MTCSF_stateChangeIndCB(state);
#endif

    /* Send the update to appClient */
    LOG_printf(LOG_APPSRV_MSG_CONTENT,
               "stateChangeUpdate, newstate: (%d) %s\n",
               (int)(state), CSF_cllc_statename(state));
    appsrv_stateChangeUpdate(state);
}

/*!
 The application calls this function to reinitialize
 the MAC attributes on the CoProcessor after a reset

 Public function defined in csf.h
 */
void Csf_restoreMacAttributes(void)
{
    /* Set PHY ID */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_phyCurrentDescriptorId,
                           (uint8_t)CONFIG_PHY_ID);
    /* Set Channel Page */
    ApiMac_mlmeSetReqUint8(ApiMac_attribute_channelPage,
                           (uint8_t)CONFIG_CHANNEL_PAGE);
    /* Set RX On Idle */
    ApiMac_mlmeSetReqBool(ApiMac_attribute_RxOnWhenIdle,true);

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
    /* Set Transmit Power */
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

    if(CONFIG_FH_ENABLE)
    {
        uint8_t excludeChannels[APIMAC_154G_CHANNEL_BITMAP_SIZ];
        uint8_t sizeOfChannelMask, idx;
        uint8_t configChannelMask[APIMAC_154G_CHANNEL_BITMAP_SIZ];
        memcpy( configChannelMask, linux_CONFIG_FH_CHANNEL_MASK, sizeof(configChannelMask) );

        /* Always set association permit to 1 for FH */
        ApiMac_mlmeSetReqBool(ApiMac_attribute_associatePermit, true);

        /* set PIB to FH coordinator */
        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_unicastChannelFunction, 2);
        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_broadcastChannelFunction,
                                 2);
        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_unicastDwellInterval,
                                 CONFIG_DWELL_TIME);
        ApiMac_mlmeSetFhReqUint8(ApiMac_FHAttribute_broadcastDwellInterval,
                                 FH_BROADCAST_DWELL_TIME);

        ApiMac_mlmeSetFhReqUint32(ApiMac_FHAttribute_BCInterval,
                                  (FH_BROADCAST_INTERVAL >> 1));

         /* set up the number of NON-sleep and sleep device
         * the order is important. Need to set up the number of non-sleep first
         */

        ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_numNonSleepDevice,
                                 FH_NUM_NON_SLEEPY_HOPPING_NEIGHBORS);
        ApiMac_mlmeSetFhReqUint16(ApiMac_FHAttribute_numSleepDevice,
                                 FH_NUM_NON_SLEEPY_FIXED_CHANNEL_NEIGHBORS);

        /* set Exclude Channels */
        sizeOfChannelMask = sizeof(configChannelMask)/sizeof(uint8_t);
        if(sizeOfChannelMask > APIMAC_154G_CHANNEL_BITMAP_SIZ)
        {
            sizeOfChannelMask = APIMAC_154G_CHANNEL_BITMAP_SIZ;
        }
        memset(excludeChannels, 0, APIMAC_154G_CHANNEL_BITMAP_SIZ);
        for(idx = 0; idx < sizeOfChannelMask; idx++)
        {
            excludeChannels[idx] = ~configChannelMask[idx];
        }
        ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_unicastExcludedChannels,
                                 excludeChannels);
        ApiMac_mlmeSetFhReqArray(ApiMac_FHAttribute_broadcastExcludedChannels,
                                 excludeChannels);
    }
}

/* Wrappers for Callbacks*/
static void processConfigTimeoutCallback_WRAPPER(intptr_t timer_handle,
                         intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processConfigTimeoutCallback(0);
}

static void processJoinTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                               intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processJoinTimeoutCallback(0);
}

static void processPATrickleTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                                    intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processPATrickleTimeoutCallback(0);
}

static void processPCTrickleTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                                    intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processPCTrickleTimeoutCallback(0);
}

/* Wrap HLOS to embedded callback */
static void processTrackingTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                                  intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processTrackingTimeoutCallback(0);
}

/* Wrap HLOS to embedded callback */
static void processBroadcastTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                                  intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processBroadcastTimeoutCallback(0);
}

#ifndef IS_HEADLESS
static void processOadResetReqRetryTimeoutCallback_WRAPPER(intptr_t timer_handle,
                                                  intptr_t cookie)
{
    (void)timer_handle;
    (void)cookie;
    processOadResetReqRetryTimeoutCallback(0);
}
#endif

/*!
 Initialize the tracking clock.

 Public function defined in csf.h
 */
void Csf_initializeTrackingClock(void)
{
    trackingClkHandle = TIMER_CB_create("trackingTimer",
        processTrackingTimeoutCallback_WRAPPER,
        0,
        TRACKING_INIT_TIMEOUT_VALUE,
        false);
}

/*!
 Initialize the broadcast cmd clock.

 Public function defined in csf.h
 */
void Csf_initializeBroadcastClock(void)
{
    broadcastClkHandle = TIMER_CB_create("broadcastTimer",
        processBroadcastTimeoutCallback_WRAPPER,
        0,
        TRACKING_INIT_TIMEOUT_VALUE,
        false);
}

/*!
 Initialize the trickle clock.

 Public function defined in csf.h
 */
void Csf_initializeTrickleClock(void)
{
    tricklePAClkHandle =
        TIMER_CB_create(
            "paTrickleTimer",
            processPATrickleTimeoutCallback_WRAPPER,
             0,
                TRICKLE_TIMEOUT_VALUE,
                false);

    tricklePCClkHandle =
        TIMER_CB_create(
            "pcTrickleTimer",
            processPCTrickleTimeoutCallback_WRAPPER,
             0,
            TRICKLE_TIMEOUT_VALUE,
        false);
}

/*!
 Initialize the clock for join permit attribute.

 Public function defined in csf.h
 */
void Csf_initializeJoinPermitClock(void)
{
    /* Initialize join permit timer */
    joinClkHandle =
        TIMER_CB_create(
        "joinTimer",
        processJoinTimeoutCallback_WRAPPER,
        0,
        JOIN_TIMEOUT_VALUE,
        false);
}

/*!
 Initialize the clock for config request delay

 Public function defined in csf.h
 */
void Csf_initializeConfigClock(void)
{
    configClkHandle =
        TIMER_CB_create(
            "configTimer",
            processConfigTimeoutCallback_WRAPPER,
            0,
            CONFIG_TIMEOUT_VALUE,
            false );
}

/*!
 Initialize the clock for identify timeout

 Public function defined in csf.h
 */
void Csf_initializeIdentifyClock(void)
{

}

/*!
 Set the tracking clock.

 Public function defined in csf.h
 */
void Csf_setTrackingClock(uint32_t trackingTime)
{
    /* Stop the Tracking timer */

    TIMER_CB_destroy(trackingClkHandle);
    trackingClkHandle = 0;

    /* Setup timer */
    if(trackingTime != 0)
    {
        trackingClkHandle =
            TIMER_CB_create(
                "trackingTimer",
                processTrackingTimeoutCallback_WRAPPER,
                0,
                trackingTime,
                false);
    }
}


/*!
 Set the broadcast clock.

 Public function defined in csf.h
 */
void Csf_setBroadcastClock(uint32_t broadcastTime)
{
    /* Stop the Broadcast timer */
    TIMER_CB_destroy(broadcastClkHandle);
    broadcastClkHandle = 0;

    /* Setup timer */
    if(broadcastTime != 0)
    {
        broadcastClkHandle =
            TIMER_CB_create(
                "broadcastTimer",
                processBroadcastTimeoutCallback_WRAPPER,
                0,
                broadcastTime,
                false);
    }
}


/*!
 Set the trickle clock.

 Public function defined in csf.h
 */
void Csf_setTrickleClock(uint32_t trickleTime, uint8_t frameType)
{
    uint16_t randomNum = 0;
    uint16_t randomTime = 0;

    if(trickleTime > 0)
    {
        randomNum = ((ApiMac_randomByte() << 8) + ApiMac_randomByte());
        randomTime = (trickleTime >> 1) +
                      (randomNum % (trickleTime >> 1));
    }

    if(frameType == ApiMac_wisunAsyncFrame_advertisement)
    {
        /* ALWAYS stop (avoid race conditions) */
        TIMER_CB_destroy(tricklePAClkHandle);
        tricklePAClkHandle = 0;

    /* then create new, only if needed */
        if(trickleTime > 0)
        {
            /* Setup timer */
            tricklePAClkHandle = TIMER_CB_create(
                "paTrickleTimer",
                    processPATrickleTimeoutCallback_WRAPPER,
                    0,
                    randomTime,
                    false);
        }
    }
    else if(frameType == ApiMac_wisunAsyncFrame_config)
    {
        /* Always stop */
        TIMER_CB_destroy(tricklePCClkHandle);
        tricklePCClkHandle = 0;
    /* and recreate only if needed */
        if(trickleTime > 0)
        {
            /* Setup timer */
            tricklePCClkHandle =
                TIMER_CB_create(
                "pcTrickleTimer",
                processPCTrickleTimeoutCallback_WRAPPER,
                0,
                trickleTime, false);
        }
    }
}

/*!
 Set the clock join permit attribute.

 Public function defined in csf.h
 */
void Csf_setJoinPermitClock(uint32_t joinDuration)
{
    /* Always stop the join timer */
    TIMER_CB_destroy(joinClkHandle);
    joinClkHandle = 0;

    /* Setup timer */
    if(joinDuration != 0)
    {
        joinClkHandle =
            TIMER_CB_create("joinTimer",
                processJoinTimeoutCallback_WRAPPER,
                0,
                joinDuration, false);
    }
}

/*!
 Set the clock config request delay.

 Public function defined in csf.h
 */
void Csf_setConfigClock(uint32_t delay)
{
    /* Always destroy */
    TIMER_CB_destroy( configClkHandle );
    configClkHandle = 0;
    /* and create if needed */
    if( delay != 0 ){
        configClkHandle =
            TIMER_CB_create( "configClk",
                             processConfigTimeoutCallback_WRAPPER,
                             0,
                             delay,
                             false );
    }
}

/*!
 Read the number of device list items stored

 Public function defined in csf.h
 */
uint16_t Csf_getNumDeviceListEntries(void)
{
    uint16_t numEntries = 0;

    if(pNV != NULL)
    {
        NVINTF_itemID_t id;
        uint8_t stat;

        /* Setup NV ID for the number of entries in the device list */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_DEVICELIST_ENTRIES_ID;
        id.subID = 0;

        /* Read the number of device list items from NV */
        stat = pNV->readItem(id, 0, sizeof(uint16_t), &numEntries);
        if(stat != NVINTF_SUCCESS)
        {
            numEntries = 0;
        }
    }
    return (numEntries);
}

/*!
 Find the short address from a given extended address

 Public function defined in csf.h
 */
uint16_t Csf_getDeviceShort(ApiMac_sAddrExt_t *pExtAddr)
{
    Llc_deviceListItem_t item;
    ApiMac_sAddr_t devAddr;
    uint16_t shortAddr = CSF_INVALID_SHORT_ADDR;

    devAddr.addrMode = ApiMac_addrType_extended;
    memcpy(&devAddr.addr.extAddr, pExtAddr, sizeof(ApiMac_sAddrExt_t));

    if(Csf_getDevice(&devAddr,&item))
    {
        shortAddr = item.devInfo.shortAddress;
    }

    return(shortAddr);
}

/*!
 Find the extended address from a given short address

 Public function defined in csf.h
 */
bool Csf_getDeviceExtended(uint16_t shortAddr, ApiMac_sAddrExt_t *pExtAddr)
{
    Llc_deviceListItem_t item;
    ApiMac_sAddr_t devAddr;
    bool ret = false;

    devAddr.addrMode = ApiMac_addrType_short;
    devAddr.addr.shortAddr = shortAddr;

    if(Csf_getDevice(&devAddr,&item))
    {
        /* Copy found extended address */
        memcpy(pExtAddr, &item.devInfo.extAddress, sizeof(ApiMac_sAddrExt_t));
        ret = true;
    }

    return(ret);
}

/*!
 Find entry in device list

 Public function defined in csf.h
 */
bool Csf_getDevice(ApiMac_sAddr_t *pDevAddr, Llc_deviceListItem_t *pItem)
{
    if((pNV != NULL) && (pItem != NULL))
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;
            id.subID = 0;
            /* Read Network Information from NV */
            if(pDevAddr->addrMode == ApiMac_addrType_short)
            {
                pNV->readContItem(id, 0, sizeof(Llc_deviceListItem_t), pItem,
                                      sizeof(uint16_t),
                                      (uint16_t)((unsigned long)&pItem->devInfo.shortAddress-(unsigned long)&pItem->devInfo.panID),
                                      &pDevAddr->addr.shortAddr, &id.subID);
            }
            else
            {
                pNV->readContItem(id, 0, sizeof(Llc_deviceListItem_t), pItem,
                                      APIMAC_SADDR_EXT_LEN,
                                      (uint16_t)((unsigned long)&pItem->devInfo.extAddress-(unsigned long)&pItem->devInfo.panID),
                                      &pDevAddr->addr.extAddr, &id.subID);
            }


            if(id.subID != CSF_INVALID_SUBID)
            {
                return(true);
            }
        }
    }
    return (false);
}

/*!
 Find entry in device list

 Public function defined in csf.h
 */
bool Csf_getDeviceItem(uint16_t devIndex, Llc_deviceListItem_t *pItem)
{
    if((pNV != NULL) && (pItem != NULL))
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;
            uint8_t stat;
            int subId = 0;
            int readItems = 0;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;

            while((readItems < numEntries) && (subId
                                               < CSF_MAX_DEVICELIST_IDS))
            {
                Llc_deviceListItem_t item;

                id.subID = (uint16_t)subId;

                /* Read Network Information from NV */
                stat = pNV->readItem(id, 0, sizeof(Llc_deviceListItem_t),
                                     &item);
                if(stat == NVINTF_SUCCESS)
                {
                    if(readItems == devIndex)
                    {
                        memcpy(pItem, &item, sizeof(Llc_deviceListItem_t));
                        return (true);
                    }
                    readItems++;
                }
                subId++;
            }
        }
    }

    return (false);
}

/*!
 Csf implementation for memory allocation

 Public function defined in csf.h
 */
void *Csf_malloc(uint16_t size)
{
    return malloc(size);
}

/*!
 Csf implementation for memory de-allocation

 Public function defined in csf.h
 */
void Csf_free(void *ptr)
{
    free(ptr);
}

/*!
 Update the Frame Counter

 Public function defined in csf.h
 */
void Csf_updateFrameCounter(ApiMac_sAddr_t *pDevAddr, uint32_t frameCntr)
{
    if((pNV != NULL) && (pNV->writeItem != NULL))
    {
        if(pDevAddr == NULL)
        {
            /* Update this device's frame counter */
            if((frameCntr >=
                (lastSavedCoordinatorFrameCounter + FRAME_COUNTER_SAVE_WINDOW)))
            {
                NVINTF_itemID_t id;

                /* Setup NV ID */
                id.systemID = NVINTF_SYSID_APP;
                id.itemID = CSF_NV_FRAMECOUNTER_ID;
                id.subID = 0;

                /* Write the NV item */
                if(pNV->writeItem(id, sizeof(uint32_t), &frameCntr)
                                == NVINTF_SUCCESS)
                {
                    lastSavedCoordinatorFrameCounter = frameCntr;
                }
            }
        }
        else
        {
            /* Child frame counter update */
            Llc_deviceListItem_t devItem;

            /* Is the device in our database? */
            if(Csf_getDevice(pDevAddr, &devItem))
            {
                /*
                 Don't save every update, only save if the new frame
                 counter falls outside the save window.
                 */
                if((devItem.rxFrameCounter + FRAME_COUNTER_SAVE_WINDOW)
                                <= frameCntr)
                {
                    /* Update the frame counter */
                    devItem.rxFrameCounter = frameCntr;
                    updateDeviceListItem(&devItem);
                }
            }
        }
    }
}

/*!
 Get the Frame Counter

 Public function defined in csf.h
 */
bool Csf_getFrameCounter(ApiMac_sAddr_t *pDevAddr, uint32_t *pFrameCntr)
{
    /* Check for valid pointer */
    if(pFrameCntr != NULL)
    {
        /*
         A pDevAddr that is null means to get the frame counter for this device
         */
        if(pDevAddr == NULL)
        {
            if((pNV != NULL) && (pNV->readItem != NULL))
            {
                NVINTF_itemID_t id;

                /* Setup NV ID */
                id.systemID = NVINTF_SYSID_APP;
                id.itemID = CSF_NV_FRAMECOUNTER_ID;
                id.subID = 0;

                /* Read Network Information from NV */
                if(pNV->readItem(id, 0, sizeof(uint32_t), pFrameCntr)
                                == NVINTF_SUCCESS)
                {
                    /* Set to the next window */
                    *pFrameCntr += FRAME_COUNTER_SAVE_WINDOW;
                    return(true);
                }
                else
                {
                    /*
                     Wasn't found, so write 0, so the next time it will be
                     greater than 0
                     */
                    uint32_t fc = 0;

                    /* Setup NV ID */
                    id.systemID = NVINTF_SYSID_APP;
                    id.itemID = CSF_NV_FRAMECOUNTER_ID;
                    id.subID = 0;

                    /* Write the NV item */
                    pNV->writeItem(id, sizeof(uint32_t), &fc);
                }
            }
        }

        *pFrameCntr = 0;
    }
    return (false);
}


/*!
 Delete an entry from the device list

 Public function defined in csf.h
 */
void Csf_removeDeviceListItem(ApiMac_sAddrExt_t *pAddr)
{
    if((pNV != NULL) && (pNV->deleteItem != NULL))
    {
        int index;

        /* Does the item exist? */
        index = findDeviceListIndex(pAddr);
        if(index != DEVICE_INDEX_NOT_FOUND)
        {
            uint8_t stat;
            NVINTF_itemID_t id;

            /* Setup NV ID for the device list record */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;
            id.subID = (uint16_t)index;

            stat = pNV->deleteItem(id);
            if(stat == NVINTF_SUCCESS)
            {
                /* Update the number of entries */
                uint16_t numEntries = Csf_getNumDeviceListEntries();
                if(numEntries > 0)
                {
                    numEntries--;
                    saveNumDeviceListEntries(numEntries);
                }
            }
        }
    }
}

/*!
 Assert Indication

 Public function defined in csf.h
 */
void Csf_assertInd(uint8_t reason)
{
    LOG_printf( LOG_ERROR, "Assert Reason: %d\n", (int)(reason) );

#if defined(MT_CSF)
    if((pNV != NULL) && (pNV->writeItem != NULL))
    {
        /* Attempt to save reason to read after reset */
        (void)pNV->writeItem(nvResetId, 1, &reason);
    }
#endif
}

/*!
 Clear all the NV Items

 Public function defined in csf.h
 */
void Csf_clearAllNVItems(void)
{
    if((pNV != NULL) && (pNV->deleteItem != NULL))
    {
        NVINTF_itemID_t id;
        uint16_t entries;

        /* Clear Network Information */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_NETWORK_INFO_ID;
        id.subID = 0;
        pNV->deleteItem(id);

        /* Clear the device list entries number */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_DEVICELIST_ENTRIES_ID;
        id.subID = 0;
        pNV->deleteItem(id);

        /*
         Clear the device list entries.  Brute force through
         every possible subID, if it doesn't exist that's fine,
         it will fail in deleteItem.
         */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_DEVICELIST_ID;
        for(entries = 0; entries < CSF_MAX_DEVICELIST_IDS; entries++)
        {
            id.subID = entries;
            pNV->deleteItem(id);
        }

        /* Clear the device tx frame counter */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_FRAMECOUNTER_ID;
        id.subID = 0;
        pNV->deleteItem(id);
    }
}


/*!
 Check if config timer is active

 Public function defined in csf.h
 */
bool Csf_isConfigTimerActive(void)
{
    bool b;
    int r;
    /*
     * If the timer is not valid (ie: handle=0)
     * the 'getRemain()' will return negative
     * which is the same as expired.
     */
    r = TIMER_CB_getRemain(configClkHandle);
    if( r < 0 ){
        b = false;
    } else {
        b = true;
    }

    return b;
}

/*!
 Check if tracking timer is active

 Public function defined in csf.h
*/
bool Csf_isTrackingTimerActive(void)
{
    bool b;
    int r;
    r = TIMER_CB_getRemain(trackingClkHandle);
    if( r < 0 ){
        b = false;
    } else {
        b = true;
    }

    return b;
}

/*!
 The application calls this function to open the network.

 Public function defined in csf.h
 */
void Csf_openNwk(void)
{
    permitJoining = true;
    Cllc_setJoinPermit(0xFFFFFFFF);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: PermitJoin-ON");
#endif

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: PermitJoin-ON\n");
}

/*!
 The application calls this function to close the network.

 Public function defined in csf.h
 */
void Csf_closeNwk(void)
{
    permitJoining = false;
    Cllc_setJoinPermit(0);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: PermitJoin-OFF");
#endif

    LOG_printf(LOG_APPSRV_MSG_CONTENT, "Info: PermitJoin-OFF\n");
}

/*!
 * @brief       Removes a device from the network.
 *
 * @param        deviceShortAddr - device short address to remove.
 */
int Csf_sendDisassociateMsg(uint16_t deviceShortAddr)
{
    int status = -1;

    if(pNV != NULL)
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;
            uint16_t subId = 0;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;

            while(subId < CSF_MAX_DEVICELIST_IDS)
            {
                Llc_deviceListItem_t item;
                uint8_t stat;

                id.subID = (uint16_t)subId;

                /* Read Network Information from NV */
                stat = pNV->readItem(id, 0, sizeof(Llc_deviceListItem_t),
                                     &item);

                if( (stat == NVINTF_SUCCESS) && (deviceShortAddr == item.devInfo.shortAddress))
                {
                    /* Send a disassociate to the device */
                    Cllc_sendDisassociationRequest(item.devInfo.shortAddress,
                                                   item.capInfo.rxOnWhenIdle);

                    /* Remove it from the Device list */
                    Csf_removeDeviceListItem(&item.devInfo.extAddress);

                    status = 0;
                    break;
                }
                subId++;
            }
        }
    }

    return status;
}

/*!
 * @brief       Get the device Ext. Address from short address
 *
 * @param        deviceShortAddr - device short address
 */
int Csf_getDeviceExtAdd(uint16_t deviceShortAddr, ApiMac_sAddrExt_t * extAddr)
{
    int status = -1;

    if(pNV != NULL)
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;
            uint16_t subId = 0;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;

            while(subId < CSF_MAX_DEVICELIST_IDS)
            {
                Llc_deviceListItem_t item;
                uint8_t stat;

                id.subID = (uint16_t)subId;

                /* Read Network Information from NV */
                stat = pNV->readItem(id, 0, sizeof(Llc_deviceListItem_t),
                                     &item);

                if( (stat == NVINTF_SUCCESS) && (deviceShortAddr == item.devInfo.shortAddress))
                {
                    memcpy(extAddr, &item.devInfo.extAddress, sizeof(ApiMac_sAddrExt_t));
                    status = 0;
                    break;
                }
                subId++;
            }
        }
    }

    return status;
}

/*!
 *  Set collector event to start device after reset
 *
 *  Public function defined in csf.h
 */
void Csf_processCoPReset(void)
{
    /* Start the device */
    Util_setEvent(&Collector_events, COLLECTOR_START_EVT);
}

/******************************************************************************
 Local Functions
 *****************************************************************************/

/*!
 * @brief       Tracking timeout handler function.
 *
 * @param       a0 - ignored
 */
static void processTrackingTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Collector_events, COLLECTOR_TRACKING_TIMEOUT_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Broadcast timeout handler function.
 *
 * @param       a0 - ignored
 */
static void processBroadcastTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Collector_events, COLLECTOR_BROADCAST_TIMEOUT_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Join permit timeout handler function.
 *
 * @param       a0 - ignored
 */
static void processJoinTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Cllc_events, CLLC_JOIN_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Config delay timeout handler function.
 *
 * @param       a0 - ignored
 */
static void processConfigTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Collector_events, COLLECTOR_CONFIG_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Trickle timeout handler function for PA .
 *
 * @param       a0 - ignored
 */
static void processPATrickleTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Cllc_events, CLLC_PA_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Trickle timeout handler function for PC.
 *
 * @param       a0 - ignored
 */
static void processPCTrickleTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    Util_setEvent(&Cllc_events, CLLC_PC_EVT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

#ifndef IS_HEADLESS
/*!
 * @brief       OAD reset reqest retry timeout handler function.
 *
 * @param       a0 - ignored
 */
static void processOadResetReqRetryTimeoutCallback(UArg a0)
{
    (void)a0; /* Parameter is not used */

    ResetReqSendRetry = 1;
    Util_setEvent(&Csf_events, CSF_KEY_EVENT);

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}
#endif

/*!
 * @brief       Key event handler function
 *
 * @param       keysPressed - Csf_keys that are pressed
 */
static void processKeyChangeCallback(uint8_t keysPressed)
{
    Csf_keys = keysPressed;

    Csf_events |= CSF_KEY_EVENT;

    /* Wake up the application thread when it waits for clock event */
    Semaphore_post(collectorSem);
}

/*!
 * @brief       Add an entry into the device list
 *
 * @param       pItem - pointer to the device list entry
 * @param       pNewDevice - pointer to a flag which will be updated
 *              based on if the sensor joining is already assoc with
 *              the collector or freshly joining the network
 * @return      true if added or already existed, false if problem
 */
static bool addDeviceListItem(Llc_deviceListItem_t *pItem, bool *pNewDevice)
{
    bool retVal = false;

    int subId = DEVICE_INDEX_NOT_FOUND;
    /* By default, set this flag to true;
    will be updated - if device already found in the list*/
    *pNewDevice = true;

    if((pNV != NULL) && (pItem != NULL))
    {
        subId = findDeviceListIndex(&pItem->devInfo.extAddress);
        if(subId != DEVICE_INDEX_NOT_FOUND)
        {
            retVal = true;

            /* Not a new device; already exists */
            *pNewDevice = false;
        }
        else
        {
            uint8_t stat;
            NVINTF_itemID_t id;
            uint16_t numEntries = Csf_getNumDeviceListEntries();

            /* Check the maximum size */
            if(numEntries < CSF_MAX_DEVICELIST_ENTRIES)
            {
                /* Setup NV ID for the device list record */
                id.systemID = NVINTF_SYSID_APP;
                id.itemID = CSF_NV_DEVICELIST_ID;
                id.subID = (uint16_t)findUnusedDeviceListIndex();

                /* write the device list record */
                if(id.subID != CSF_INVALID_SUBID)
                {
                    stat = pNV->writeItem(id, sizeof(Llc_deviceListItem_t), pItem);
                    if(stat == NVINTF_SUCCESS)
                    {
                        /* Update the number of entries */
                        numEntries++;
                        saveNumDeviceListEntries(numEntries);
                        retVal = true;
                    }
                }
            }
        }
    }

    return (retVal);
}

/*!
 * @brief       Update an entry in the device list
 *
 * @param       pItem - pointer to the device list entry
 */
static void updateDeviceListItem(Llc_deviceListItem_t *pItem)
{
    if((pNV != NULL) && (pItem != NULL))
    {
        int idx;

        idx = findDeviceListIndex(&pItem->devInfo.extAddress);
        if(idx != DEVICE_INDEX_NOT_FOUND)
        {
            NVINTF_itemID_t id;

            /* Setup NV ID for the device list record */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;
            id.subID = (uint16_t)idx;

            /* write the device list record */
            pNV->writeItem(id, sizeof(Llc_deviceListItem_t), pItem);
        }
    }
}

/*!
 * @brief       Find entry in device list
 *
 * @param       pAddr - address to of device to find
 *
 * @return      sub index into the device list, -1 (DEVICE_INDEX_NOT_FOUND)
 *              if not found
 */
static int findDeviceListIndex(ApiMac_sAddrExt_t *pAddr)
{
    if((pNV != NULL) && (pAddr != NULL))
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;
            Llc_deviceListItem_t item;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;
            id.subID = 0;

            /* Read Network Information from NV */
            pNV->readContItem(id, 0, sizeof(Llc_deviceListItem_t), &item,
                                  APIMAC_SADDR_EXT_LEN,
                                  (uint16_t)((unsigned long)&item.devInfo.extAddress-(unsigned long)&item), pAddr, &id.subID);

            if(id.subID != CSF_INVALID_SUBID)
            {
                return(id.subID);
            }
        }
    }
    return (DEVICE_INDEX_NOT_FOUND);
}

/*!
 * @brief       Find an unused device list index
 *
 * @return      index that is not in use
 */
static int findUnusedDeviceListIndex(void)
{
    int x;

    for(x = 0; (x < CONFIG_MAX_DEVICES); x++)
    {
        /* Make sure the entry is valid. */
        if(CSF_INVALID_SHORT_ADDR == Cllc_associatedDevList[x].shortAddr)
        {
            return (x);
        }
    }
    return (CSF_INVALID_SUBID);
}

/*!
 * @brief       Read the number of device list items stored
 *
 * @param       numEntries - number of entries in the device list
 */
static void saveNumDeviceListEntries(uint16_t numEntries)
{
    if(pNV != NULL)
    {
        NVINTF_itemID_t id;

        /* Setup NV ID for the number of entries in the device list */
        id.systemID = NVINTF_SYSID_APP;
        id.itemID = CSF_NV_DEVICELIST_ENTRIES_ID;
        id.subID = 0;

        /* Read the number of device list items from NV */
        pNV->writeItem(id, sizeof(uint16_t), &numEntries);
    }
}


#ifndef IS_HEADLESS

/*!
 * @brief       This is an example function on how to remove a device
 *              from this network.
 *
 * @param       addr - device address
 *
 * @return      true if found, false if not
 */
static bool removeDevice(ApiMac_sAddr_t addr)
{
    LOG_printf(LOG_ERROR, "removing device 0x%04x\n", addr.addr.shortAddr);

    LOG_printf(LOG_ERROR, "sending Disassociation request to device 0x%04x\n", addr.addr.shortAddr);

    /* Send a disassociate to the device and remove from NV */
    Csf_sendDisassociateMsg(addr.addr.shortAddr);

    return 1;
}

#endif

#if defined(TEST_REMOVE_DEVICE)
/*!
 * @brief       This is an example function on how to remove a device
 *              from this network.
 */
static void removeTheFirstDevice(void)
{
    if(pNV != NULL)
    {
        uint16_t numEntries;

        numEntries = Csf_getNumDeviceListEntries();

        if(numEntries > 0)
        {
            NVINTF_itemID_t id;
            uint16_t subId = 0;

            /* Setup NV ID for the device list records */
            id.systemID = NVINTF_SYSID_APP;
            id.itemID = CSF_NV_DEVICELIST_ID;

            while(subId < CSF_MAX_DEVICELIST_IDS)
            {
                Llc_deviceListItem_t item;
                uint8_t stat;

                id.subID = (uint16_t)subId;

                /* Read Network Information from NV */
                stat = pNV->readItem(id, 0, sizeof(Llc_deviceListItem_t),
                                     &item);
                if(stat == NVINTF_SUCCESS)
                {
                    /* Found the first device in the list */
                    ApiMac_sAddr_t addr;

                    /* Send a disassociate to the device */
                    Cllc_sendDisassociationRequest(item.devInfo.shortAddress,
                                                   item.capInfo.rxOnWhenIdle);
                    /* Remove device from the NV list */
                    Cllc_removeDevice(&item.devInfo.extAddress);

                    /* Remove it from the Device list */
                    Csf_removeDeviceListItem(&item.devInfo.extAddress);
                    break;
                }
                subId++;
            }
        }
    }
}
#endif

/*!
 * @brief       Retrieve the first device's short address
 *
 * @return      short address or 0xFFFF if not found
 */
void Csf_freeDeviceInformationList(size_t n, Csf_deviceInformation_t *p)
{
    (void)(n); /* not used */
    if(p)
    {
        free((void *)(p));
    }
}

/*!
 The appSrv calls this function to get the list of connected
 devices

 Public function defined in csf_linux.h
 */
int Csf_getDeviceInformationList(Csf_deviceInformation_t **ppDeviceInfo)
{
    Csf_deviceInformation_t *pThis;
    Llc_deviceListItem_t    tmp;
    uint16_t actual;
    uint16_t subId;
    int n;
    NVINTF_itemID_t id;

    /* get number of connected devices */
    n = Csf_getNumDeviceListEntries();
    /* initialize device list pointer */

    pThis = calloc(n+1, sizeof(*pThis));
    *ppDeviceInfo = pThis;
    if(pThis == NULL)
    {
        LOG_printf(LOG_ERROR, "No memory for device list\n");
        return 0;
    }

    /* Setup NV ID for the device list records */
    id.systemID = NVINTF_SYSID_APP;
    id.itemID = CSF_NV_DEVICELIST_ID;
    subId = 0;
    actual = 0;
    /* Read the Entries */
    while((subId < CSF_MAX_DEVICELIST_IDS) && (actual < n))
    {
        uint8_t stat;

        id.subID = subId;
        /* Read Device Information from NV */
        stat = pNV->readItem(id, 0,
                             sizeof(tmp),
                             &tmp);
        if(stat == NVINTF_SUCCESS)
        {
            pThis->devInfo = tmp.devInfo;
            pThis->capInfo = tmp.capInfo;
            actual++;
            pThis++;
        }
        subId++;
     }

    /* return actual number of devices connected */
    return actual;
}

void CSF_LINUX_USE_THESE_FUNCTIONS(void);
void CSF_LINUX_USE_THESE_FUNCTIONS(void)
{
    /* (void)started; */
#if defined(TEST_REMOVE_DEVICE)
    (void)removeTheFirstDevice;
#endif
    (void)processKeyChangeCallback;
    (void)permitJoining;
}

const char *CSF_cllc_statename(Cllc_states_t s)
{
    const char *cp;
    switch(s)
    {
    default:
        cp = "unknown";
        break;
    case Cllc_states_initWaiting:
        cp = "initWaiting";
        break;
    case Cllc_states_startingCoordinator:
        cp = "startingCoordinator";
        break;
    case Cllc_states_initRestoringCoordinator:
        cp = "initRestoringCoordinator";
        break;
    case Cllc_states_started:
        cp = "started";
        break;
    case Cllc_states_restored:
        cp = "restored";
        break;
    case Cllc_states_joiningAllowed:
        cp = "joiningAllowed";
        break;
    case Cllc_states_joiningNotAllowed:
        cp = "joiningNotAllowed";
        break;
    }
    return cp;
}

/*!
 The appsrv module calls this function to send config request
 to a device over the air

 Public function defined in csf_linux.h
 */
extern uint8_t Csf_sendConfigRequest( ApiMac_sAddr_t *pDstAddr,
                uint16_t frameControl,
                uint32_t reportingInterval,
                uint32_t pollingInterval)
{
    return Collector_sendConfigRequest( pDstAddr,
                frameControl,
                reportingInterval,
                pollingInterval);
}

/*!
 The appsrv module calls this function to send a led toggle request
 to a device over the air

 Public function defined in csf_linux.h
 */
extern uint8_t Csf_sendToggleLedRequest(
                ApiMac_sAddr_t *pDstAddr)
{
    return Collector_sendToggleLedRequest(pDstAddr);
}

/*
 * Public function in csf_linux.h
 * Gateway front end uses this to get the current state.
 */
Cllc_states_t Csf_getCllcState(void)
{
    return savedCllcState;
}

/*!
	Call this function to obtain the time from sys clock
 */
static double getUnixTime(void)
{
	struct timespec tv;
	if(clock_gettime(CLOCK_REALTIME, &tv) != 0) return 0;
	return (tv.tv_sec + (tv.tv_nsec / 1000000000.0));
}

#ifndef IS_HEADLESS
/*!
	Starts the OAD reset request retry timer
 */
static void startOADResetReqRetryTimer(void)
{
    stopOADResetReqRetryTimer();

    oadResetReqRetryClkHandle = TIMER_CB_create("oadResetReqRetryTimer",
        processOadResetReqRetryTimeoutCallback_WRAPPER,
        0,
        OAD_RESET_REQ_RETRY_TIMEOUT_VALUE,
        true);
}

/*!
	Stops the OAD reset request retry timer
 */
static void stopOADResetReqRetryTimer(void)
{
    if (oadResetReqRetryClkHandle != 0)
    {
        TIMER_CB_destroy(oadResetReqRetryClkHandle);
        oadResetReqRetryClkHandle = 0;
    }
}
#endif

/*!
 The application calls this function to indicate that a device
 has reported raw data.

 Public function defined in csf.h
 */
void Csf_deviceRawDataUpdate(ApiMac_mcpsDataInd_t *pDataInd)
{
    /* send data to the appClient */
    appsrv_deviceRawDataUpdate(pDataInd);
    Board_Led_toggle(board_led_type_LED2);
}

/*!
 * @brief       Handles printing that the orphaned device joined back
 *
 * @return      none
 */
void Csf_IndicateOrphanReJoin(uint16_t shortAddr)
{
    LOG_printf(LOG_DBG_COLLECTOR_RAW, "Orphaned Sensor Re-Joined: Short: 0x%04x\n",
                shortAddr);

#ifndef IS_HEADLESS
    Board_Lcd_printf(DisplayLine_info, "Info: Orphaned Sensor Re-Joined: 0x%04x", shortAddr);
#endif //!IS_HEADLESS

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
