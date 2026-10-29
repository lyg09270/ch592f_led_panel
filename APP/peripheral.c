/********************************** (C) COPYRIGHT *******************************
 * File Name          : peripheral.C
 * Author             : zhangxiyi @WCH
 * Version            : v0.1
 * Date               : 2020/11/26
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "peripheral.h"

#include "ble_uart_service.h"
#include "app_drv_fifo.h"
#include "TinyCmd.h"
#include "dev_microphone.h"
#include "dev_rgb_matrix.h"
#include "led_effects.h"

#include <string.h>

uint8_t Peripheral_TaskID = INVALID_TASK_ID; // Task ID for internal task/event processing

//The buffer length should be a power of 2
#define APP_UART_RX_BUFFER_LENGTH    128U
#if (DEV_RGB_MATRIX_USE_TILED_2X2 == 1) && \
    ((DEV_RGB_MATRIX_USE_WS2812_GPIO == 1) || \
     (DEV_RGB_MATRIX_USE_WS2812_SPI == 1))
/*
 * A 1024-pixel frame blocks interrupts for about 31 ms.  Another 33.125 ms
 * is reserved after every transfer so the BLE controller can service at
 * least one connection event.  This is the practical blocking-SPI/BLE
 * coexistence limit: approximately 15 fps.
 */
#define LED_ANIMATION_FRAME_TICKS     53U
#define LED_ANIMATION_SCHEDULE_AFTER_WRITE  1U
#else
/* 53 * 0.625 ms = 33.125 ms, approximately 30 fps. */
#define LED_ANIMATION_FRAME_TICKS     53U
#define LED_ANIMATION_SCHEDULE_AFTER_WRITE  0U
#endif

static uint8_t app_uart_rx_buffer[APP_UART_RX_BUFFER_LENGTH] = {0};
static app_drv_fifo_t app_uart_rx_fifo;

//for interrupt rx blcak hole ,when uart rx fifo full
uint8_t for_uart_rx_black_hole = 0;

static uint8_t rgb_pixels[DEV_RGB_MATRIX_HEIGHT]
                         [DEV_RGB_MATRIX_WIDTH]
                         [DEV_RGB_MATRIX_RGB888_CHANNELS];

/*
 * Deterministic panel-orientation pattern used to verify the physical map.
 * Every 16x16 logical tile must show: red top, green right, blue bottom,
 * yellow left, and a 3x3 white marker in its top-left corner.
 */
static bool led_show_mapping_pattern(void)
{
    uint16_t tile_y;
    uint16_t tile_x;

    memset(rgb_pixels, 0, sizeof(rgb_pixels));
    for(tile_y = 0U; tile_y < DEV_RGB_MATRIX_TILE_ROWS; tile_y++)
    {
        for(tile_x = 0U; tile_x < DEV_RGB_MATRIX_TILE_COLUMNS; tile_x++)
        {
            uint16_t origin_y =
                (uint16_t)(tile_y * DEV_RGB_MATRIX_PANEL_HEIGHT);
            uint16_t origin_x =
                (uint16_t)(tile_x * DEV_RGB_MATRIX_PANEL_WIDTH);
            uint16_t offset;
            uint16_t marker_y;
            uint16_t marker_x;

            for(offset = 0U; offset < DEV_RGB_MATRIX_PANEL_WIDTH; offset++)
            {
                /* Top: red. Bottom: blue. */
                rgb_pixels[origin_y][origin_x + offset][0] = 64U;
                rgb_pixels[origin_y + DEV_RGB_MATRIX_PANEL_HEIGHT - 1U]
                          [origin_x + offset][2] = 64U;
            }
            for(offset = 0U; offset < DEV_RGB_MATRIX_PANEL_HEIGHT; offset++)
            {
                /* Left: yellow. Right: green. */
                rgb_pixels[origin_y + offset][origin_x][0] = 64U;
                rgb_pixels[origin_y + offset][origin_x][1] = 48U;
                rgb_pixels[origin_y + offset]
                          [origin_x + DEV_RGB_MATRIX_PANEL_WIDTH - 1U][1] = 64U;
            }
            for(marker_y = 1U; marker_y <= 3U; marker_y++)
            {
                for(marker_x = 1U; marker_x <= 3U; marker_x++)
                {
                    rgb_pixels[origin_y + marker_y]
                              [origin_x + marker_x][0] = 64U;
                    rgb_pixels[origin_y + marker_y]
                              [origin_x + marker_x][1] = 64U;
                    rgb_pixels[origin_y + marker_y]
                              [origin_x + marker_x][2] = 64U;
                }
            }
        }
    }

    return dev_rgb_matrix.set_brightness(&dev_rgb_matrix, 255U) &&
           dev_rgb_matrix.write(&dev_rgb_matrix, rgb_pixels);
}

