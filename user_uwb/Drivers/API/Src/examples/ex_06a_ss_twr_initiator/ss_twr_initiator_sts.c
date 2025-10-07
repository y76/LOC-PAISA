/*! ----------------------------------------------------------------------------
 *  @file    ss_twr_initiator_sts.c
 *  @brief   Single-sided two-way ranging (SS TWR) initiator example code
 *           MODIFIED TO MEASURE EXCHANGE TIME
 *
 * @author Decawave
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 */

#include "deca_probe_interface.h"
#include <config_options.h>
#include <deca_device_api.h>
#include <deca_spi.h>
#include <deca_types.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>
#include <stdlib.h>
#include <nrf_drv_uart.h>
#include <nrf_gpio.h>
#include <string.h>
#include <ctype.h>

#if defined(TEST_SS_TWR_INITIATOR_STS)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "SS TWR INIT STS v1.0"

/* Define PI for PDOA calculations */
#define PI 3.14159265358979f

/* Inter-ranging delay period, in milliseconds. */
#define RNG_DELAY_MS 1000

/* Default antenna delay values for 64 MHz PRF. See NOTE 2 below. */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

/* Frames used in the ranging process. See NOTE 3 below. */
static uint8_t tx_poll_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0 };
static uint8_t rx_resp_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Length of the common part of the message (up to and including the function code, see NOTE 3 below). */
#define ALL_MSG_COMMON_LEN 10
/* Indexes to access some of the fields in the frames defined above. */
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4
/* Frame sequence number, incremented after each transmission. */
static uint8_t frame_seq_nb = 0;

/* Buffer to store received response message. */
#define RX_BUF_LEN 24
static uint8_t rx_buffer[RX_BUF_LEN];

/* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
static uint32_t status_reg = 0;

/* Delay between frames, in UWB microseconds. See NOTE 1 below. */
#define POLL_TX_TO_RESP_RX_DLY_UUS (300 + CPU_PROCESSING_TIME)
/* Receive response timeout. See NOTE 5 below. */
#define RESP_RX_TIMEOUT_UUS 700

/* Hold copies of computed time of flight and distance here for reference. */
static double tof;
static double distance;

/* Add PDOA-related variables */
static char dist_pdoa_str[80]; /* Buffer for combined distance and PDOA output */
static int16_t pdoa_val = 0;
static float pdoa_degrees = 0;

/* Add timing measurement variables */
static uint64_t exchange_start_dtu = 0;  /* Device Time Units */
static uint64_t exchange_end_dtu = 0;
static double exchange_duration_ms = 0;

/* Hold the amount of errors that have occurred */
static uint32_t errors[23] = { 0 };

/* See NOTE 6 below. */
extern dwt_config_t config_options;
extern dwt_txconfig_t txconfig_options;
extern dwt_txconfig_t txconfig_options_ch9;

/* UART Configuration */
#define UART_RX_PIN  8
#define UART_TX_PIN 6
#define UART_BAUDRATE NRF_UART_BAUDRATE_115200
#define RX_BUF_SIZE 256
#define START_MARKER "PAISASTART:"
#define END_MARKER ":PAISAEND"

static nrf_drv_uart_t uart_instance = NRF_DRV_UART_INSTANCE(0);
static uint8_t rx_buf[RX_BUF_SIZE];
static uint8_t rxBuffer[RX_BUF_SIZE];
static uint16_t bufferIndex = 0;

/*
 * 128-bit STS key to be programmed into CP_KEY register.
 */
static dwt_sts_cp_key_t cp_key = { 0x14EB220F, 0xF86050A8, 0xD1D336AA, 0x14148674 };

/*
 * 128-bit initial value for the nonce to be programmed into the CP_IV register.
 */
static dwt_sts_cp_iv_t cp_iv = { 0x1F9A3DE4, 0xD37EC3CA, 0xC44FA8FB, 0x362EEB34 };

