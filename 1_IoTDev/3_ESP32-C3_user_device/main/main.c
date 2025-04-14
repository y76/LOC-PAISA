/*
 * ESP32 BLE Extended Advertisement Receiver
 * Companion code for DB-PAISA project
 */

#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include <ctype.h>
#include "nimble/nimble_port_freertos.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/pk.h"
#include "mbedtls/base64.h"
#include "mbedtls/error.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"

#define INSTANCE_ID 0

#define EXAMPLE_ESP_WIFI_SSID "THE BEST TP LINK"
#define EXAMPLE_ESP_WIFI_PASS "ZOTzot2023"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
static int s_retry_num = 0;
static uint8_t saved_n_dev[32] = {0};
static uint32_t cur_timestamp = 0;

// Configure the maximum advertisement size
#define MAX_ADV_DATA_LEN 255 // Maximum extended advertisement data length
struct __attribute__((packed)) ble_gap_disc_desc_debug
{
    uint8_t event_type;
    uint8_t length_data;
    uint8_t addr_type;
    uint8_t reserved;
    uint8_t addr[6];
    uint16_t flags;
    uint8_t phy_primary;
    uint8_t phy_secondary;
    uint32_t extended_flags;
    uint16_t more_flags;
    uint8_t *data; 
};

typedef struct
{
    uint32_t key0;
    uint32_t key1;
    uint32_t key2;
    uint32_t key3;
} sts_key_t;

typedef struct
{
    uint32_t iv0;
    uint32_t iv1;
    uint32_t iv2;
    uint32_t iv3;
} sts_iv_t;

typedef struct
{
    uint32_t key0;
    uint32_t key1;
    uint32_t key2;
    uint32_t key3;
    uint32_t key4;
    uint32_t key5;
    uint32_t key6;
    uint32_t key7;
} aes_key_t;

static const char *TAG = "BLE_RECEIVER";
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static uint8_t own_addr_type;
#define BUF_SIZE (155)
#define UART_BUF_SIZE (255)
#define MBUF_DATA_SIZE 260 
#define ECHO_UART_PORT_NUM (1)
#define ECHO_TEST_TXD (6)
#define ECHO_TEST_RXD (7)
#define ECHO_TEST_RTS (-1)
#define ECHO_TEST_CTS (-1)
#define ECHO_UART_BAUD_RATE (115200)
struct os_mbuf_pool large_mbuf_pool;
struct os_mempool large_mbuf_mempool;
uint8_t large_mbuf_buffer[OS_MEMPOOL_BYTES(10, MBUF_DATA_SIZE)];

static uint8_t global_n_dev[32];
static uint32_t global_timestamp;
static uint8_t global_url[100];
static uint8_t global_url_len;
static uint8_t global_attest_result;
static uint32_t global_time_attest;
static uint8_t global_signature[256];
static size_t global_signature_len;
static uint32_t global_device_id = 0;

#define CERT_BUFFER_SIZE 2048 
#define PUBKEY_BUFFER_SIZE 1024

static char certificate_of_manufacturer[CERT_BUFFER_SIZE] = {0};
static char public_key_manufacturer[PUBKEY_BUFFER_SIZE] = {0};
static void IRAM_ATTR uart_rx_isr_handler(void *arg);

void uart_init(void)
{
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
    ESP_LOGI(TAG, "UART init done");
}

static void uart_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UART task started");
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE + 1);
    
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART buffer");
        vTaskDelete(NULL);
    }

    while (1) {
        const int rxBytes = uart_read_bytes(ECHO_UART_PORT_NUM, data, BUF_SIZE, 20 / portTICK_PERIOD_MS);
        
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            
            ESP_LOGI(TAG, "Received %d bytes from UART:", rxBytes);
        
            printf("HEX: ");
            for (int i = 0; i < rxBytes; i++) {
                printf("%02X ", data[i]);
                if ((i + 1) % 16 == 0) {
                    printf("\n     ");
                }
            }
            printf("\n");
            
            printf("ASCII: ");
            for (int i = 0; i < rxBytes; i++) {
                if (data[i] >= 32 && data[i] <= 126) {
                    printf("%c", data[i]);
                } else {
                    printf(".");
                }
            }
            printf("\n");
            
            if (rxBytes > 6 && memcmp(&data[rxBytes - 6], "MSGEND", 6) == 0) {
                ESP_LOGI(TAG, "Received message with MSGEND marker");               
                uint8_t response[] = "ACK: Message received";
            } 
        }
    }
    free(data);
    vTaskDelete(NULL);
}

