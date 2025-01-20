/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "esp_log.h"
#include "nvs_flash.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#define CONFIG_EXAMPLE_EXTENDED_ADV 1
/* non ble imports */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"

#include "host/ble_hs_mbuf.h"

// ble config
static const char *tag = "NimBLE_BLE_PRPH";
static int bleprph_gap_event(struct ble_gap_event *event, void *arg);
#if CONFIG_EXAMPLE_RANDOM_ADDR
static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;
#else
static uint8_t own_addr_type;
#endif

#define EVALUATION_CONFIG   (0)

void ble_store_config_init(void);

// uart config
#define ECHO_UART_PORT_NUM (1)
#define ECHO_UART_BAUD_RATE (115200)
#define ECHO_TASK_STACK_SIZE (2048)

#define ECHO_TEST_TXD (6)
#define ECHO_TEST_RXD (7)
#define ECHO_TEST_RTS (-1)
#define ECHO_TEST_CTS (-1)

#define MSG_END_CHAR "MSGEND"
#define ACK_END_CHAR "ACKEND"
#define BRD_END_CHAR "BRDEND"
#define NSC_END_CHAR "NSCEND"

#define BUF_SIZE (155)

#define MBUF_DATA_SIZE 260 // Maximum advertising data length is 255 bytes

// Declare the mbuf pool variables
struct os_mbuf_pool large_mbuf_pool;
struct os_mempool large_mbuf_mempool;
uint8_t large_mbuf_buffer[OS_MEMPOOL_BYTES(10, MBUF_DATA_SIZE)];

void init_large_mbuf_pool(void) {
    int rc;

    // Initialize the memory pool for mbufs
    rc = os_mempool_init(
        &large_mbuf_mempool,
        10, // Number of mbufs in the pool
        MBUF_DATA_SIZE,
        large_mbuf_buffer,
        "large_mbuf_mempool"
    );
    assert(rc == 0);

    // Initialize the mbuf pool with the memory pool
    rc = os_mbuf_pool_init(
        &large_mbuf_pool,
        &large_mbuf_mempool,
        MBUF_DATA_SIZE,
        10
    );
    assert(rc == 0);
}

// beacon config
uint8_t beacon_raw[BUF_SIZE] = {
    0x00, 0x50, 0x41, 0x49, 0x53, 0x41,
    0x80, 0x00,
    0x00, 0x00,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x7c, 0xdf, 0xa1, 0xbc, 0x45, 0x74,
    0x7c, 0xdf, 0xa1, 0xbc, 0x45, 0x74,
    0xe0, 0x01,
    0x5c, 0x35, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
    0x64, 0x00,
    0x31, 0x04,
    0x00, 0x05, 0x50, 0x41, 0x49, 0x53, 0x41, // SSID: DB-PAISA
    0x01, 0x08, 0x8b, 0x96, 0x82, 0x84, 0x0c, 0x18, 0x30, 0x60,
    0x03, 0x01, 0x01,
    0x05, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
    0xdd, 0x0b, 0x00, 0x14, 0x6c, 0x08, // Initial Length: 75 bytes
};
uint8_t beacon_raw_size = 70; 
uint8_t beacon_raw_ascii[155];
uint8_t instance;


static void ext_bleprph_advertise_init(void) {
    struct ble_gap_ext_adv_params params;
    instance = 0;
    int rc;

    /* First check if any instance is already active */
    if (ble_gap_ext_adv_active(instance))
    {
        rc = ble_gap_ext_adv_stop(instance);
        assert(rc == 0);
    }

    /* use defaults for non-set params */
    memset(&params, 0, sizeof(params));

    /* enable connectable advertising */
    params.connectable = 0;
    params.scannable = 0;
    params.legacy_pdu = 0;

    /* advertise using random addr */
    params.own_addr_type = BLE_OWN_ADDR_PUBLIC;

    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    //params.tx_power = 127;
    params.sid = 1;

    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    /* configure instance 0 */
    rc = ble_gap_ext_adv_configure(instance, &params, NULL,
                                   bleprph_gap_event, NULL);
    assert(rc == 0);
}

/**
 * Enables advertising with the following parameters:
 *     o General discoverable mode.
 *     o Undirected connectable mode.
 */
