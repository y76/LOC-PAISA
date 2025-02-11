/*! ----------------------------------------------------------------------------
 *  @file    simple_tx.c
 *  @brief   Simple TX example code with UART reception
 */

#include "deca_probe_interface.h"
#include <deca_device_api.h>
#include <deca_spi.h>
#include <example_selection.h>
#include <port.h>
#include <shared_defines.h>
#include <shared_functions.h>
#include <nrf_drv_uart.h>
#include <nrf_gpio.h>
#include <string.h>
#include <ctype.h>

#if defined(TEST_SIMPLE_TX)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "SIMPLE TX UART RX v1.0"

/* UART Configuration */
#define UART_RX_PIN  8  // P0.08
#define UART_TX_PIN 6   // P0.06
#define UART_BAUDRATE NRF_UART_BAUDRATE_115200
#define RX_BUF_SIZE 256
#define START_MARKER "PAISASTART:"
#define END_MARKER ":PAISAEND"

static nrf_drv_uart_t uart_instance = NRF_DRV_UART_INSTANCE(0);
static uint8_t rx_buf[RX_BUF_SIZE];
static uint8_t rxBuffer[RX_BUF_SIZE];
static uint16_t bufferIndex = 0;

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
                        if (isprint(buffer[k])) {  // If character is printable
                            printf("%c", buffer[k]);
                        } else {
                            printf("."); // Print dot for non-printable characters
                        }
                        if ((k - msgStart + 1) % 16 == 0) {
                            printf("\n");
                        }
                    }
                    printf("\n\nSide by side (16 bytes per line):\n");
                    for (uint16_t k = msgStart; k < j; k += 16) {
                        // Print hex values
                        for (uint16_t m = k; m < k + 16 && m < j; m++) {
                            printf("%02X ", buffer[m]);
                        }
                        // Pad with spaces if less than 16 bytes
                        for (uint16_t m = j; m < k + 16; m++) {
                            printf("   ");
                        }
                        printf("   ");  // Separator between hex and ASCII
                        
                        // Print ASCII values
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
                    //return;
                }
            }
        }
    }
}

/* Default communication configuration. Same as original */
static dwt_config_t config = {
    5,                /* Channel number. */
    DWT_PLEN_128,     /* Preamble length. Used in TX only. */
    DWT_PAC8,         /* Preamble acquisition chunk size. Used in RX only. */
    9,                /* TX preamble code. Used in TX only. */
    9,                /* RX preamble code. Used in RX only. */
    1,                /* 0 to use standard 8 symbol SFD, 1 to use non-standard 8 symbol, 2 for non-standard 16 symbol SFD and 3 for 4z 8 symbol SDF type */
    DWT_BR_6M8,       /* Data rate. */
    DWT_PHRMODE_STD,  /* PHY header mode. */
    DWT_PHRRATE_STD,  /* PHY header rate. */
    (129 + 8 - 8),    /* SFD timeout (preamble length + 1 + SFD length - PAC size). Used in RX only. */
    DWT_STS_MODE_OFF, /* No STS mode enabled */
    DWT_STS_LEN_64,   /* STS length */
    DWT_PDOA_M0       /* PDOA mode off */
};

static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E' };
#define BLINK_FRAME_SN_IDX 1
#define FRAME_LENGTH (sizeof(tx_msg) + FCS_LEN)

extern dwt_txconfig_t txconfig_options;