static void send_uart_data(const uint8_t *data, uint8_t data_len)
{
    const char *start_marker = "PAISASTART:";
    const char *end_marker = ":PAISAEND";
    size_t start_marker_len = strlen(start_marker);
    size_t end_marker_len = strlen(end_marker);
    ESP_LOGI(TAG, "DATA_LEN: %d", data_len);
    char *buf = (char *)malloc(UART_BUF_SIZE);
    memset(buf, 0, UART_BUF_SIZE);

    size_t pos = 0;

    memcpy(buf, start_marker, start_marker_len);
    pos += start_marker_len;

    if (data && data_len > 0)
    {
        memcpy(buf + pos, data, data_len);
        pos += data_len;
    }

    memcpy(buf + pos, end_marker, end_marker_len);
    pos += end_marker_len;

    printf("Sending message (hex): ");
    for (size_t i = 0; i < pos; i++)
    {
        printf("%02X", (unsigned char)buf[i]);
    }
    printf("\nSending message (ASCII): ");
    for (size_t i = 0; i < pos; i++)
    {
        printf("%c", buf[i]);
    }
    printf("\n");

    ESP_LOGI(TAG, "Sending complete message:");
    uart_write_bytes(ECHO_UART_PORT_NUM, buf, pos);

    free(buf);
}

void init_large_mbuf_pool(void)
{
    int rc;

    rc = os_mempool_init(
        &large_mbuf_mempool,
        10,
        MBUF_DATA_SIZE,
        large_mbuf_buffer,
        "large_mbuf_mempool");
    assert(rc == 0);

    rc = os_mbuf_pool_init(
        &large_mbuf_pool,
        &large_mbuf_mempool,
        MBUF_DATA_SIZE,
        10);
    assert(rc == 0);
}

// Initialize extended advertising
static void ext_adv_init(void)
{
    struct ble_gap_ext_adv_params params;
    int rc;

    if (ble_gap_ext_adv_active(INSTANCE_ID))
    {
        rc = ble_gap_ext_adv_stop(INSTANCE_ID);
        assert(rc == 0);
    }

    memset(&params, 0, sizeof(params));

    params.connectable = 0;
    params.scannable = 0;
    params.legacy_pdu = 0;
    params.own_addr_type = own_addr_type;
    params.primary_phy = BLE_HCI_LE_PHY_1M;
    params.secondary_phy = BLE_HCI_LE_PHY_2M;
    params.sid = 1;
    params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MIN;

    rc = ble_gap_ext_adv_configure(INSTANCE_ID, &params, NULL,
                                   ble_gap_event, NULL);
    assert(rc == 0);
}

static void send_ble_message(uint8_t *data, size_t data_len)
{
    const char *prefix = "LOC-RESP";
    size_t prefix_len = strlen(prefix);
    size_t total_data_len = prefix_len + data_len;

    uint8_t total_adv_length = 3 + 2 + 2 + total_data_len; // Flags + header + company ID + prefix + data

    uint8_t *adv_data = malloc(total_adv_length);
    if (adv_data == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        return;
    }

    adv_data[0] = 0x02;
    adv_data[1] = 0x01;
    adv_data[2] = 0x06;

    adv_data[3] = total_data_len + 3;
    adv_data[4] = 0xFF;
    adv_data[5] = 0xE5;
    adv_data[6] = 0x02;

    memcpy(&adv_data[7], prefix, prefix_len);
    memcpy(&adv_data[7 + prefix_len], data, data_len);

    printf("  Complete packet (hex):\n    ");
    for (size_t i = 0; i < total_adv_length; i++) {
        printf("%02X ", adv_data[i]);
        if ((i + 1) % 16 == 0 && i < total_adv_length - 1) {
            printf("\n    ");
        }
    }
    printf("\n");

    struct os_mbuf *mbuf;
    int rc;

    if (ble_gap_ext_adv_active(INSTANCE_ID))
    {
        rc = ble_gap_ext_adv_stop(INSTANCE_ID);
        assert(rc == 0);
    }

    mbuf = os_mbuf_get_pkthdr(&large_mbuf_pool, 0);
    if (!mbuf)
    {
        ESP_LOGE(TAG, "Failed to allocate mbuf!");
        free(adv_data);
        return;
    }

    rc = os_mbuf_append(mbuf, adv_data, total_adv_length);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to append to mbuf! rc=%d", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_set_data(INSTANCE_ID, mbuf);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set advertisement data! rc=%d", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_start(INSTANCE_ID, 100, 0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising! rc=%d", rc);
        free(adv_data);
        return;
    }

    free(adv_data);
    ESP_LOGI(TAG, "BLE message sent (size: %d bytes)", total_data_len);
}

// Function to send LOC-RESP advertisement
static void send_loc_resp(void)
{
    struct os_mbuf *data;
    int rc;
    const char *loc_resp = "LOC-RESP";

    uint8_t total_adv_length = 3 + 2 + 2 + strlen(loc_resp); 

    uint8_t *adv_data = malloc(total_adv_length);
    if (adv_data == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        return;
    }

    adv_data[0] = 0x02; 
    adv_data[1] = 0x01; 
    adv_data[2] = 0x06; 

    adv_data[3] = strlen(loc_resp) + 3; 
    adv_data[4] = 0xFF;                
    adv_data[5] = 0xE5;                
    adv_data[6] = 0x02;                 

    // Add LOC-RESP
    memcpy(&adv_data[7], loc_resp, strlen(loc_resp));

    if (ble_gap_ext_adv_active(INSTANCE_ID))
    {
        rc = ble_gap_ext_adv_stop(INSTANCE_ID);
        assert(rc == 0);
    }

    data = os_mbuf_get_pkthdr(&large_mbuf_pool, 0);
    if (!data)
    {
        ESP_LOGE(TAG, "Failed to allocate mbuf!");
        free(adv_data);
        return;
    }

    rc = os_mbuf_append(data, adv_data, total_adv_length);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to append to mbuf! rc=%d", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_set_data(INSTANCE_ID, data);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to set advertisement data! rc=%d", rc);
        free(adv_data);
        return;
    }

    rc = ble_gap_ext_adv_start(INSTANCE_ID, 100, 0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Failed to start advertising! rc=%d", rc);
        free(adv_data);
        return;
    }

    free(adv_data);
    ESP_LOGI(TAG, "LOC-RESP advertisement started");
}