static void ext_bleprph_advertise(const uint8_t* r_msg, int msglen) 
{
    struct os_mbuf *data;
    int rc;

    // Add debug print
    printf("Incoming UART message length: %d\n", msglen);
    printf("UART message content: ");
    for(int i = 0; i < msglen; i++) {
        printf("%02X ", r_msg[i]);
    }
    printf("\n");

    // ???????????????????????????????????????
    if (msglen > 23) {  
        printf("msglen: %d", msglen);
        printf("Warning: Message too long for advertisement packet\n");
        msglen = msglen/2;  // Truncate to maximum allowed
        printf("msglen: %d", msglen);
    }

    uint8_t total_adv_length = 3 + 2 + 2 + msglen;
    
    uint8_t* adv_data = malloc(total_adv_length);
    if (adv_data == NULL) {
        printf("Memory allocation failed!\n");
        return;
    }

    // Standard flags
    adv_data[0] = 0x02;           // Length of flags field
    adv_data[1] = 0x01;           // Flags data type
    adv_data[2] = 0x06;           // Flags value
    
    // Manufacturer specific data
    adv_data[3] = msglen + 3;     // Length of mfg specific data (payload + company ID)
    adv_data[4] = 0xFF;           // Manufacturer specific data type
    adv_data[5] = 0xE5;           // Company ID (LSB)
    adv_data[6] = 0x02;           // Company ID (MSB)

    memcpy(&adv_data[7], r_msg, msglen);

    // Debug print the final advertisement packet
    printf("Final advertisement packet (%d bytes): ", total_adv_length);
    for(int i = 0; i < total_adv_length; i++) {
        printf("%02X ", adv_data[i]);
    }
    printf("\n");

    // Make sure advertising is stopped before setting new data
    ble_gap_ext_adv_stop(instance);

    data = os_msys_get_pkthdr(&large_mbuf_pool, 0);
    if (!data) {
        printf("Failed to allocate mbuf!\n");
        free(adv_data);
        return;
    }

    rc = os_mbuf_append(data, adv_data, total_adv_length);
    if (rc != 0) {
        printf("Failed to append to mbuf! rc=%d\n", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_set_data(instance, data);
    if (rc != 0) {
        printf("Failed to set advertisement data! rc=%d\n", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_start(instance, 0, 0);
    if (rc != 0) {
        printf("Failed to start advertising! rc=%d\n", rc);
        free(adv_data);
        return;
    }

    free(adv_data);
    printf("Advertisement started successfully\n");
}
// msg: ["DP-RES" (6) || n_dev(12) || num_of_n_usr(1) || n_usr_list(12*num_of_n_usr) || M_SRV_URL(10) || attest_result(1) || attest_time(4) || signature(variable)]


static void
ble_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    int rc;

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Tell the controller to filter duplicates; we don't want to process
     * repeated advertisements from the same device.
     */
    disc_params.filter_duplicates = 1;

    /**
     * Perform a passive scan.  I.e., don't send follow-up scan requests to
     * each advertiser.
     */
    disc_params.passive = 1;

    /* Use defaults for the rest of the parameters. */
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      bleprph_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery procedure; rc=%d\n",
                    rc);
    }
}

static void
bleprph_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
ble_app_set_addr(void)
{
    ble_addr_t addr;
    int rc;

    /* generate new non-resolvable private address */
    rc = ble_hs_id_gen_rnd(0, &addr);
    assert(rc == 0);

    /* set generated address */
    rc = ble_hs_id_set_rnd(addr.val);

    assert(rc == 0);
}

static void
bleprph_on_sync(void)
{
    int rc;

#if CONFIG_EXAMPLE_RANDOM_ADDR
    /* Generate a non-resolvable private address. */
    ble_app_set_addr();
#endif

    /* Make sure we have proper identity address set (public preferred) */
#if CONFIG_EXAMPLE_RANDOM_ADDR
    rc = ble_hs_util_ensure_addr(1);
#else
    rc = ble_hs_util_ensure_addr(0);
#endif
    assert(rc == 0);

    /* Figure out address to use while advertising (no privacy for now) */
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    /* Printing ADDR */
    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL);

    MODLOG_DFLT(INFO, "Device Address: ");
    // print_addr(addr_val);
    MODLOG_DFLT(INFO, "\n");
    /* Begin scanning. */

    // ext_bleprph_advertise(); // commented this out
    ext_bleprph_advertise_init();
    ble_scan();
}