void findMessage(const uint8_t* buffer, uint16_t length) {
    printf("Buffer length: %d\n", length);
    
    for (uint16_t i = 0; i < length - strlen(START_MARKER) + 1; i++) {
        if (memcmp(buffer + i, START_MARKER, strlen(START_MARKER)) == 0) {
            uint16_t msgStart = i + strlen(START_MARKER);
            for (uint16_t j = msgStart; j < length - strlen(END_MARKER) + 1; j++) {
                if (memcmp(buffer + j, END_MARKER, strlen(END_MARKER)) == 0) {
                    printf("\nHex representation:\n");
                    for (uint16_t k = msgStart; k < j; k++) {
                        printf("%02X ", buffer[k]);
                        if ((k - msgStart + 1) % 16 == 0) {
                            printf("\n");
                        }
                    }
                    
                    printf("\n\nASCII representation:\n");
                    for (uint16_t k = msgStart; k < j; k++) {
                        if (isprint(buffer[k])) {
                            printf("%c", buffer[k]);
                        } else {
                            printf(".");
                        }
                        if ((k - msgStart + 1) % 16 == 0) {
                            printf("\n");
                        }
                    }
                    printf("\n\nSide by side (16 bytes per line):\n");
                    for (uint16_t k = msgStart; k < j; k += 16) {
                        for (uint16_t m = k; m < k + 16 && m < j; m++) {
                            printf("%02X ", buffer[m]);
                        }
                        for (uint16_t m = j; m < k + 16; m++) {
                            printf("   ");
                        }
                        printf("   ");
                        for (uint16_t m = k; m < k + 16 && m < j; m++) {
                            if (isprint(buffer[m])) {
                                printf("%c", buffer[m]);
                            } else {
                                printf(".");
                            }
                        }
                        printf("\n");
                    }
                    printf("\n");
                }
            }
        }
    }
}

void update_sts_key_iv(const uint8_t* data, uint16_t length) {
    if (length < 36) {
        printf("Not enough data for key, IV, and addresses update\n");
        return;
    }

    cp_key.key0 = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    cp_key.key1 = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    cp_key.key2 = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
    cp_key.key3 = (data[12] << 24) | (data[13] << 16) | (data[14] << 8) | data[15];

    cp_iv.iv0 = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
    cp_iv.iv1 = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
    cp_iv.iv2 = (data[24] << 24) | (data[25] << 16) | (data[26] << 8) | data[27];
    cp_iv.iv3 = (data[28] << 24) | (data[29] << 16) | (data[30] << 8) | data[31];

    tx_poll_msg[5] = data[34];
    tx_poll_msg[6] = data[35];
    tx_poll_msg[7] = data[32];
    tx_poll_msg[8] = data[33];

    rx_resp_msg[5] = data[32];
    rx_resp_msg[6] = data[33];
    rx_resp_msg[7] = data[34];
    rx_resp_msg[8] = data[35];

    printf("Updated STS key and IV:\n");
    printf("Key: %08lX %08lX %08lX %08lX\n", 
           cp_key.key0, cp_key.key1, cp_key.key2, cp_key.key3);
    printf("IV: %08lX %08lX %08lX %08lX\n", 
           cp_iv.iv0, cp_iv.iv1, cp_iv.iv2, cp_iv.iv3);
    printf("Updated addresses - Src: %02X%02X, Dst: %02X%02X\n",
           data[32], data[33], data[34], data[35]);
}

void uart_event_handler(nrf_drv_uart_event_t *p_event, void *p_context)
{
    if (p_event->type == NRF_DRV_UART_EVT_RX_DONE)
    {
        if (bufferIndex < RX_BUF_SIZE)
        {
            rxBuffer[bufferIndex++] = rx_buf[0];

            if (bufferIndex >= strlen(END_MARKER) &&
                memcmp(&rxBuffer[bufferIndex - strlen(END_MARKER)],
                      END_MARKER, strlen(END_MARKER)) == 0)
            {
                for (uint16_t i = 0; i < bufferIndex - strlen(START_MARKER); i++) {
                    if (memcmp(&rxBuffer[i], START_MARKER, strlen(START_MARKER)) == 0) {
                        uint16_t dataStart = i + strlen(START_MARKER);
                        uint16_t dataLength = bufferIndex - dataStart - strlen(END_MARKER);
                        
                        if (dataLength >= 32) {
                            update_sts_key_iv(&rxBuffer[dataStart], dataLength);
                        }
                        break;
                    }
                }
                
                bufferIndex = 0;
            }
        }
        else
        {
            bufferIndex = 0;
        }

        nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    }
}