static void print_adv_data(const uint8_t *data, uint16_t length)
{
    ESP_LOGI(TAG, "\n=== Message Components Breakdown ===");

    int offset = 7 + 9; 
    ESP_LOGI(TAG, "n_dev (32 bytes): ");
    for (int i = 0; i < 32; i++)
    {
        printf("%02X ", data[offset + i]);
        global_n_dev[i] = data[offset + i];
    }
    printf("\n");
    offset += 32;

    uint32_t curTs = data[offset] | (data[offset + 1] << 8) |
                     (data[offset + 2] << 16) | (data[offset + 3] << 24);
    global_timestamp = curTs;
    ESP_LOGI(TAG, "curTS (4 bytes): %02X %02X %02X %02X (Decimal: %lu)",
             data[offset], data[offset + 1], data[offset + 2], data[offset + 3],
             (unsigned long)curTs);
    offset += 4;

    int sig_start = offset;
    int sig_end = length - 6 - data[length - 6];
    int sig_len = sig_end - sig_start;
    ESP_LOGI(TAG, "signature (%d bytes): ", sig_len);
    for (int i = sig_start; i < sig_end; i++)
    {
        printf("%02X ", data[i]);
        global_signature[i - sig_start] = data[i];
    }
    global_signature_len = sig_len;
    printf("\n");

    uint8_t url_len = data[length - 6];
    global_url_len = url_len;
    ESP_LOGI(TAG, "M_SRV_URL (%d bytes): ", url_len);
    for (int i = 0; i < url_len; i++)
    {
        printf("%c", data[length - 6 - url_len + i]);
        global_url[i] = data[length - 6 - url_len + i];
    }
    global_url[url_len] = '\0';
    printf("\n");

    ESP_LOGI(TAG, "m_srv_url_len (1 byte): %02X (Decimal: %u)", url_len, url_len);

    global_attest_result = data[length - 5];
    ESP_LOGI(TAG, "attest_result (1 byte): %02X", global_attest_result);

    uint32_t time_attest = data[length - 4] | (data[length - 3] << 8) |
                           (data[length - 2] << 16) | (data[length - 1] << 24);
    global_time_attest = time_attest;
    ESP_LOGI(TAG, "time_attest (4 bytes): %02X %02X %02X %02X (Decimal: %lu)",
             data[length - 4], data[length - 3], data[length - 2], data[length - 1],
             (unsigned long)time_attest);

}
static void ble_scanner_init(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    struct ble_gap_disc_params scan_params = {
        .itvl = BLE_GAP_SCAN_ITVL_MS(100),
        .window = BLE_GAP_SCAN_WIN_MS(50),
        .filter_duplicates = 0, 
        .limited = 0,           
        .passive = 0,          
        .filter_policy = 0      
    };

    rc = ble_gap_disc(own_addr_type, 0, 
                      &scan_params,
                      ble_gap_event, NULL);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error initiating scan; rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Scanner started successfully");
}

// Helper function to check for LOC-PAISA in advertisement data
static bool contains_loc_paisa(const uint8_t *data)
{
    const char *loc_paisa = "LOC-PAISA";
    const size_t marker_len = strlen(loc_paisa);
    uint8_t total_length = data[3];

    if (total_length < 7 + marker_len)
    {
        return false;
    }

    for (size_t i = 7; i <= total_length - marker_len; i++)
    {
        if (memcmp(&data[i], loc_paisa, marker_len) == 0)
        {
            return true;
        }
    }
    return false;
}
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        }
        else
        {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

static char response_buffer[4096];
static int response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_HEADER:
        if (evt->header_key != NULL)
        {
            response_len = 0;
            memset(response_buffer, 0, sizeof(response_buffer));
        }
        break;

    case HTTP_EVENT_ON_DATA:
        if (response_len + evt->data_len < sizeof(response_buffer))
        {
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buffer[response_len] = 0;
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        if (response_len < sizeof(response_buffer))
        {
            response_buffer[response_len] = 0;
        }
        break;

    case HTTP_EVENT_ERROR:
        response_len = 0;
        memset(response_buffer, 0, sizeof(response_buffer));
        break;

    default:
        break;
    }
    return ESP_OK;
}

static char certificate_of_device[CERT_BUFFER_SIZE] = {0};
static char public_key[PUBKEY_BUFFER_SIZE] = {0};

