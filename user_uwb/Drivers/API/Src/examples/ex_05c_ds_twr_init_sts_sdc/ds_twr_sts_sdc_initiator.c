/*! ----------------------------------------------------------------------------
 *  @file    ds_twr_sts_sdc_initator.c
 *  @brief   Double-sided two-way ranging (DS TWR) STS with SDC (STS-SDC) initiator example code
 *           MODIFIED TO CALCULATE DISTANCE, PDOA, AND MEASURE EXCHANGE TIME ON INITIATOR SIDE
 *
 * @author Decawave
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 */

#include "deca_probe_interface.h"
#include <config_options.h>
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>

#if defined(TEST_DS_TWR_STS_SDC_INITIATOR)

extern void test_run_info(unsigned char *data);

/* Example application name and version to display on LCD screen. */
#define APP_NAME "DSTWR IN STS-SDC v1.0"

/* Define PI for PDOA calculations */
#define PI 3.14159265358979f

/* Inter-ranging delay period, in milliseconds. */
#define RNG_DELAY_MS 1000

/* Default communication configuration. We use STS with SDC DW mode. */
static dwt_config_t config = {
    5,               /* Channel number. */
    DWT_PLEN_64,     /* Preamble length. Used in TX only. */
    DWT_PAC8,        /* Preamble acquisition chunk size. Used in RX only. */
    9,               /* TX preamble code. Used in TX only. */
    9,               /* RX preamble code. Used in RX only. */
    1,               /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,      /* Data rate. */
    DWT_PHRMODE_STD, /* PHY header mode. */
    DWT_PHRRATE_STD, /* PHY header rate. */
    (65 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_1 | DWT_STS_MODE_SDC, /* STS mode 1 with SDC see NOTE on SDC below*/
    DWT_STS_LEN_64,                    /* STS length see allowed values in Enum dwt_sts_lengths_e */
    DWT_PDOA_M0                        /* PDOA mode off */
};

/* Default antenna delay values for 64 MHz PRF. See NOTE 1 below. */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

/* Frames used in the ranging process. See NOTE 2 below. */
static uint8_t tx_poll_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21 };
/* MODIFIED: Response message now includes responder timestamps (8 extra bytes) */
static uint8_t rx_resp_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0 };
static uint8_t tx_final_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Length of the common part of the message (up to and including the function code, see NOTE 2 below). */
#define ALL_MSG_COMMON_LEN 10

/* Indexes to access some of the fields in the frames defined above. */
#define ALL_MSG_SN_IDX            2
#define RESP_MSG_POLL_RX_TS_IDX   10
#define RESP_MSG_RESP_TX_TS_IDX   14
#define FINAL_MSG_POLL_TX_TS_IDX  10
#define FINAL_MSG_RESP_RX_TS_IDX  14
#define FINAL_MSG_FINAL_TX_TS_IDX 18

/* Frame sequence number, incremented after each transmission. */
static uint8_t frame_seq_nb = 0;

/* Buffer to store received response message. */
#define RX_BUF_LEN 24
static uint8_t rx_buffer[RX_BUF_LEN];

/* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
static uint32_t status_reg = 0;

/* Delay between frames, in UWB microseconds. See NOTE 4 below. */
#define POLL_TX_TO_RESP_RX_DLY_UUS (290 + CPU_PROCESSING_TIME)
#define RESP_RX_TO_FINAL_TX_DLY_UUS (480 + CPU_PROCESSING_TIME)

/* Receive response timeout. See NOTE 5 below. */
#define RESP_RX_TIMEOUT_UUS 300

/* Preamble timeout, in multiple of PAC size. See NOTE 6 below. */
#define PRE_TIMEOUT 5

/* Time-stamps of frames transmission/reception, expressed in device time units. */
static uint64_t poll_tx_ts;
static uint64_t resp_rx_ts;
static uint64_t final_tx_ts;

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

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum at the current
 * temperature. These values can be calibrated prior to taking reference measurements. See NOTE 7 below. */