void uart_init(void)
{
    nrf_gpio_cfg_input(UART_RX_PIN, NRF_GPIO_PIN_PULLUP);
    
    nrf_drv_uart_config_t uart_config = {
        .pseltxd = UART_TX_PIN,
        .pselrxd = UART_RX_PIN,
        .pselcts = NRF_UART_PSEL_DISCONNECTED,
        .pselrts = NRF_UART_PSEL_DISCONNECTED,
        .p_context = NULL,
        .hwfc = NRF_UART_HWFC_DISABLED,
        .parity = NRF_UART_PARITY_EXCLUDED,
        .baudrate = UART_BAUDRATE,
        .interrupt_priority = APP_IRQ_PRIORITY_HIGH
    };

    printf("Initializing UART RX...\n");
    printf("RX Pin: %d\n", uart_config.pselrxd);
    printf("Baudrate: %d\n", uart_config.baudrate);
    printf("IRQ Priority: %d\n", uart_config.interrupt_priority);
    
    uint32_t err_code = nrf_drv_uart_init(&uart_instance, &uart_config, uart_event_handler);
    if (err_code != NRF_SUCCESS)
    {
        printf("UART initialization failed with error: %d\n", err_code);
        return;
    }
    
    nrf_drv_uart_rx_enable(&uart_instance);
    
    err_code = nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    if (err_code != NRF_SUCCESS)
    {
        printf("Failed to start RX with error: %d\n", err_code);
        return;
    }
    
    printf("UART RX initialization complete\n");
}

static void send_tx_poll_msg(void)
{
    /* Write frame data to DW IC and prepare transmission. */
    tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);

    /* Start transmission. */
    dwt_starttx(DWT_START_TX_IMMEDIATE);

    /* Poll DW IC until TX frame sent event set. */
    waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

    /* Clear TXFRS event. */
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn ss_twr_initiator_sts()
 *
 * @brief Application entry point.
 *
 * @param  none
 *
 * @return none
 */