void bleprph_host_task(void *param)
{
    ESP_LOGD(tag, "BLE Host Task Started");
    /* This function will return only when nimble_port_stop() is executed */
    nimble_port_run();

    nimble_port_freertos_deinit();
}


bool contains_DB_PAISA(const uint8_t *data, size_t data_length) {
    uint8_t db_paisa_id[] = {0x44, 0x50, 0x2d, 0x52, 0x45, 0x51};
    // Iterate through the data array
    if (data_length < sizeof(db_paisa_id)) {
        return false;
    }

    for (size_t i = 0; i <= data_length - sizeof(db_paisa_id); ++i) {
        // Check if the current position matches the start of the series
        if (memcmp(data + i, db_paisa_id, sizeof(db_paisa_id)) == 0) {
            return true; // Series found
        }
    }
    return false; // Series not found
}

static void send_uart_data(const uint8_t *data, uint8_t data_len)
{
    uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
    uint8_t db_paisa_buf[] = "DB-PAISA:MSGEND";

    ESP_LOGI(tag, "send data to NXP (%d)", data_len);
    uart_write_bytes(ECHO_UART_PORT_NUM, db_paisa_buf, sizeof(db_paisa_buf));
}

static int bleprph_gap_event(struct ble_gap_event *event, void *arg) 
{
    int rc;
    struct ble_gap_conn_desc desc;
    struct ble_hs_adv_fields fields;

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGE(tag, "code should not reach here");
        case BLE_GAP_EVENT_DISC:
            rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                        event->disc.length_data);
            if (rc != 0) {
                return 0;
            }
            return 0;
        case BLE_GAP_EVENT_EXT_DISC:
            /* An advertisment report was received during GAP discovery. */
            const struct ble_gap_ext_disc_desc *disc = (struct ble_gap_ext_disc_desc *)&event->disc;
            
            // if received buffer contains DB-PAISA, call uart task and start advertising
            // note currently uart task has advertise in it 
            if(disc->legacy_event_type == 0 && contains_DB_PAISA(disc->data, disc->length_data)){
                rc = ble_gap_disc_cancel();
                assert(rc == 0);

                const uint8_t *u8p;
                u8p = disc->addr.val;
                
                send_uart_data(disc->data, disc->length_data);
            }

            return 0;
        
    }
    return 0;
}


void uart_init(void)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));
    ESP_LOGD(tag, "UART init done");
}

static void eval_task()
{
    static const char *EVAL_TASK_TAG = "EVAL_TASK";
    char *data = (char *)malloc(BUF_SIZE+1);
    const char db_paisa_buf[] = "DB-PAISA";
    strcpy(data, db_paisa_buf);
    strcpy(data+strlen(db_paisa_buf), MSG_END_CHAR);

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        uart_write_bytes(ECHO_UART_PORT_NUM, data, strlen(data));
    }
}

