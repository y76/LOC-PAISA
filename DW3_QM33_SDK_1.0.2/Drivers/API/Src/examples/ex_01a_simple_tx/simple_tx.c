/*! ----------------------------------------------------------------------------
 *  @file    simple_tx.c
 *  @brief   Simple TX example code with UART reception and BLE advertising
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

#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "ble_advdata.h"
#include "app_error.h"
#include "nrf_log.h"

#if defined(TEST_SIMPLE_TX)

extern void test_run_info(unsigned char *data);

/* Example application name */
#define APP_NAME "SIMPLE TX v1.0"

/* BLE Configuration */
#define APP_BLE_CONN_CFG_TAG    1
#define APP_COMPANY_IDENTIFIER   0x0059  // Nordic Semiconductor ASA company identifier
#define NON_CONNECTABLE         true     // Set to false if you want connectable advertising

static uint8_t m_adv_handle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;
static uint8_t m_enc_advdata[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static ble_gap_adv_data_t m_adv_data;

/* UART Configuration */
#define UART_RX_PIN  8  // P0.08
#define UART_BAUDRATE NRF_UART_BAUDRATE_115200
#define RX_BUF_SIZE 256

static nrf_drv_uart_t uart_instance = NRF_DRV_UART_INSTANCE(0);
static uint8_t rx_buf[RX_BUF_SIZE];

/* Default communication configuration */
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
#define TX_DELAY_MS 500

extern dwt_txconfig_t txconfig_options;

/* Initialize BLE stack */
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure BLE stack using the default settings
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);
}

/* Initialize BLE advertising */
static void advertising_init(void)
{
    ret_code_t err_code;
    ble_advdata_t advdata;
    
    memset(&advdata, 0, sizeof(advdata));
    
    // Configure advertisement data
    advdata.name_type = BLE_ADVDATA_FULL_NAME;
    advdata.include_appearance = false;
    advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

    // Build and set advertising data
    memset(&m_adv_data, 0, sizeof(m_adv_data));
    
    err_code = ble_advdata_encode(&advdata, m_enc_advdata, &m_adv_data.adv_data.len);
    APP_ERROR_CHECK(err_code);
    
    m_adv_data.adv_data.p_data = m_enc_advdata;

    // Configure advertising parameters
    ble_gap_adv_params_t adv_params;
    memset(&adv_params, 0, sizeof(adv_params));

    if (NON_CONNECTABLE) {
        adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED;
    } else {
        adv_params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
    }
    
    adv_params.interval = BLE_GAP_ADV_INTERVAL_MIN;
    adv_params.duration = BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED;
    adv_params.primary_phy = BLE_GAP_PHY_1MBPS;

    err_code = sd_ble_gap_adv_set_configure(&m_adv_handle, &m_adv_data, &adv_params);
    APP_ERROR_CHECK(err_code);
}

/* Start advertising */
static void advertising_start(void)
{
    ret_code_t err_code;
    
    err_code = sd_ble_gap_adv_start(m_adv_handle, APP_BLE_CONN_CFG_TAG);
    APP_ERROR_CHECK(err_code);
}

/* UART event handler */
void uart_event_handler(nrf_drv_uart_event_t *p_event, void *p_context)
{
    if (p_event->type == NRF_DRV_UART_EVT_RX_DONE)
    {
        // Print received data
        printf("UART RX: ");
        for(int i = 0; i < p_event->data.rxtx.bytes; i++) {
            printf("%02X ", p_event->data.rxtx.p_data[i]);
        }
        printf("\r\n");
        
        // Start receiving again
        nrf_drv_uart_rx(&uart_instance, rx_buf, 1);  // Receive one byte at a time
    }
}

/* Initialize UART for reception */
void uart_init(void)
{
    nrf_drv_uart_config_t uart_config = {
        .pseltxd = NRF_UART_PSEL_DISCONNECTED,  // We don't need TX
        .pselrxd = UART_RX_PIN,                 // P0.08
        .pselcts = NRF_UART_PSEL_DISCONNECTED,  // No hardware flow control
        .pselrts = NRF_UART_PSEL_DISCONNECTED,  // No hardware flow control
        .p_context = NULL,
        .hwfc = NRF_UART_HWFC_DISABLED,
        .parity = NRF_UART_PARITY_EXCLUDED,
        .baudrate = NRF_UART_BAUDRATE_115200,   // Set explicit baudrate
        .interrupt_priority = UART_DEFAULT_CONFIG_IRQ_PRIORITY,
    };

    // Add debug prints
    printf("About to initialize UART with:\n");
    printf("RX Pin: %d\n", uart_config.pselrxd);
    printf("Baudrate setting: %d\n", uart_config.baudrate);
    
    uint32_t err_code = nrf_drv_uart_init(&uart_instance, &uart_config, uart_event_handler);
    APP_ERROR_CHECK(err_code);
    
    // Start receiving
    err_code = nrf_drv_uart_rx(&uart_instance, rx_buf, 1);
    APP_ERROR_CHECK(err_code);
    
    printf("UART initialization complete\n");
}

/**
 * Application entry point.
 */
int simple_tx(void)
{
    // Initialize BLE stack
    ble_stack_init();
    
    // Initialize and start advertising
    advertising_init();
    advertising_start();
    
    // Initialize UART
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

    /* Loop forever sending frames periodically */
    while (1)
    {
        /* Write frame data to DW IC and prepare transmission */
        dwt_writetxdata(FRAME_LENGTH - FCS_LEN, tx_msg, 0);
        dwt_writetxfctrl(FRAME_LENGTH, 0, 0);

        /* Start transmission */
        dwt_starttx(DWT_START_TX_IMMEDIATE);
        
        /* Poll DW IC until TX frame sent event set */
        waitforsysstatus(NULL, NULL, DWT_INT_TXFRS_BIT_MASK, 0);

        /* Clear TX frame sent event */
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

        test_run_info((unsigned char *)"TX Frame Sent");

        /* Execute a delay between transmissions */
        Sleep(TX_DELAY_MS);

        /* Increment frame sequence number */
        tx_msg[BLINK_FRAME_SN_IDX]++;
    }
}

#endif