/* UART event handler */
void uart_event_handler(nrf_drv_uart_event_t *p_event, void *p_context)
{
    if (p_event->type == NRF_DRV_UART_EVT_RX_DONE)
    {
        // Store the byte in the buffer
        if (bufferIndex < RX_BUF_SIZE)
        {
            rxBuffer[bufferIndex++] = rx_buf[0];

            // Check if we have an end marker
            if (bufferIndex >= strlen(END_MARKER) &&
                memcmp(&rxBuffer[bufferIndex - strlen(END_MARKER)],
                      END_MARKER, strlen(END_MARKER)) == 0)
            {
                // We have a complete message, process it
                findMessage(rxBuffer, bufferIndex);
                // Reset buffer after processing
                bufferIndex = 0;
            }
        }
        else
        {
            // Buffer is full, reset it
            bufferIndex = 0;
        }

        // Start receiving next byte
        nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    }
}/*
void uart_event_handler(nrf_drv_uart_event_t *p_event, void *p_context)
{
    static uint8_t line_buffer[16];    // Buffer for holding one line of data
    static uint8_t buffer_pos = 0;     // Current position in line buffer
    
    if (p_event->type == NRF_DRV_UART_EVT_RX_DONE)
    {
        // Add byte to line buffer
        line_buffer[buffer_pos] = rx_buf[0];
        buffer_pos++;
        
        // When we have 16 bytes or hit a newline, print the formatted output
        if (buffer_pos == 16 || rx_buf[0] == '\n')
        {
            // Print hex values
            for (uint8_t i = 0; i < buffer_pos; i++) {
                printf("%02X ", line_buffer[i]);
            }
            
            // Pad with spaces if less than 16 bytes
            for (uint8_t i = buffer_pos; i < 16; i++) {
                printf("   ");
            }
            
            printf("   ");  // Separator between hex and ASCII
            
            // Print ASCII values
            for (uint8_t i = 0; i < buffer_pos; i++) {
                if (isprint(line_buffer[i])) {
                    printf("%c", line_buffer[i]);
                } else {
                    printf(".");
                }
            }
            
            printf("\n");  // New line after each 16-byte block
            buffer_pos = 0;  // Reset buffer position
        }
        
        // Start receiving next byte
        nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    }
}*/
/* Initialize UART for reception */
void uart_init(void)
{
    // Make sure UART pins are configured as GPIO inputs first
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
        .interrupt_priority = APP_IRQ_PRIORITY_HIGH  // Set high priority for UART interrupts
    };

    printf("Initializing UART RX...\n");
    printf("RX Pin: %d\n", uart_config.pselrxd);
    printf("Baudrate: %d\n", uart_config.baudrate);
    printf("IRQ Priority: %d\n", uart_config.interrupt_priority);
    
    // Initialize UART with event handler
    uint32_t err_code = nrf_drv_uart_init(&uart_instance, &uart_config, uart_event_handler);
    if (err_code != NRF_SUCCESS)
    {
        printf("UART initialization failed with error: %d\n", err_code);
        return;
    }
    
    // Enable UART receiver
    nrf_drv_uart_rx_enable(&uart_instance);
    
    // Start receiving first byte
    err_code = nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    if (err_code != NRF_SUCCESS)
    {
        printf("Failed to start RX with error: %d\n", err_code);
        return;
    }
    
    printf("UART RX initialization complete\n");
}

/**
 * Application entry point.
 */
int simple_tx(void)
{
    // Initialize UART first
    uart_init();
    
    #if USE_SPI2
        uint8_t sema_res;
    #endif
    uint32_t dev_id;

    /* Display application name on LCD. */
    test_run_info((unsigned char *)APP_NAME);

    /* Configure SPI rate, DW3000 supports up to 38 MHz */
    port_set_dw_ic_spi_fastrate();

    /* Reset DW IC */
    reset_DWIC(); /* Target specific drive of RSTn line into DW IC low for a period. */

    Sleep(2); // Time needed for DW3000 to start up

    /* Probe for the correct device driver. */
    dwt_probe((struct dwt_probe_s *)&dw3000_probe_interf);

    dev_id = dwt_readdevid();
    if (dev_id == (uint32_t)DWT_DW3720_PDOA_DEV_ID)
    {
        #if USE_SPI2
            change_SPI(SPI_2);
            port_set_dw_ic_spi_fastrate();
            reset_DWIC();
            Sleep(2);

            sema_res = dwt_ds_sema_status();
            if ((sema_res & (0x2)) == 0)
            {
                dwt_ds_sema_request();
            }
            else
            {
                test_run_info((unsigned char *)"SPI2 IS NOT FREE");
                while (1) { };
            }
        #endif
    }

    while (!dwt_checkidlerc()) { };

    if (dwt_initialise(DWT_DW_INIT) == DWT_ERROR)
    {
        test_run_info((unsigned char *)"INIT FAILED     ");
        while (1) { };
    }

    #if USE_SPI2 == 0
        dwt_setleds(DWT_LEDS_ENABLE | DWT_LEDS_INIT_BLINK);
    #endif

    /* Configure DW IC. */
    if (dwt_configure(&config))
    {
        test_run_info((unsigned char *)"CONFIG FAILED    ");
        while (1) { };
    }

    /* Configure the TX spectrum parameters (power and PG delay) */
    dwt_configuretxrf(&txconfig_options);

    /* Main loop - just transmit the frame when UART data is received */
    while (1)
    {
        // If we have received UART data, transmit the frame
      //  if (rx_data_len > 0)
      //  {
            /* Write frame data to DW IC and prepare transmission */
            dwt_writetxdata(FRAME_LENGTH - FCS_LEN, tx_msg, 0);
            dwt_writetxfctrl(FRAME_LENGTH, 0, 0);

            /* Start transmission */
            dwt_starttx(DWT_START_TX_IMMEDIATE);
            
            /* Poll DW IC until TX frame sent event set */
            waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

            /* Clear TX frame sent event */
            dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

         //   test_run_info((unsigned char *)"TX Frame Sent");

            /* Increment frame sequence number */
            tx_msg[BLINK_FRAME_SN_IDX]++;
            
            // Reset the UART buffer
          //  rx_data_len = 0;
      //  }
        
        // Small delay to prevent tight spinning
        Sleep(1);
    }
}

#endif