static void uart_task()
{
    static const char *RX_TASK_TAG = "UART_TASK";
    static const char *BT_TASK_TAG = "BT_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE+1);
    
    while (1) {
        const int rxBytes = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        if (rxBytes > 6) {
            data[rxBytes] = 0;
            
            ESP_LOGI(RX_TASK_TAG, "\n=== Message Components Breakdown ===\n");
            
            // Print n_dev (32 bytes)
            ESP_LOGI(RX_TASK_TAG, "n_dev (32 bytes):");
            for(int i = 0; i < 32; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");
            
            // Print curTS (4 bytes)
            ESP_LOGI(RX_TASK_TAG, "curTS (4 bytes):");
            for(int i = 32; i < 36; i++) {
                printf("%02X ", data[i]);
            }
            uint32_t curTs = *(uint32_t*)(data + 32);
            printf(" (Decimal: %lu)\n", curTs);
            
            // Calculate signature length and position
            int sig_start = 36;
            int url_len = data[rxBytes - 6];
            int sig_len = rxBytes - (sig_start + url_len + 6);
            
            // Print signature
            ESP_LOGI(RX_TASK_TAG, "signature (length %d):", sig_len);
            for(int i = sig_start; i < sig_start + sig_len; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");
            
            // Print M_SRV_URL
            ESP_LOGI(RX_TASK_TAG, "M_SRV_URL (%d bytes): ", url_len);
            for(int i = sig_start + sig_len; i < sig_start + sig_len + url_len; i++) {
                printf("%c", data[i]);
            }
            printf("\n");
            
            // Print attest_result (1 byte)
            ESP_LOGI(RX_TASK_TAG, "attest_result (1 byte): %02X", data[rxBytes - 5]);
            
            // Print time_attest (4 bytes)
            ESP_LOGI(RX_TASK_TAG, "time_attest (4 bytes):");
            for(int i = rxBytes - 4; i < rxBytes; i++) {
                printf("%02X ", data[i]);
            }
            uint32_t time_attest = *(uint32_t*)(data + rxBytes - 4);
            printf(" (Decimal: %lu)\n", time_attest);
            
            // Print full message
            ESP_LOGI(RX_TASK_TAG, "\n=== Complete Raw Message ===");
            ESP_LOGI(RX_TASK_TAG, "Full message (length %d):", rxBytes);
            for(int i = 0; i < rxBytes; i++) {
                printf("%02X ", data[i]);
            }
            printf("\n");

            if (strncmp((const char *)data + rxBytes - strlen(BRD_END_CHAR), BRD_END_CHAR, strlen(BRD_END_CHAR)) == 0)
            {
                ext_bleprph_advertise(data, rxBytes/* - strlen(BRD_END_CHAR)*/);

                // wait 3 seconds
                ESP_LOGI(tag, "Read %d bytes: '%s'", rxBytes, data);
                vTaskDelay(3000 / portTICK_PERIOD_MS);
                int rc = ble_gap_ext_adv_stop(instance);
                assert(rc == 0);

                ble_scan();
            }
            else if (strncmp((const char *)data + rxBytes - strlen(NSC_END_CHAR), NSC_END_CHAR, strlen(NSC_END_CHAR)) == 0)
            {
                // This message is assumed to be sent out to server, but we do not implement the server side
                ESP_LOGI(RX_TASK_TAG, "[%lld us] Temperature: %f", esp_timer_get_time(), *(float *)data);
            }
            else
            {
                int rc;
                  if (ble_gap_ext_adv_active(instance)) {
                        rc = ble_gap_ext_adv_stop(instance);
                    assert(rc == 0);
                    }
                ESP_LOGI(RX_TASK_TAG, "OTHER CASE STARTING TO ADVERTISE");
                ext_bleprph_advertise(data, rxBytes/* - strlen(BRD_END_CHAR)*/);
                 vTaskDelay(1000 / portTICK_PERIOD_MS);
              ///  int rc = ble_gap_ext_adv_stop(instance);
               /// assert(rc == 0);
                ///ESP_LOGI(RX_TASK_TAG, "OTHER CASE STOPPING TO ADVERTISE");
            }
        }
    }
    free(data);
}



void app_main(void)
{
    int rc;

    /* Initialize NVS — it is used to store PHY calibration data */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

// uart init
    uart_init();

// nimble init
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }
    /* Initialize the NimBLE host configuration. */
    ble_hs_cfg.reset_cb = bleprph_on_reset;
    ble_hs_cfg.sync_cb = bleprph_on_sync;
    ble_hs_cfg.sm_io_cap = CONFIG_EXAMPLE_IO_TYPE;

    #ifdef CONFIG_EXAMPLE_MITM
        ble_hs_cfg.sm_mitm = 1;
    #endif
    #ifdef CONFIG_EXAMPLE_USE_SC
        ble_hs_cfg.sm_sc = 1;
    #else
        ble_hs_cfg.sm_sc = 0;
    #endif
init_large_mbuf_pool();

    nimble_port_freertos_init(bleprph_host_task); //scanning

// UART Task to read responses from IoT main device (NXP)
    xTaskCreate(uart_task, "uart_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);

// Eval Task only needed for evaluation
#if EVALUATION_CONFIG
    xTaskCreate(eval_task, "eval_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);
#endif
}