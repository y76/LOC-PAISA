/*! ----------------------------------------------------------------------------
 *  @file    ds_twr_sts_sdc_responder.c
 *  @brief   Double-sided two-way ranging (DS TWR) with STS with SDC (STS-SDC) responder example code
 *           MODIFIED TO SEND TIMESTAMPS TO INITIATOR
 *
 * @author Decawave
 * @copyright SPDX-FileCopyrightText: Copyright (c) 2024 Qorvo US, Inc.
 *            SPDX-License-Identifier: LicenseRef-QORVO-2
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>

#if defined(TEST_DS_TWR_STS_SDC_RESPONDER)

extern void test_run_info(unsigned char *data);

/* Example application name and version to display on LCD screen. */
#define APP_NAME "DSTWR RE STS-SDC v1.0"

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

/* have some delay after each range */
#define DELAY_MS 980

/* Default antenna delay values for 64 MHz PRF. See NOTE 1 below. */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

/* Frames used in the ranging process. See NOTE 2 below. */
static uint8_t rx_poll_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x21 };
/* MODIFIED: Response message now includes timestamps (8 extra bytes) */
static uint8_t tx_resp_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0x10, 0x02, 0, 0, 0, 0, 0, 0, 0, 0 };
static uint8_t rx_final_msg[] = { 0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0x23, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/* Length of the common part of the message (up to and including the function code, see NOTE 2 below). */
#define ALL_MSG_COMMON_LEN 10

/* Index to access some of the fields in the frames involved in the process. */
#define ALL_MSG_SN_IDX            2
#define RESP_MSG_POLL_RX_TS_IDX   10
#define RESP_MSG_RESP_TX_TS_IDX   14
#define FINAL_MSG_POLL_TX_TS_IDX  10
#define FINAL_MSG_RESP_RX_TS_IDX  14
#define FINAL_MSG_FINAL_TX_TS_IDX 18

/* Frame sequence number, incremented after each transmission. */
static uint8_t frame_seq_nb = 0;

/* Buffer to store received messages. */
#define RX_BUF_LEN 24
static uint8_t rx_buffer[RX_BUF_LEN];

/* Hold copy of status register state here for reference so that it can be examined at a debug breakpoint. */
static uint32_t status_reg = 0;

/* Delay between frames, in UWB microseconds. See NOTE 4 below. */
#define POLL_RX_TO_RESP_TX_DLY_UUS 900
#define RESP_TX_TO_FINAL_RX_DLY_UUS 670

/* Receive final timeout. See NOTE 5 below. */
#define FINAL_RX_TIMEOUT_UUS 300

/* Preamble timeout, in multiple of PAC size. See NOTE 6 below. */
#define PRE_TIMEOUT 5

/* Timestamps of frames transmission/reception. */
static uint64_t poll_rx_ts;
static uint64_t resp_tx_ts;
static uint64_t final_rx_ts;

/* Hold copies of computed time of flight and distance here for reference. */
static double tof;
static double distance;

/* Values for the PG_DELAY and TX_POWER registers reflect the bandwidth and power of the spectrum. */
extern dwt_txconfig_t txconfig_options;

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn set_ts_in_resp_msg()
 *
 * @brief Write a given timestamp in the response message timestamp field. We only write 4 bytes (32-bit timestamp).
 *
 * @param  ts_field  pointer to the first byte of the timestamp field to write to
 * @param  ts  64-bit timestamp value (only lower 32 bits will be written)
 *
 * @return none
 */
static void set_ts_in_resp_msg(uint8_t *ts_field, uint64_t ts)
{
    int i;
    uint32_t ts_32 = (uint32_t)ts;
    for (i = 0; i < 4; i++)
    {
        ts_field[i] = (uint8_t)(ts_32 >> (i * 8));
    }
}

/*! ------------------------------------------------------------------------------------------------------------------
 * @fn ds_twr_sts_sdc_responder()
 *
 * @brief Application entry point.
 *
 * @param  none
 *
 * @return none
 */
int ds_twr_sts_sdc_responder(void)
{
    int range_ok = 0;
    
    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC();

    Sleep(2); // Time needed for DW3000 to start up

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    /* Configure DW IC. */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED     ");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power, PG delay and PG count) */
    dwt_configuretxrf(&txconfig_options);

    /* Apply default antenna delay value. */
    dwt_setrxantennadelay(RX_ANT_DLY);
    dwt_settxantennadelay(TX_ANT_DLY);

    /* Enable TX/RX states output on GPIOs 5 and 6 to help debug */
    dwt_setlnapamode(DWT_LNA_ENABLE | DWT_PA_ENABLE);

    /* Loop forever responding to ranging requests. */
    while (1)
    {
        /* turn off preamble timeout as the responder does not know when the poll is coming. */
        dwt_setpreambledetecttimeout(0);
        /* Clear reception timeout to start next ranging process. */
        dwt_setrxtimeout(0);

        /* Activate reception immediately. */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);

        /* Poll for reception of a frame or error/timeout. */
        waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR), 0);

        if (status_reg & DWT_INT_RXFCG_BIT_MASK)
        {
            uint16_t frame_len;
            int goodSts = 0;
            int16_t stsQual;
            uint16_t stsStatus;

            /* Clear good RX frame event in the DW3000 status register. */
            dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

            // as STS mode is used, we only consider frames that are received with good STS quality
            if (((goodSts = dwt_readstsquality(&stsQual, 0)) >= 0) && (dwt_readstsstatus(&stsStatus, 0) == DWT_SUCCESS))
            {
                /* A frame has been received, read it into the local buffer. */
                frame_len = dwt_getframelength(0);
                if (frame_len <= RX_BUF_LEN)
                {
                    dwt_readrxdata(rx_buffer, frame_len, 0);
                }

                /* Check that the frame is a poll sent by initiator. */
                rx_buffer[ALL_MSG_SN_IDX] = 0;
                if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) == 0)
                {
                    uint32_t resp_tx_time;
                    int ret;

                    /* Retrieve poll reception timestamp. */
                    poll_rx_ts = get_rx_timestamp_u64();

                    /* Set send time for response. */
                    resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
                    dwt_setdelayedtrxtime(resp_tx_time);

                    /* Response TX timestamp is the transmission time we programmed plus the TX antenna delay. */
                    resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

                    /* MODIFIED: Write timestamps into response message so initiator can calculate distance */
                    set_ts_in_resp_msg(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
                    set_ts_in_resp_msg(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

                    /* Set expected delay and timeout for final message reception. */
                    dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);
                    dwt_setrxtimeout(FINAL_RX_TIMEOUT_UUS);

                    /* Write and send the response message. */
                    tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
                    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
                    dwt_writetxfctrl(sizeof(tx_resp_msg) + FCS_LEN, 0, 1);
                    
                    /* Set preamble timeout for expected final frame from the initiator. */
                    dwt_setpreambledetecttimeout(PRE_TIMEOUT);
                    ret = dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED);

                    /* If dwt_starttx() returns an error, abandon this ranging exchange. */
                    if (ret == DWT_ERROR)
                    {
                        continue;
                    }

                    /* Poll for reception of expected "final" frame or error/timeout. */
                    waitforsysstatus(&status_reg, NULL, (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR), 0);

                    /* Increment frame sequence number after transmission of the response message (modulo 256). */
                    frame_seq_nb++;

                    if (status_reg & DWT_INT_RXFCG_BIT_MASK)
                    {
                        /* Clear good RX frame event and TX frame sent in the DW3000 status register. */
                        dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK | DWT_INT_TXFRS_BIT_MASK);

                        // as STS mode is used, we only consider frames that are received with good STS quality
                        if (((goodSts = dwt_readstsquality(&stsQual, 0)) >= 0)
                            && (dwt_readstsstatus(&stsStatus, 0) == DWT_SUCCESS))
                        {
                            /* A frame has been received, read it into the local buffer. */
                            frame_len = dwt_getframelength(0);
                            if (frame_len <= RX_BUF_LEN)
                            {
                                dwt_readrxdata(rx_buffer, frame_len, 0);
                            }

                            /* Check that the frame is a final message sent by initiator. */
                            rx_buffer[ALL_MSG_SN_IDX] = 0;
                            if (memcmp(rx_buffer, rx_final_msg, ALL_MSG_COMMON_LEN) == 0)
                            {
                                uint32_t poll_tx_ts, resp_rx_ts, final_tx_ts;
                                uint32_t poll_rx_ts_32, resp_tx_ts_32, final_rx_ts_32;
                                double Ra, Rb, Da, Db;
                                int64_t tof_dtu;

                                /* Retrieve response transmission and final reception timestamps. */
                                resp_tx_ts = get_tx_timestamp_u64();
                                final_rx_ts = get_rx_timestamp_u64();

                                /* Get timestamps embedded in the final message. */
                                final_msg_get_ts(&rx_buffer[FINAL_MSG_POLL_TX_TS_IDX], &poll_tx_ts);
                                final_msg_get_ts(&rx_buffer[FINAL_MSG_RESP_RX_TS_IDX], &resp_rx_ts);
                                final_msg_get_ts(&rx_buffer[FINAL_MSG_FINAL_TX_TS_IDX], &final_tx_ts);

                                /* Compute time of flight. 32-bit subtractions give correct answers even if clock has wrapped. */
                                poll_rx_ts_32 = (uint32_t)poll_rx_ts;
                                resp_tx_ts_32 = (uint32_t)resp_tx_ts;
                                final_rx_ts_32 = (uint32_t)final_rx_ts;
                                Ra = (double)(resp_rx_ts - poll_tx_ts);
                                Rb = (double)(final_rx_ts_32 - resp_tx_ts_32);
                                Da = (double)(final_tx_ts - resp_rx_ts);
                                Db = (double)(resp_tx_ts_32 - poll_rx_ts_32);
                                tof_dtu = (int64_t)((Ra * Rb - Da * Db) / (Ra + Rb + Da + Db));

                                tof = tof_dtu * DWT_TIME_UNITS;
                                distance = tof * SPEED_OF_LIGHT;

                                /* Display computed distance on LCD. */
                                char dist_str[20];
                                snprintf(dist_str, sizeof(dist_str), "%.2f m", distance);
                                test_run_info((unsigned char *)dist_str);
                                printf("RESP DIST: %3.2f m\n", distance);

                                range_ok = 1;
                            }
                        } // if STS good on the Final message reception
                    }
                    else
                    {
                        /* Clear RX error/timeout events in the DW3000 status register. */
                        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
                    }
                }
            } // if STS good on the Poll message reception
        }
        else
        {
            /* Clear RX error/timeout events in the DW3000 status register. */
            dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        }

        /* add some delay before next ranging exchange */
        if (range_ok)
        {
            range_ok = 0;
            Sleep(DELAY_MS);
        }
    }
}
#endif