void fix_certificate_format(const char *src_cert, char *fixed_cert, size_t fixed_cert_size)
{
    const char *src = src_cert;
    char *dst = fixed_cert;

    while (*src)
    {
        if (*src == '\\' && *(src + 1) == 'n')
        {
            if (dst - fixed_cert < fixed_cert_size - 1) 
            {
                *dst = '\n'; 
                dst++;
            }
            src += 2; 
        }
        else
        {
            if (dst - fixed_cert < fixed_cert_size - 1) 
            {
                *dst = *src;
                dst++;
            }
            src++;
        }
    }

    *dst = '\0'; 
}

#define CERT_SIZE 2048 

void extract_public_key_from_cert(const char *certificate, char *output_key, size_t output_size, const char *key_owner)
{
    if (strlen(certificate) == 0)
    {
        ESP_LOGE(TAG, "Certificate for %s is empty, cannot extract public key!", key_owner);
        return;
    }

    char fixed_cert[CERT_SIZE];
    fix_certificate_format(certificate, fixed_cert, CERT_SIZE);

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *)fixed_cert, strlen(fixed_cert) + 1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to parse %s certificate, error: -0x%X", key_owner, -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    mbedtls_pk_context *pk = &cert.pk;

    if (!mbedtls_pk_can_do(pk, MBEDTLS_PK_ECKEY))
    {
        ESP_LOGE(TAG, "%s certificate does not contain an EC public key!", key_owner);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    unsigned char buf[PUBKEY_BUFFER_SIZE];
    size_t olen = 0;
    ret = mbedtls_pk_write_pubkey_pem(pk, buf, PUBKEY_BUFFER_SIZE);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to write %s public key in PEM format, error: -0x%X", key_owner, -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    strncpy(output_key, (const char *)buf, output_size - 1);
    output_key[output_size - 1] = '\0';

    ESP_LOGI(TAG, "Extracted %s EC Public Key:\n%s", key_owner, output_key);

    mbedtls_x509_crt_free(&cert);
}

void extract_public_keys(void)
{
    extract_public_key_from_cert(certificate_of_device, public_key, PUBKEY_BUFFER_SIZE, "device");

    extract_public_key_from_cert(certificate_of_manufacturer, public_key_manufacturer, PUBKEY_BUFFER_SIZE, "manufacturer");

    ESP_LOGI(TAG, "\n=== Public Keys Extraction Complete ===");
    ESP_LOGI(TAG, "Device Public Key Status: %s", strlen(public_key) > 0 ? "Extracted" : "Failed");
    ESP_LOGI(TAG, "Manufacturer Public Key Status: %s", strlen(public_key_manufacturer) > 0 ? "Extracted" : "Failed");
}