static TinyCmd_CallBack_Ret led_command_callback(void)
{
    uint8_t row;
    uint8_t column;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t turn_on;

    if(TinyCmd_buf.arg[0] == NULL)
    {
        return TINYCMD_FAILED;
    }

    if(TinyCmd_Arg_Check("map", 0) == TINYCMD_SUCCESS)
    {
        led_effects_stop();
        tmos_stop_task(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);
        tmos_clear_event(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);
        return led_show_mapping_pattern()
                   ? TINYCMD_SUCCESS
                   : TINYCMD_FAILED;
    }

    if(TinyCmd_Arg_Check("animate", 0) == TINYCMD_SUCCESS)
    {
        uint8_t effect;
        uint8_t speed;
        uint8_t intensity;
        uint8_t brightness;

        if((TinyCmd_buf.arg[1] == NULL) ||
           (TinyCmd_buf.arg[2] == NULL) ||
           (TinyCmd_buf.arg[3] == NULL) ||
           (TinyCmd_buf.arg[4] == NULL) ||
           (TinyCmd_Arg_To_Num(1, &effect, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           (TinyCmd_Arg_To_Num(2, &speed, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           (TinyCmd_Arg_To_Num(3, &intensity, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           (TinyCmd_Arg_To_Num(4, &brightness, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           !led_effects_start(effect, speed, intensity, brightness))
        {
            return TINYCMD_FAILED;
        }

        tmos_stop_task(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);
        tmos_set_event(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);
        return TINYCMD_SUCCESS;
    }

    if(TinyCmd_Arg_Check("calib", 0) == TINYCMD_SUCCESS)
    {
        if((TinyCmd_buf.arg[1] == NULL) ||
           (TinyCmd_buf.arg[2] == NULL) ||
           (TinyCmd_buf.arg[3] == NULL) ||
           (TinyCmd_Arg_To_Num(1, &red, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           (TinyCmd_Arg_To_Num(2, &green, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           (TinyCmd_Arg_To_Num(3, &blue, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
           !dev_rgb_matrix.set_correction(&dev_rgb_matrix,
                                          red,
                                          green,
                                          blue))
        {
            return TINYCMD_FAILED;
        }

        return dev_rgb_matrix.write(&dev_rgb_matrix, rgb_pixels)
                   ? TINYCMD_SUCCESS
                   : TINYCMD_FAILED;
    }

    if((TinyCmd_buf.arg[1] == NULL) ||
       (TinyCmd_buf.arg[2] == NULL) ||
       (TinyCmd_buf.arg[3] == NULL) ||
       (TinyCmd_buf.arg[4] == NULL) ||
       (TinyCmd_buf.arg[5] == NULL))
    {
        return TINYCMD_FAILED;
    }

    if(TinyCmd_Arg_Check("on", 0) == TINYCMD_SUCCESS)
    {
        turn_on = 1U;
    }
    else if(TinyCmd_Arg_Check("off", 0) == TINYCMD_SUCCESS)
    {
        turn_on = 0U;
    }
    else
    {
        return TINYCMD_FAILED;
    }

    if((TinyCmd_Arg_To_Num(1, &row, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
       (TinyCmd_Arg_To_Num(2, &column, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
       (TinyCmd_Arg_To_Num(3, &red, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
       (TinyCmd_Arg_To_Num(4, &green, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
       (TinyCmd_Arg_To_Num(5, &blue, TINYCMD_UINT8) != TINYCMD_SUCCESS) ||
       (row >= DEV_RGB_MATRIX_HEIGHT) ||
       (column >= DEV_RGB_MATRIX_WIDTH))
    {
        return TINYCMD_FAILED;
    }

    if(dev_rgb_matrix.brightness != 255U)
    {
        uint16_t scale_row;
        uint16_t scale_column;
        uint8_t channel;

        for(scale_row = 0U;
            scale_row < DEV_RGB_MATRIX_HEIGHT;
            scale_row++)
        {
            for(scale_column = 0U;
                scale_column < DEV_RGB_MATRIX_WIDTH;
                scale_column++)
            {
                for(channel = 0U;
                    channel < DEV_RGB_MATRIX_RGB888_CHANNELS;
                    channel++)
                {
                    rgb_pixels[scale_row][scale_column][channel] =
                        (uint8_t)(((uint16_t)rgb_pixels[scale_row]
                                                        [scale_column]
                                                        [channel] *
                                   dev_rgb_matrix.brightness) / 255U);
                }
            }
        }
    }

    rgb_pixels[row][column][0] = turn_on ? red : 0U;
    rgb_pixels[row][column][1] = turn_on ? green : 0U;
    rgb_pixels[row][column][2] = turn_on ? blue : 0U;

    if((dev_rgb_matrix.set_brightness == NULL) ||
       !dev_rgb_matrix.set_brightness(&dev_rgb_matrix, 255U))
    {
        return TINYCMD_FAILED;
    }

    led_effects_stop();
    tmos_stop_task(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);
    tmos_clear_event(Peripheral_TaskID, SBP_LED_ANIMATE_EVT);

    return dev_rgb_matrix.write(&dev_rgb_matrix, rgb_pixels)
               ? TINYCMD_SUCCESS
               : TINYCMD_FAILED;
}

static TinyCmd_Command led_command = {
    .command = "led",
    .callback = led_command_callback
};

static void command_input_char(uint8_t data)
{
    if((data == '\r') || (data == '\n'))
    {
        if(TinyCmd_buf.length != 0U)
        {
            TinyCmd_buf.input[TinyCmd_buf.length] = '\0';
            TinyCmd_Handler();
        }
        return;
    }

    if(TinyCmd_buf.length < (CMD_BUF_SIZE - 1U))
    {
        TinyCmd_buf.input[TinyCmd_buf.length++] = (char)data;
    }
    else
    {
        TinyCmd_buf.length = 0U;
        TinyCmd_buf.input[0] = '\0';
    }
}

void app_uart_process(void)
{
    while(!app_drv_fifo_is_empty(&app_uart_rx_fifo))
    {
        command_input_char(app_drv_fifo_pop(&app_uart_rx_fifo));
    }
}

void app_uart_init()
{
    app_drv_fifo_init(&app_uart_rx_fifo, app_uart_rx_buffer, APP_UART_RX_BUFFER_LENGTH);

    //uart rx io
    GPIOA_SetBits(bRXD3);
    GPIOA_ModeCfg(bRXD3, GPIO_ModeIN_PU);

    //uart3 init
    UART3_DefInit();

    //enable interupt
    UART3_INTCfg(ENABLE, RB_IER_RECV_RDY | RB_IER_LINE_STAT);
    PFIC_EnableIRQ(UART3_IRQn);

    TinyCmd_Add_Cmd(&led_command);
    (void)dev_microphone.init(&dev_microphone);
    if(dev_rgb_matrix.init(&dev_rgb_matrix))
    {
        dev_rgb_matrix.write(&dev_rgb_matrix, rgb_pixels);
    }
}
//
//Not every uart reception will end with a UART_II_RECV_TOUT
//UART_II_RECV_TOUT can only be triggered when R8_UARTx_RFC is not 0
//Here we cannot rely UART_II_RECV_TOUT as the end of a uart reception

__INTERRUPT
__HIGH_CODE
void UART3_IRQHandler(void)
{
    uint16_t error;
    switch(UART3_GetITFlag())
    {
        case UART_II_LINE_STAT:
            UART3_GetLinSTA();
            break;

        case UART_II_RECV_RDY:
        case UART_II_RECV_TOUT:
            error = app_drv_fifo_write_from_same_addr(&app_uart_rx_fifo, (uint8_t *)&R8_UART3_RBR, R8_UART3_RFC);
            if(error != APP_DRV_FIFO_RESULT_SUCCESS)
            {
                for(uint8_t i = 0; i < R8_UART3_RFC; i++)
                {
                    //fifo full,put to fifo black hole
                    for_uart_rx_black_hole = R8_UART3_RBR;
                }
            }
            break;

        case UART_II_THR_EMPTY:
            break;
        case UART_II_MODEM_CHG:
            break;
        default:
            break;
    }
}

//ble uart service callback handler
void on_bleuartServiceEvt(uint16_t connection_handle, ble_uart_evt_t *p_evt)
{
    switch(p_evt->type)
    {
        case BLE_UART_EVT_TX_NOTI_DISABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_DISABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_TX_NOTI_ENABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_ENABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_BLE_DATA_RECIEVED:
            for(uint16_t i = 0U; i < p_evt->data.length; i++)
            {
                command_input_char(p_evt->data.p_data[i]);
            }
            break;
        default:
            break;
    }
}

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// How often to perform periodic event
#define SBP_PERIODIC_EVT_PERIOD              1600

// How often to perform read rssi event
#define SBP_READ_RSSI_EVT_PERIOD             3200

// Parameter update delay
#define SBP_PARAM_UPDATE_DELAY               6400

// What is the advertising interval when device is discoverable (units of 625us, 80=50ms)
#define DEFAULT_ADVERTISING_INTERVAL         160

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely
#define DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL

// Minimum connection interval (units of 1.25ms, 10=12.5ms)
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL    8

// Maximum connection interval (units of 1.25ms, 100=125ms)
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    20

// Slave latency to use parameter update
#define DEFAULT_DESIRED_SLAVE_LATENCY        0

// Supervision timeout value (units of 10ms, 100=1s)
#define DEFAULT_DESIRED_CONN_TIMEOUT         100

// Company Identifier: WCH
#define WCH_COMPANY_ID                       0x07D7

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

blePaControlConfig_t pa_lna_ctl;

//static uint8_t Peripheral_TaskID = INVALID_TASK_ID;   // Task ID for internal task/event processing

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scanRspData[] = {
    // complete name
    15, // length of this data
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'C', 'H', '5', '9', '2', '_', 'N', 'U', 'S', '_', 'U', 'A', 'R', 'T',
    // connection interval range
    0x05, // length of this data
    GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), // 100ms
    HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
    LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), // 1s
    HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),

    // Tx power level
    0x02, // length of this data
    GAP_ADTYPE_POWER_LEVEL,
    0 // 0dBm
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertisting)
static uint8_t advertData[] = {
    // Flags; this sets the device to use limited discoverable
    // mode (advertises for 30 seconds at a time) instead of general
    // discoverable mode (advertises indefinitely)
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    // Nordic UART Service UUID (little-endian, complete 128-bit UUID list)
    0x11,
    GAP_ADTYPE_128BIT_COMPLETE,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E};

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "CH592_NUS_UART";

// Connection item list
static peripheralConnItem_t peripheralConnList;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);

static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout);
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList);
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Peripheral_PeripheralCBs = {
    peripheralStateNotificationCB, // Profile State Change Callbacks
    peripheralRssiCB,              // When a valid RSSI is read from controller (not used by application)
    peripheralParamUpdateCB};

// Broadcast Callbacks
static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = {
    NULL, // Not used in peripheral role
    NULL  // Receive scan request callback
};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
    NULL, // Passcode callback (not used by application)
    NULL  // Pairing / Bonding state Callback (not used by application)
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Peripheral_Init
 *
 * @brief   Initialization function for the Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Peripheral_Init()
{
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    // Setup the GAP Peripheral Role Profile
    {
        uint8_t  initial_advertising_enable = TRUE;
        uint16_t desired_min_interval = 6;
        uint16_t desired_max_interval = 1000;

        // Set the GAP Role Parameters
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);
    }

    // Set advertising interval
    {
        uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;

        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
    }

    // Setup the GAP Bond Manager
    {
        uint32_t passkey = 0; // passkey "000000"
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = TRUE;
        uint8_t  bonding = TRUE;
        uint8_t  ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);         // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES); // GATT attributes
    DevInfo_AddService();                      // Device Information Service
    ble_uart_add_service(on_bleuartServiceEvt);

    // Set the GAP Characteristics
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

    // Init Connection Item
    peripheralInitConnItem(&peripheralConnList);

    // Register receive scan request callback
    GAPRole_BroadcasterSetCB(&Broadcaster_BroadcasterCBs);

    // Setup a delayed profile startup
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
}

/*********************************************************************
 * @fn      peripheralInitConnItem
 *
 * @brief   Init Connection Item
 *
 * @param   peripheralConnList -
 *
 * @return  NULL
 */
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList)
{
    peripheralConnList->connHandle = GAP_CONNHANDLE_INIT;
    peripheralConnList->connInterval = 0;
    peripheralConnList->connSlaveLatency = 0;
    peripheralConnList->connTimeout = 0;
}

uint32_t get_fattime(void)
{
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessEvent
 *
 * @brief   Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        // Start the Device
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        return (events ^ SBP_START_DEVICE_EVT);
    }
    if(events & SBP_PARAM_UPDATE_EVT)
    {
        // Send connect param update request
        GAPRole_PeripheralConnParamUpdateReq(peripheralConnList.connHandle,
                                             DEFAULT_DESIRED_MIN_CONN_INTERVAL,
                                             DEFAULT_DESIRED_MAX_CONN_INTERVAL,
                                             DEFAULT_DESIRED_SLAVE_LATENCY,
                                             DEFAULT_DESIRED_CONN_TIMEOUT,
                                             Peripheral_TaskID);

        //        GAPRole_PeripheralConnParamUpdateReq( peripheralConnList.connHandle,
        //                                              10,
        //                                              20,
        //                                              0,
        //                                              400,
        //                                              Peripheral_TaskID);

        return (events ^ SBP_PARAM_UPDATE_EVT);
    }

    if(events & SBP_LED_ANIMATE_EVT)
    {
        if(led_effects_is_running())
        {
#if LED_ANIMATION_SCHEDULE_AFTER_WRITE == 0U
            /* Schedule first so rendering time is not added to the period. */
            tmos_start_task(Peripheral_TaskID, SBP_LED_ANIMATE_EVT,
                            LED_ANIMATION_FRAME_TICKS);
#endif
            led_effects_render(rgb_pixels);
            dev_rgb_matrix.write(&dev_rgb_matrix, rgb_pixels);
#if LED_ANIMATION_SCHEDULE_AFTER_WRITE == 1U
            /* Prevent a slow render/write from creating back-to-back frames. */
            tmos_start_task(Peripheral_TaskID, SBP_LED_ANIMATE_EVT,
                            LED_ANIMATION_FRAME_TICKS);
#endif
        }
        return (events ^ SBP_LED_ANIMATE_EVT);
    }

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        default:
            break;
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkEstablished
 *
 * @brief   Process link established.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkEstablished(gapRoleEvent_t *pEvent)
{
    gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;

    // See if already connected
    if(peripheralConnList.connHandle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(pEvent->linkCmpl.connectionHandle);
        PRINT("Connection max...\n");
    }
    else
    {
        peripheralConnList.connHandle = event->connectionHandle;
        peripheralConnList.connInterval = event->connInterval;
        peripheralConnList.connSlaveLatency = event->connLatency;
        peripheralConnList.connTimeout = event->connTimeout;

        // Set timer for param update event
        tmos_start_task(Peripheral_TaskID, SBP_PARAM_UPDATE_EVT, SBP_PARAM_UPDATE_DELAY);

        PRINT("Conn %x - Int %x \n", event->connectionHandle, event->connInterval);
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkTerminated
 *
 * @brief   Process link terminated.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
    gapTerminateLinkEvent_t *event = (gapTerminateLinkEvent_t *)pEvent;

    if(event->connectionHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connHandle = GAP_CONNHANDLE_INIT;
        peripheralConnList.connInterval = 0;
        peripheralConnList.connSlaveLatency = 0;
        peripheralConnList.connTimeout = 0;

        // Restart advertising
        {
            uint8_t advertising_enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
        }
    }
    else
    {
        PRINT("ERR..\n");
    }
}

/*********************************************************************
 * @fn      peripheralRssiCB
 *
 * @brief   RSSI callback.
 *
 * @param   connHandle - connection handle
 * @param   rssi - RSSI
 *
 * @return  none
 */
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi)
{
    PRINT("RSSI -%d dB Conn  %x \n", -rssi, connHandle);
}

/*********************************************************************
 * @fn      peripheralParamUpdateCB
 *
 * @brief   Parameter update complete callback
 *
 * @param   connHandle - connect handle
 *          connInterval - connect interval
 *          connSlaveLatency - connect slave latency
 *          connTimeout - connect timeout
 *
 * @return  none
 */
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout)
{
    if(connHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connInterval = connInterval;
        peripheralConnList.connSlaveLatency = connSlaveLatency;
        peripheralConnList.connTimeout = connTimeout;

        PRINT("Update %x - Int %x \n", connHandle, connInterval);
    }
    else
    {
        PRINT("peripheralParamUpdateCB err..\n");
    }
}

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
        {
            uint8_t ownAddr[6];
            GAPRole_GetParameter(GAPROLE_BD_ADDR, ownAddr);
            GAP_ConfigDeviceAddr(ADDRTYPE_STATIC, ownAddr);
            PRINT("Initialized..\n");
            break;
        }

        case GAPROLE_ADVERTISING:
            if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
            }
            PRINT("Advertising..\n");
            break;

        case GAPROLE_CONNECTED:
            if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                Peripheral_LinkEstablished(pEvent);
                PRINT("Connected..\n");
            }
            break;

        case GAPROLE_CONNECTED_ADV:
            PRINT("Connected Advertising..\n");
            break;

        case GAPROLE_WAITING:
            if(pEvent->gap.opcode == GAP_END_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Waiting for advertising..\n");
            }
            else if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
                PRINT("Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
            }
            else if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                if(pEvent->gap.hdr.status != SUCCESS)
                {
                    PRINT("Waiting for advertising..\n");
                }
                else
                {
                    PRINT("Error..\n");
                }
            }
            else
            {
                PRINT("Error..%x\n", pEvent->gap.opcode);
            }
            break;

        case GAPROLE_ERROR:
            PRINT("Error..\n");
            break;

        default:
            break;
    }
}

/*********************************************************************
*********************************************************************/