/*****************************************************************************************************************************************************
 * NOTES:
 *
 * Super Deterministic Code (SDC):
 * Since the Ipatov preamble consists of a repeating sequence of the same Ipatov code, the time-of-arrival determined using it is
 * vulnerable to a collision with another packet. The STS uses a continually varying sequence which means the colliding packet will 
 * not line up with the desired signal. As a result, the TOA will be unaffected. SDC mode uses a code optimized for TOA performance
 * without requiring key management, though it does not provide security.
 *
 * 1. The sum of the values is the TX to RX antenna delay, experimentally determined by a calibration process.
 * 2. The messages comply with IEEE 802.15.4 standard MAC data frame encoding and ISO/IEC:24730-62:2013 standard.
 * 3. Source and destination addresses are hard coded constants - in real applications every device should have a unique ID.
 * 4. Delays between frames ensure proper synchronisation and accuracy of computed distance.
 * 5. Timeout duration must account for the length of the expected frame.
 * 6. Preamble timeout saves power when the expected message is not coming.
 * 7. In real applications, TX pulse bandwidth and power should be calibrated per device.
 * 8. Polled mode is used for simplicity - all status events can generate interrupts.
 * 9. Timestamps and delayed transmission time are in device time units.
 * 10. dwt_writetxdata() only copies (size - 2) bytes as checksum is automatically appended.
 * 11. If dwt_starttx() returns error due to too short delay, the ranging exchange is abandoned.
 * 12. High order byte of 40-bit timestamps is discarded - acceptable as timestamps are not separated by more than 2**32 device time units.
 * 13. dwt_configure is called to set desired configuration.
 *
 * MODIFICATIONS FOR DISTANCE CALCULATION ON BOTH SIDES:
 * - Response message now includes responder's timestamps (poll_rx_ts and resp_tx_ts) at bytes 10-17
 * - This allows the initiator to calculate distance independently
 * - Both initiator and responder now calculate and display distance
 * - Message format: Response message is now 19 bytes instead of 13 bytes
 ****************************************************************************************************************************************************/