void extract_public_key()
{
    if (strlen(certificate_of_device) == 0)
    {
        ESP_LOGI(TAG, "Certificate is empty, cannot extract public key!");
        return;
    }

    char fixed_cert[CERT_SIZE];
    fix_certificate_format(certificate_of_device, fixed_cert, CERT_SIZE);

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *)fixed_cert, strlen(fixed_cert) + 1);
    if (ret != 0)
    {
        ESP_LOGI(TAG, "Failed to parse certificate, error: -0x%X", -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    mbedtls_pk_context *pk = &cert.pk;

    if (!mbedtls_pk_can_do(pk, MBEDTLS_PK_ECKEY))
    {
        ESP_LOGE(TAG, "Certificate does not contain an EC public key!");
        mbedtls_x509_crt_free(&cert);
        return;
    }

    unsigned char buf[PUBKEY_BUFFER_SIZE];
    size_t olen = 0;
    ret = mbedtls_pk_write_pubkey_pem(pk, buf, PUBKEY_BUFFER_SIZE);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to write public key in PEM format, error: -0x%X", -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    strncpy(public_key, (const char *)buf, PUBKEY_BUFFER_SIZE - 1);
    public_key[PUBKEY_BUFFER_SIZE - 1] = '\0'; 

    ESP_LOGI(TAG, "Extracted EC Public Key:\n%s", public_key);

    mbedtls_x509_crt_free(&cert);
}

void display_paisa_info(const char *url)
{
    ESP_LOGI(TAG, "Device URL: %s", url);

    esp_http_client_config_t config = {
        .host = "bit.ly",   //hardcoded for testing, just pass in the extracted url
        .path = "/3EJadxK",
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .cert_pem = NULL,
        .skip_cert_common_name_check = true,
        .timeout_ms = 10000,
        .max_redirection_count = 5,
        .user_agent = "Mozilla/5.0",
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status = %d", status);

        ESP_LOGI(TAG, "Actual Response Length: %d bytes", response_len);
        ESP_LOGI(TAG, "RAW MESSAGE: %.*s", response_len, response_buffer);

        const char *device_id_start = strstr(response_buffer, "device_id:");
        if (device_id_start != NULL)
        {
            device_id_start += strlen("device_id:");
            char *device_id_end = strchr(device_id_start, '\n');
            if (device_id_end != NULL)
            {
                char device_id_str[32] = {0};
                size_t id_len = device_id_end - device_id_start;
                if (id_len < sizeof(device_id_str))
                {
                    strncpy(device_id_str, device_id_start, id_len);
                    device_id_str[id_len] = '\0';
                    global_device_id = (uint32_t)strtoul(device_id_str, NULL, 10);
                    ESP_LOGI(TAG, "Device ID extracted: %lu", (unsigned long)global_device_id);
                }
                else
                {
                    ESP_LOGE(TAG, "Device ID string too long!");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Device ID end not found");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Device ID not found in response");
        }

        const char *cert_device_start = strstr(response_buffer, "certificate_of_device:");
        if (cert_device_start != NULL)
        {
            cert_device_start += strlen("certificate_of_device:");
            const char *cert_device_end = strstr(cert_device_start, "-----END CERTIFICATE-----");
            if (cert_device_end != NULL)
            {
                size_t cert_length = cert_device_end - cert_device_start + strlen("-----END CERTIFICATE-----");
                if (cert_length < CERT_BUFFER_SIZE - 1)
                {
                    strncpy(certificate_of_device, cert_device_start, cert_length);
                    certificate_of_device[cert_length] = '\0';
                    ESP_LOGI(TAG, "Certificate of device stored successfully!");
                }
                else
                {
                    ESP_LOGE(TAG, "Certificate of device too large for buffer!");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Certificate of device end not found");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Certificate of device not found in response");
        }

        const char *cert_mfr_start = strstr(response_buffer, "certificate_of_manufacturer:");
        if (cert_mfr_start != NULL)
        {
            cert_mfr_start += strlen("certificate_of_manufacturer:");
            const char *cert_mfr_end = strstr(cert_mfr_start, "-----END CERTIFICATE-----");
            if (cert_mfr_end != NULL)
            {
                size_t cert_length = cert_mfr_end - cert_mfr_start + strlen("-----END CERTIFICATE-----");
                if (cert_length < CERT_BUFFER_SIZE - 1)
                {
                    strncpy(certificate_of_manufacturer, cert_mfr_start, cert_length);
                    certificate_of_manufacturer[cert_length] = '\0';
                    ESP_LOGI(TAG, "Certificate of manufacturer stored successfully!");
                }
                else
                {
                    ESP_LOGE(TAG, "Certificate of manufacturer too large for buffer!");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Certificate of manufacturer end not found");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Certificate of manufacturer not found in response");
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

static void extract_and_display_url(const uint8_t *data, uint8_t length)
{
    const char *https_marker = "https://";
    const size_t marker_len = strlen(https_marker);
    char url_buffer[100] = {0};

    for (int i = 0; i < length - marker_len; i++)
    {
        if (memcmp(&data[i], https_marker, marker_len) == 0)
        {
            size_t remaining_length = length - i;
            size_t url_length = remaining_length < sizeof(url_buffer) ? remaining_length : sizeof(url_buffer) - 1;
            memcpy(url_buffer, &data[i], url_length);
            url_buffer[url_length] = '\0';

            display_paisa_info(url_buffer);
            break;
        }
    }
}
#include "mbedtls/pk.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/aes.h"
#include "esp_log.h"

#define TAG "ENCRYPTION"

static int encrypt_with_public_key(const uint8_t *data, size_t data_len,
                                   const char *public_key_pem,
                                   uint8_t *encrypted_data, size_t *encrypted_len,
                                   uint8_t *out_ephemeral_public, 
                                   uint8_t *out_iv)             
{
    mbedtls_ecdh_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context peer_pk;
    const char *pers = "gen_key";
    int ret = 0;
    uint8_t *decrypted = NULL; 
    uint8_t shared_secret[32];
    uint8_t iv[12] = {0};
    uint8_t tag[16];

    esp_fill_random(&iv, sizeof(iv));

    mbedtls_ecdh_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&peer_pk);

    decrypted = (uint8_t *)malloc(data_len);
    if (decrypted == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate decryption buffer");
        ret = -1;
        goto cleanup;
    }

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to seed RNG: -0x%x", -ret);
        goto cleanup;
    }

    ret = mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to setup ECDH: -0x%x", -ret);
        goto cleanup;
    }

    ret = mbedtls_ecdh_gen_public(&ctx.MBEDTLS_PRIVATE(grp),
                                  &ctx.MBEDTLS_PRIVATE(d),
                                  &ctx.MBEDTLS_PRIVATE(Q),
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to generate keypair: -0x%x", -ret);
        goto cleanup;
    }

    ret = mbedtls_pk_parse_public_key(&peer_pk, (const unsigned char *)public_key_pem,
                                      strlen(public_key_pem) + 1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to parse public key: -0x%x", -ret);
        goto cleanup;
    }

    const mbedtls_ecp_keypair *peer_keypair = mbedtls_pk_ec(peer_pk);
    ret = mbedtls_ecdh_compute_shared(&ctx.MBEDTLS_PRIVATE(grp),
                                      &ctx.MBEDTLS_PRIVATE(z),
                                      &peer_keypair->MBEDTLS_PRIVATE(Q),
                                      &ctx.MBEDTLS_PRIVATE(d),
                                      mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to compute shared secret: -0x%x", -ret);
        goto cleanup;
    }

    size_t secret_len;
    ret = mbedtls_mpi_write_binary(&ctx.MBEDTLS_PRIVATE(z), shared_secret, sizeof(shared_secret));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to extract shared secret: -0x%x", -ret);
        goto cleanup;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, shared_secret, 256);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to set GCM key: -0x%x", -ret);
        goto cleanup_gcm;
    }

    ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, data_len,
                                    iv, sizeof(iv), NULL, 0,
                                    data, encrypted_data,
                                    16, tag);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to encrypt: -0x%x", -ret);
        goto cleanup_gcm;
    }

    memcpy(encrypted_data + data_len, tag, 16);
    *encrypted_len = data_len + 16;

    ESP_LOGI(TAG, "Encryption successful");

    size_t public_key_len;
    ret = mbedtls_ecp_point_write_binary(&ctx.MBEDTLS_PRIVATE(grp),
                                         &ctx.MBEDTLS_PRIVATE(Q),
                                         MBEDTLS_ECP_PF_UNCOMPRESSED,
                                         &public_key_len,
                                         out_ephemeral_public,
                                         65);

    ESP_LOGI(TAG, "Ephemeral public key export:");
    ESP_LOGI(TAG, "Return code: -0x%x", -ret);
    ESP_LOGI(TAG, "Exported key length: %zu", public_key_len);

    ESP_LOGI(TAG, "First 8 bytes of ephemeral public key:");
    for (int i = 0; i < 65; i++)
    {
        printf("%02x ", out_ephemeral_public[i]);
    }
    printf("\n");

    memcpy(out_iv, iv, 12);

    ESP_LOGI(TAG, "Shared secret:");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", shared_secret[i]);
    }
    printf("\n");

    ESP_LOGI(TAG, "Encrypted data + tag (%d bytes):", *encrypted_len);
    for (int i = 0; i < *encrypted_len; i++)
    {
        printf("%02x", encrypted_data[i]);
    }
    printf("\n");

    memcpy(tag, encrypted_data + data_len, 16);

    ret = mbedtls_gcm_auth_decrypt(&gcm, data_len,
                                   iv, sizeof(iv),
                                   NULL, 0,
                                   tag, 16,
                                   encrypted_data, 
                                   decrypted);

    if (ret == 0)
    {
        ESP_LOGI(TAG, "Decryption successful");
        ESP_LOGI(TAG, "Decrypted text: ");
        for (int i = 0; i < data_len; i++)
        {
            printf("%c", decrypted[i]);
        }
        printf("\n");

        ESP_LOGI(TAG, "Decrypted data (hex):");
        for (int i = 0; i < data_len; i++)
        {
            printf("%02x", decrypted[i]);
        }
        printf("\n");
    }
    else
    {
        ESP_LOGE(TAG, "Decryption failed: -0x%x", -ret);
    }