int ss_twr_initiator_sts(void)
{
    uart_init();
    int goodSts = 0;
    int16_t stsQual;
    uint16_t stsStatus;
    uint8_t firstLoopFlag = 0;

    /* Display application name on UART. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
#ifdef CONFIG_SPI_FAST_RATE
    port_set_dw_ic_spi_fastrate();
#endif
#ifdef CONFIG_SPI_SLOW_RATE
    port_set_dw_ic_spi_slowrate();
#endif

    /* Reset DW IC */
    reset_DWIC();

    Sleep(2);

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_IDLE) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Enabling LEDs here for debug */
    dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);

    /* Configure DW IC. */
    if (dwt_configure(&config_options))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Enable PDOA mode 3 */
    dwt_setpdoamode(DWT_PDOA_M3);

    /* Configure the TX spectrum parameters */
    if (config_options.chan == 5)
    {
        dwt_configuretxrf(&txconfig_options);
    }
    else
    {
        dwt_configuretxrf(&txconfig_options_ch9);
    }

    /* Apply default antenna delay value. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Set expected response's timeout. */
    set_resp_rx_timeout(RESP_RX_TIMEOUT_UUS, &config_options);

    /* Enable TX/RX states output on GPIOs */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    /* Loop for user defined number of ranges. */
    while (1)
    {
        /* Set STS encryption key and IV (nonce). */
        if (!firstLoopFlag)
        {
            dwt_configurestskey(&cp_key);
            dwt_configurestsiv(&cp_iv);
            dwt_configurestsloadiv();
            firstLoopFlag = 1;
        }
        else
        {
            dwt_configurestsiv(&cp_iv);
            dwt_configurestsloadiv();
        }

        /* Send the poll message to the responder. */
        send_tx_poll_msg();

        /* Set a reference time for the RX to start after TX timestamp. */
        set_delayed_rx_time(POLL_TX_TO_RESP_RX_DLY_UUS, &config_options);

        /* Activate reception after TX timestamp for the POLL message. */
        dwt_rxenable(DWT_START_RX_DLY_TS);

        /* Poll for reception of a frame or error/timeout. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR), 0);

        /* Check the STS quality. */
        goodSts = dwt_readstsquality(&stsQual, 0);

        /* Increment frame sequence number after transmission of the poll message (modulo 256). */
        frame_seq_nb++;

        /* Check for a good frame and good STS quality. */
        if ((status_reg & DWT_INT_RXFCG_BIT_MASK) && (goodSts >= 0) && (dwt_readstsstatus(&stsStatus, 0) == DWT_SUCCESS))
        {
            uint16_t frame_len;

            /* Clear the RX events. */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD);

            /* A frame has been received, read it into the local buffer. */
            frame_len = dwt_getframelength(0);
            if (frame_len <= sizeof(rx_buffer))
            {
                dwt_readrxdata(rx_buffer, frame_len, 0);

                /* Check that the frame is the expected response. */
                rx_buffer[ALL_MSG_SN_IDX] = 0;
                if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0)
                {
                    uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
                    int32_t rtd_init, rtd_resp;
                    float clockOffsetRatio;

                    /* Retrieve poll transmission and response reception timestamps. */
                    poll_tx_ts = dwt_readtxtimestamplo32();
                    resp_rx_ts = dwt_readrxtimestamplo32(0);

                    /* Use poll TX and response RX timestamps for exchange timing */
                    exchange_start_dtu = (uint64_t)poll_tx_ts;
                    exchange_end_dtu = (uint64_t)resp_rx_ts;

                    /* Read carrier integrator value and calculate clock offset ratio. */
                    clockOffsetRatio = ((float)dwt_readclockoffset()) / (uint32_t)(1 << 26);

                    /* Get timestamps embedded in response message. */
                    resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
                    resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

                    /* Compute time of flight and distance */
                    rtd_init = resp_rx_ts - poll_tx_ts;
                    rtd_resp = resp_tx_ts - poll_rx_ts;

                    tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
                    distance = tof * SPEED_OF_LIGHT;

                    /* Read PDOA value */
                    pdoa_val = dwt_readpdoa();
                    pdoa_degrees = ((float)pdoa_val / (1<<11)) * 180 / PI;

                    /* Calculate exchange duration in milliseconds */
                    exchange_duration_ms = (double)(exchange_end_dtu - exchange_start_dtu) * DWT_TIME_UNITS * 1000.0;
                    
                    /* Display distance, PDOA, and exchange time */
                    snprintf(dist_pdoa_str, sizeof(dist_pdoa_str), 
                            "DIST: %3.2f m, PDOA: %d (%3.1f deg)", 
                            distance, pdoa_val, pdoa_degrees);
                    test_run_info((unsigned char *)dist_pdoa_str);

                    /* Print with timing information */
                    printf("SS-TWR: %3.2f m, PDOA: %d (%3.1f deg), Time: %.2f ms\n", 
                           distance, pdoa_val, pdoa_degrees, exchange_duration_ms);

                    /* Transmit "b" over UART */
                    uint8_t tx_byte = 'b';
                    nrf_drv_uart_tx(&uart_instance, &tx_byte, 1);
                }
                else
                {
                    errors[BAD_FRAME_ERR_IDX] += 1;
                    /* Print the received vs expected frame contents */
                    printf("Frame comparison failed!\n");
                    printf("Received frame:  ");
                    for (int i = 0; i < ALL_MSG_COMMON_LEN; i++) {
                        printf("%02X ", rx_buffer[i]);
                    }
                    printf("\nExpected frame: ");
                    for (int i = 0; i < ALL_MSG_COMMON_LEN; i++) {
                        printf("%02X ", rx_resp_msg[i]);
                    }
                    printf("\nMismatch at byte(s): ");
                    for (int i = 0; i < ALL_MSG_COMMON_LEN; i++) {
                        if (rx_buffer[i] != rx_resp_msg[i]) {
                            printf("%d ", i);
                        }
                    }
                    printf("\n");
                }
            }
            else
            {
                errors[RTO_ERR_IDX] += 1;
            }
        }
        else
        {
            check_for_status_errors(status_reg, errors);

            if (!(status_reg & DWT_INT_RXFCG_BIT_MASK))
            {
                errors[BAD_FRAME_ERR_IDX] += 1;
            }
            if (goodSts < 0)
            {
                errors[PREAMBLE_COUNT_ERR_IDX] += 1;
            }
            if (stsQual <= 0)
            {
                errors[CP_QUAL_ERR_IDX] += 1;
            }
        }

        /* Clear RX error/timeout events in the DW IC status register. */
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_GOOD | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);

        /* Execute a delay between ranging exchanges. */
        Sleep(RNG_DELAY_MS);
    }
}
#endif

/*****************************************************************************************************************************************************
 * MODIFICATIONS SUMMARY:
 *
 * 1. TIMING MEASUREMENT:
 *    - Added timing variables (exchange_start_dtu, exchange_end_dtu, exchange_duration_ms)
 *    - Start time = Poll TX timestamp (already being read)
 *    - End time = Response RX timestamp (already being read)
 *    - Calculate duration using DW3000's Device Time Units
 *    - Convert to milliseconds for easy reading
 *
 * 2. EXISTING FEATURES PRESERVED:
 *    - PDOA measurement (already present)
 *    - Distance calculation (already present)
 *    - UART functionality (already present)
 *    - STS security (already present)
 *
 * EXPECTED OUTPUT:
 * SS-TWR: 2.35 m, PDOA: 512 (22.5 deg), Time: 0.52 ms
 *
 * This measures the actual over-the-air exchange time between poll TX and response RX.
 * Compare this with DS-TWR which will show longer times due to the additional final message.
 ****************************************************************************************************************************************************/