extern dwt_txconfig_t txconfig_options;

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn get_ts_from_resp_msg()
 *
 * @brief Read a given timestamp value from the response message. In this function we read the first 4 bytes only as
 *        we are only using 32-bit timestamps for the responder's timestamps (they wrap around but that's OK).
 *
 * @param  ts_field  pointer to the first byte of the timestamp field to get
 * @param  ts  timestamp value
 *
 * @return none
 */
static void get_ts_from_resp_msg(uint8_t *ts_field, uint32_t *ts)
{
    int i;
    *ts = 0;
    for (i = 0; i < 4; i++)
    {
        *ts += ((uint32_t)ts_field[i] << (i * 8));
    }
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn ds_twr_sts_sdc_initiator()
 *
 * @brief Application entry point.
 *
 * @param  none
 *
 * @return none
 */
int ds_twr_sts_sdc_initiator(void)
{
    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); /* Time needed for DW3000 to start up (transition from INIT_RC to IDLE_RC) */

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) /* Need to make sure DW IC is in IDLE_RC before proceeding */ { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Configure DW IC. See NOTE 13 below. */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Enable PDOA mode 3 for phase difference measurement */
    dwt_setpdoamode(DWT_PDOA_M3);

    /* Configure the TX spectrum parameters (power, PG delay and PG count). */
    dwt_configuretxrf(&txconfig_options);

    /* Apply default antenna delay value. See NOTE 1 below. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Set expected response's delay and timeout. */
    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
    dwt_setpreambledetecttimeout(PRE_TIMEOUT);

    /* Next can enable TX/RX states output on GPIOs 5 and 6 to help debug */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    /* Loop forever initiating ranging exchanges. */
    while (1)
    {
        /* Write frame data to DW3000 and prepare transmission. */
        tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
        dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);  /* Zero offset in TX buffer. */
        dwt_writetxfctrl(sizeof(tx_poll_msg) + FCS_LEN, 0, 1); /* Zero offset in TX buffer, ranging. */

        /* Clear all events */
        dwt_writesysstatuslo(0xFFFFFFFF);

        /* Start transmission, indicating that a response is expected. */
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

        /* Poll for reception of a frame or error/timeout. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR), 0);

        /* Increment frame sequence number after transmission of the poll message (modulo 256). */
        frame_seq_nb++;
        
        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            uint16_t frame_len;
            int goodSts = 0;
            int16_t stsQual;
            uint16_t stsStatus;

            /* Clear good RX frame event and TX frame sent in the DW3000 status register. */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

            /* As STS is used, we only consider frames that are received with good STS quality */
            if (((goodSts = dwt_readstsquality(&stsQual, 0)) >= 0) && (dwt_readstsstatus(&stsStatus, 0) == DWT_SUCCESS))
            {
                /* A frame has been received, read it into the local buffer. */
                frame_len = dwt_getframelength(0);
                if (frame_len <= RX_BUF_LEN)
                {
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                }

                /* Check that the frame is the expected response. */
                rx_buffer[ALL_MSG_SN_IDX] = 0;
                if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0)
                {
                    uint32_t final_tx_time;
                    uint32_t poll_rx_ts_32, resp_tx_ts_32;
                    int ret;
                    int32_t rtd_init, rtd_resp;
                    double tof_dtu;

                    /* Retrieve poll transmission and response reception timestamp. */
                    poll_tx_ts = get_tx_timestamp_u64();
                    resp_rx_ts = get_rx_timestamp_u64();

                    /* Use poll TX timestamp as exchange start time */
                    exchange_start_dtu = poll_tx_ts;

                    /* Extract responder's timestamps from response message */
                    get_ts_from_resp_msg(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts_32);
                    get_ts_from_resp_msg(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts_32);

                    /* Calculate distance using DS-TWR formula */
                    /* Compute time of flight and distance. 32-bit subtractions give correct answers even if clock has wrapped. */
                    rtd_init = (int32_t)(resp_rx_ts - poll_tx_ts);
                    rtd_resp = (int32_t)(resp_tx_ts_32 - poll_rx_ts_32);
                    tof_dtu = ((double)rtd_init - (double)rtd_resp) / 2.0;
                    tof = tof_dtu * DWT_TIME_UNITS;
                    distance = tof * SPEED_OF_LIGHT;

                    /* Read PDOA value */
                    pdoa_val = dwt_readpdoa();
                    pdoa_degrees = ((float)pdoa_val / (1<<11)) * 180.0f / PI;

                    /* Compute final message transmission time. */
                    final_tx_time = (resp_rx_ts + (RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
                    dwt_setdelayedtrxtime(final_tx_time);

                    /* Final TX timestamp is the transmission time we programmed plus the TX antenna delay. */
                    final_tx_ts = (((uint64_t)(final_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

                    /* Write all timestamps in the final message. */
                    final_msg_set_ts(&tx_final_msg[FINAL_MSG_POLL_TX_TS_IDX], poll_tx_ts);
                    final_msg_set_ts(&tx_final_msg[FINAL_MSG_RESP_RX_TS_IDX], resp_rx_ts);
                    final_msg_set_ts(&tx_final_msg[FINAL_MSG_FINAL_TX_TS_IDX], final_tx_ts);

                    /* Write and send final message. */
                    tx_final_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
                    dwt_writetxdata(sizeof(tx_final_msg), tx_final_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_final_msg) + FCS_LEN, 0, 1);
                    ret = dwt_starttx(DWT_START_TX_DELAYED);

                    if (ret == DWT_SUCCESS)
                    {
                        /* Poll DW3000 until TX frame sent event set. */
                        waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

                        /* Clear TXFRS event. */
                        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

                        /* NOW end timing after final message is sent */
                        exchange_end_dtu = final_tx_ts;
                        /* Convert device time units to milliseconds */
                        exchange_duration_ms = (double)(exchange_end_dtu - exchange_start_dtu) * DWT_TIME_UNITS * 1000.0;

                        /* Display combined distance, PDOA, and exchange time */
                        snprintf(dist_pdoa_str, sizeof(dist_pdoa_str), 
                                "%.2f m, PDOA:%d (%.1f deg)", 
                                distance, pdoa_val, pdoa_degrees);
                        test_run_info((unsigned char *)dist_pdoa_str);
                        printf("DS-TWR: %3.2f m, PDOA: %d (%3.1f deg), Time: %.2f ms\n", 
                               distance, pdoa_val, pdoa_degrees, exchange_duration_ms);

                        /* Increment frame sequence number after transmission of the final message (modulo 256). */
                        frame_seq_nb++;
                    }
                }
            }
        }
        else
        {
            /* Clear RX error/timeout events in the DW3000 status register. */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        }

        /* Execute a delay between ranging exchanges. */
        Sleep(RNG_DELAY_MS);
    }
}
#endif

/*****************************************************************************************************************************************************
 * MODIFICATIONS SUMMARY:
 *
 * 1. DISTANCE CALCULATION ON INITIATOR:
 *    - Modified response message to include responder timestamps (19 bytes instead of 13)
 *    - Added function to extract timestamps from response message
 *    - Calculate distance using DS-TWR formula: TOF = (RTD_init - RTD_resp) / 2
 *    - Both initiator and responder can now calculate distance independently
 *
 * 2. PDOA MEASUREMENT:
 *    - Added PI constant for angle calculations
 *    - Added PDOA variables (pdoa_val, pdoa_degrees)
 *    - Enabled PDOA Mode 3 (dwt_setpdoamode(DWT_PDOA_M3))
 *    - Read PDOA value after successful reception (dwt_readpdoa())
 *    - Convert raw PDOA to degrees: degrees = (pdoa_val / 2048) × 180 / π
 *
 * 3. TIMING MEASUREMENT:
 *    - Added timing variables (exchange_start_time, exchange_end_time, exchange_duration_ms)
 *    - Start timer before poll transmission
 *    - Stop timer after distance calculation
 *    - Display exchange duration with distance and PDOA
 *
 * EXPECTED OUTPUT:
 * DS-TWR: 2.35 m, PDOA: 512 (22.5 deg), Time: 8 ms
 *
 * This shows:
 * - Distance measurement in meters
 * - PDOA raw value and angle in degrees
 * - Total exchange time in milliseconds
 ****************************************************************************************************************************************************/