cleanup_gcm:
    mbedtls_gcm_free(&gcm);
cleanup:
    if (decrypted)
        free(decrypted); 
    mbedtls_ecdh_free(&ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_pk_free(&peer_pk);
    return ret;
}

static bool is_timestamp_valid(const uint8_t *data)
{
    int offset = 7 + 9 + 32; 

    uint32_t curTs = data[offset] |
                     (data[offset + 1] << 8) |
                     (data[offset + 2] << 16) |
                     (data[offset + 3] << 24);

    if (curTs <= cur_timestamp)
    {
        ESP_LOGI(TAG, "Timestamp not valid. Received: %lu, Current: %lu",
                 (unsigned long)curTs,
                 (unsigned long)cur_timestamp);
        return false;
    }

    cur_timestamp = curTs;
    return true;
}

static bool verify_msganno_signature(const uint8_t *data, uint16_t length)
{
    uint8_t url_hash[32];
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    mbedtls_sha256_starts(&sha256, 0);
    mbedtls_sha256_update(&sha256, global_url, global_url_len);
    mbedtls_sha256_finish(&sha256, url_hash);

    uint8_t signed_data[256];
    size_t signed_data_len = 0;

    memcpy(signed_data, global_n_dev, 32);
    signed_data_len += 32;

    memcpy(signed_data + signed_data_len, &global_timestamp, 4);
    signed_data_len += 4;

    memcpy(signed_data + signed_data_len, &global_device_id, 4);
    signed_data_len += 4;

    memcpy(signed_data + signed_data_len, url_hash, 32);
    signed_data_len += 32;

    memcpy(signed_data + signed_data_len, &global_attest_result, 1);
    signed_data_len += 1;

    memcpy(signed_data + signed_data_len, &global_time_attest, 4);
    signed_data_len += 4;

    uint8_t digest[32];
    mbedtls_sha256_init(&sha256);
    mbedtls_sha256_starts(&sha256, 0);
    mbedtls_sha256_update(&sha256, signed_data, signed_data_len);
    mbedtls_sha256_finish(&sha256, digest);

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)public_key,
                                          strlen(public_key) + 1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to parse public key for signature verification");
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                            digest, sizeof(digest),
                            global_signature, global_signature_len);

    mbedtls_pk_free(&pk);

    if (ret == 0)
    {
        ESP_LOGI(TAG, "Message announcement signature verified successfully");
        return true;
    }
    else
    {
        ESP_LOGE(TAG, "Message announcement signature verification failed. Error code: %d", ret);
        return false;
    }
}

static bool verify_manifest_signature(void) {
    const char *sig_start = strstr(response_buffer, "signature_of_manifest:");
    if (!sig_start) {
        ESP_LOGE(TAG, "No signature found in manifest");
        return false;
    }

    size_t content_len = sig_start - response_buffer;

    uint8_t digest[32];
    mbedtls_sha256_context sha256;
    mbedtls_sha256_init(&sha256);
    mbedtls_sha256_starts(&sha256, 0);
    mbedtls_sha256_update(&sha256, (const unsigned char *)response_buffer, content_len);
    mbedtls_sha256_finish(&sha256, digest);
    mbedtls_sha256_free(&sha256);

    ESP_LOGI(TAG, "Content being hashed (ASCII):");
    for(size_t i = 0; i < content_len; i++) {
        if(response_buffer[i] >= 32 && response_buffer[i] <= 126) {
            printf("%c", response_buffer[i]);
        } else if(response_buffer[i] == '\n') {
            printf("\n");
        } else {
            printf(".");
        }
    }
    printf("\n\n");

    ESP_LOGI(TAG, "Content being hashed (HEX):");
    for(size_t i = 0; i < content_len; i++) {
        printf("%02x ", (unsigned char)response_buffer[i]);
        if((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    ESP_LOGI(TAG, "Resulting hash:");
    for(int i = 0; i < 32; i++) {
        printf("%02x", digest[i]);
    }
    printf("\n");

    sig_start += strlen("signature_of_manifest:");
    const char *sig_end = strchr(sig_start, '\n');
    if (!sig_end) {
        ESP_LOGE(TAG, "Could not find end of signature");
        return false;
    }

    size_t sig_b64_len = sig_end - sig_start;
    char *clean_sig = malloc(sig_b64_len + 1);
    size_t clean_pos = 0;

    for(size_t i = 0; i < sig_b64_len; i++) {
        if(sig_start[i] == '\\' && i + 1 < sig_b64_len && sig_start[i + 1] == 'n') {
            i++; 
        } else {
            clean_sig[clean_pos++] = sig_start[i];
        }
    }
    clean_sig[clean_pos] = '\0';

    ESP_LOGI(TAG, "Raw signature with newlines: %s", sig_start);
    ESP_LOGI(TAG, "Cleaned signature for base64: %s", clean_sig);

    unsigned char *decoded_sig = malloc(clean_pos); 
    size_t decoded_len;
    
    int ret = mbedtls_base64_decode(decoded_sig, clean_pos, &decoded_len,
                                   (const unsigned char *)clean_sig, clean_pos);
    
    if(ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed");
        free(clean_sig);
        free(decoded_sig);
        return false;
    }

    ESP_LOGI(TAG, "Decoded signature length: %d", decoded_len);
    ESP_LOGI(TAG, "Decoded signature bytes:");
    for(size_t i = 0; i < decoded_len; i++) {
        printf("%02x", decoded_sig[i]);
    }
    printf("\n");

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    ret = mbedtls_pk_parse_public_key(&pk, 
                                     (const unsigned char *)public_key_manufacturer,
                                     strlen(public_key_manufacturer) + 1);
    
    if(ret != 0) {
        ESP_LOGE(TAG, "Failed to parse public key");
        free(clean_sig);
        free(decoded_sig);
        mbedtls_pk_free(&pk);
        return false;
    }

    ret = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256,
                           digest, sizeof(digest),
                           decoded_sig, decoded_len);

    mbedtls_pk_free(&pk);
    free(clean_sig);
    free(decoded_sig);

    ESP_LOGI(TAG, "Manifest signature verification SUCCESS");
    return true;
}
static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_EXT_DISC:
    {
        const struct ble_gap_disc_desc *disc = &event->disc;
        struct ble_gap_disc_desc_debug *debug_disc = (struct ble_gap_disc_desc_debug *)disc;

        if (debug_disc->data != NULL && debug_disc->data[4] == 0xFF && contains_loc_paisa(debug_disc->data))
        {

            if (!is_timestamp_valid(debug_disc->data))
            {
                return 0;
            }

            ESP_LOGI(TAG, "Found LOC-PAISA advertisement");

            uint8_t total_length = debug_disc->data[3];
            ESP_LOGI(TAG, "Processing advertisement with length: %d", total_length);
            print_adv_data(debug_disc->data, total_length + 4);

            char addr_str[18];
            snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                     disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
            ESP_LOGI(TAG, "Sender address: %s", addr_str);

            extract_and_display_url(debug_disc->data, total_length);
            if (strlen(certificate_of_device) > 0 && strlen(certificate_of_manufacturer) > 0)
            {
                extract_public_keys();

                if (!verify_manifest_signature())
                {
                    ESP_LOGE(TAG, "Manifest signature verification failed");
                    return 0;
                }
                ESP_LOGI(TAG, "Manifest signature verified successfully");

                if (!verify_msganno_signature(debug_disc->data, total_length))
                {
                    ESP_LOGE(TAG, "Message announcement signature verification failed");
                    return 0;
                }
                ESP_LOGI(TAG, "Message announcement signature verified successfully");

                ble_gap_disc_cancel();

                sts_key_t sts_key;
                sts_iv_t sts_iv;

                // for now, use generic hardcoded values for development
                const char *key_string = "HELLOP@ISA2024"; // 13 chars
                const char *iv_string = "PAISA@HELLO2024"; // 13 chars

                uint16_t src_addr;
                uint16_t dst_addr;

                // Convert strings to key values (4 bytes per value)
                sts_key.key0 = *((uint32_t *)&key_string[0]); // HELL
                sts_key.key1 = *((uint32_t *)&key_string[4]); // OP@I
                sts_key.key2 = *((uint32_t *)&key_string[8]); // SA20
                sts_key.key3 = *((uint32_t *)&key_string[9]); // A202

                sts_iv.iv0 = *((uint32_t *)&iv_string[0]); // PAIS
                sts_iv.iv1 = *((uint32_t *)&iv_string[4]); // A@HE
                sts_iv.iv2 = *((uint32_t *)&iv_string[8]); // LLO2
                sts_iv.iv3 = *((uint32_t *)&iv_string[9]); // O202

                // Generate random values for key and IV using ESP32's hardware RNG
                esp_fill_random(&sts_key, sizeof(sts_key));
                esp_fill_random(&sts_iv, sizeof(sts_iv));
                esp_fill_random(&src_addr, sizeof(src_addr));
                esp_fill_random(&dst_addr, sizeof(dst_addr));

                ESP_LOGI(TAG, "STS KEY: 0x%08lX 0x%08lX 0x%08lX 0x%08lX",
                         sts_key.key0, sts_key.key1, sts_key.key2, sts_key.key3);

                ESP_LOGI(TAG, "STS IV: 0x%08lX 0x%08lX 0x%08lX 0x%08lX",
                         sts_iv.iv0, sts_iv.iv1, sts_iv.iv2, sts_iv.iv3);

                aes_key_t aes_key = {0x41424344, 0x45464748, 0x49505152, 0x53545556, 0x00000000, 0x00000000, 0x00000000, 0x00000000}; /*Initialize 128bits key (actually 256 but padded)*/
                ESP_LOGI(TAG, "AES KEY: 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX 0x%08lX",
                         aes_key.key0, aes_key.key1, aes_key.key2, aes_key.key3,
                         aes_key.key4, aes_key.key5, aes_key.key6, aes_key.key7);

             
                uint8_t crypto_data[36]; // 16 (STS key) + 16 (STS IV) + 2 (src_addr) + 2 (dst_addr)
                memcpy(crypto_data, &sts_key, sizeof(sts_key));
                memcpy(crypto_data + sizeof(sts_key), &sts_iv, sizeof(sts_iv));
                memcpy(crypto_data + sizeof(sts_key) + sizeof(sts_iv), &src_addr, sizeof(src_addr));
                memcpy(crypto_data + sizeof(sts_key) + sizeof(sts_iv) + sizeof(src_addr), &dst_addr, sizeof(dst_addr));
                send_uart_data(crypto_data, sizeof(crypto_data));

            
                uint8_t encrypted_sts_data[256];
                size_t encrypted_length = sizeof(encrypted_sts_data);
                uint8_t complete_message[256 + 65 + 12 + 32];
                uint8_t ephemeral_public[65];
                uint8_t iv[12];

                int ret = encrypt_with_public_key(crypto_data, sizeof(crypto_data),
                                                  public_key,
                                                  encrypted_sts_data, &encrypted_length,
                                                  ephemeral_public, // Pass buffer for public key
                                                  iv);              // Pass buffer for IV

                if (ret == 0)
                {
                    size_t total_length = 0;
                    memcpy(complete_message, global_n_dev, 32); 
                    total_length += 32;
                    memcpy(complete_message + total_length, ephemeral_public, 65);
                    total_length += 65;
                    memcpy(complete_message + total_length, iv, 12);
                    total_length += 12;
                    memcpy(complete_message + total_length, encrypted_sts_data, encrypted_length);
                    total_length += encrypted_length;

                    send_ble_message(complete_message, total_length);
                }

                vTaskDelay(pdMS_TO_TICKS(1000));
                ble_scanner_init();
            }
            else
            {
                ESP_LOGE(TAG, "One or both certificates not available!");
                if (strlen(certificate_of_device) == 0)
                {
                    ESP_LOGE(TAG, "Device certificate missing");
                }
                if (strlen(certificate_of_manufacturer) == 0)
                {
                    ESP_LOGE(TAG, "Manufacturer certificate missing");
                }
            }
        }
        break;
    }

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Discovery complete event (type 8) - restarting scan");
        ble_scanner_init();
        break;

    default:
        ESP_LOGI(TAG, "Other unhandled BLE GAP event: %d", event->type);
        break;
    }
    return 0;
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_sync_cb(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    ext_adv_init();

    ble_scanner_init();

    ESP_LOGI(TAG, "BLE stack synchronized");
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_sta();

    nimble_port_init();

    init_large_mbuf_pool();

    ble_hs_cfg.sync_cb = ble_sync_cb; 
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;

    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    nimble_port_freertos_init(ble_host_task);
    xTaskCreate(uart_task, "uart_task", 4096, NULL, 10, NULL);


    ESP_LOGI(TAG, "BLE initialization completed");
}