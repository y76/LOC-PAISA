/*
 * ESP32 BLE Extended Advertisement Receiver
 * Companion code for DB-PAISA project
 */

/*

Next steps.
- [ ] Add LOC-PAISA to announcement on IoT Side
- [ ] on user side LOOK for LOC-PAISA in message
- [ ] Generate STS key
- [ ] Send STS information UWB BOARD -> UART -> Start ranging
- [ ] Encrypt sts key
- [ ] bluetooth broadcast BACK to the other way
- [ ] receive message on IoT side
- [ ] send message to NXP board
- [ ] Decrypt on NXP board
- [ ] send via UART STS key to UWB Board -> Start Ranging
- [ ] Stop ranging

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
    uint8_t *data; // This should be a pointer
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
#define MBUF_DATA_SIZE 260 // Maximum advertising data length is 255 bytes
#define ECHO_UART_PORT_NUM (1)
#define ECHO_TEST_TXD (6)
#define ECHO_TEST_RXD (7)
#define ECHO_TEST_RTS (-1)
#define ECHO_TEST_CTS (-1)
#define ECHO_UART_BAUD_RATE (115200)
// Declare the mbuf pool variables
struct os_mbuf_pool large_mbuf_pool;
struct os_mempool large_mbuf_mempool;
uint8_t large_mbuf_buffer[OS_MEMPOOL_BYTES(10, MBUF_DATA_SIZE)];

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

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));
    ESP_LOGD(TAG, "UART init done");
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

    // Build complete message: START_MARKER + DATA + END_MARKER
    size_t pos = 0;

    // Copy start marker
    memcpy(buf, start_marker, start_marker_len);
    pos += start_marker_len;

    // Copy data if present
    if (data && data_len > 0)
    {
        memcpy(buf + pos, data, data_len);
        pos += data_len;
    }

    // Copy end marker
    memcpy(buf + pos, end_marker, end_marker_len);
    pos += end_marker_len;

    // Print complete message before sending
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

    // Initialize the memory pool for mbufs
    rc = os_mempool_init(
        &large_mbuf_mempool,
        10, // Number of mbufs in the pool
        MBUF_DATA_SIZE,
        large_mbuf_buffer,
        "large_mbuf_mempool");
    assert(rc == 0);

    // Initialize the mbuf pool with the memory pool
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

    /* Check if instance is already active */
    if (ble_gap_ext_adv_active(INSTANCE_ID))
    {
        rc = ble_gap_ext_adv_stop(INSTANCE_ID);
        assert(rc == 0);
    }

    /* Set default parameters */
    memset(&params, 0, sizeof(params));

    /* Set advertising parameters */
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

    // Calculate total advertisement length
    uint8_t total_adv_length = 3 + 2 + 2 + total_data_len; // Flags + header + company ID + prefix + data

    uint8_t *adv_data = malloc(total_adv_length);
    if (adv_data == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        return;
    }

    // Standard flags
    adv_data[0] = 0x02;
    adv_data[1] = 0x01;
    adv_data[2] = 0x06;

    // Manufacturer specific data
    adv_data[3] = total_data_len + 3;
    adv_data[4] = 0xFF;
    adv_data[5] = 0xE5;
    adv_data[6] = 0x02;

    // Add prefix and payload data
    memcpy(&adv_data[7], prefix, prefix_len);
    memcpy(&adv_data[7 + prefix_len], data, data_len);

    // Rest of function same as before
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

    // Calculate total advertisement length
    uint8_t total_adv_length = 3 + 2 + 2 + strlen(loc_resp); // Flags + header + company ID + LOC-RESP

    uint8_t *adv_data = malloc(total_adv_length);
    if (adv_data == NULL)
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        return;
    }

    // Standard flags
    adv_data[0] = 0x02; // Length of flags field
    adv_data[1] = 0x01; // Flags data type
    adv_data[2] = 0x06; // Flags value

    // Manufacturer specific data
    adv_data[3] = strlen(loc_resp) + 3; // Length of mfg specific data
    adv_data[4] = 0xFF;                 // Manufacturer specific data type
    adv_data[5] = 0xE5;                 // Company ID (LSB)
    adv_data[6] = 0x02;                 // Company ID (MSB)

    // Add LOC-RESP
    memcpy(&adv_data[7], loc_resp, strlen(loc_resp));

    // Stop any ongoing advertising
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

    // Start advertising for a limited time (e.g., 1000ms)
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

// Function to print advertisement data in a readable format
static void print_adv_data(const uint8_t *data, uint16_t length)
{
    ESP_LOGI(TAG, "Advertisement data (length %d):", length);

    // Print entire raw data first
    ESP_LOGI(TAG, "Full raw data:");
    for (int i = 0; i < length; i++)
    {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");

    // Now we know it's structured as:
    // [02 01 06] [XX FF Company_ID Data...]
    if (length >= 3 && data[0] == 0x02 && data[1] == 0x01)
    {
        ESP_LOGI(TAG, "Flags field: %02X", data[2]);
    }

    // Find manufacturer data (should start at index 3)
    if (length > 4 && data[4] == 0xFF)
    {
        uint16_t company_id = data[5] | (data[6] << 8);
        ESP_LOGI(TAG, "Manufacturer Data:");
        ESP_LOGI(TAG, "  Company ID: 0x%04X", company_id);
        ESP_LOGI(TAG, "  Data (%d bytes):", length - 7);

        // Print manufacturer data in hex
        for (int i = 7; i < length; i++)
        {
            printf("%02X ", data[i]);
            if ((i - 6) % 16 == 0)
                printf("\n");
        }
        printf("\n");

        // Try ASCII interpretation
        ESP_LOGI(TAG, "ASCII interpretation:");
        for (int i = 7; i < length; i++)
        {
            if (isprint(data[i]))
            {
                printf("%c", data[i]);
            }
            else
            {
                printf(".");
            }
        }
        printf("\n");
    }
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
        .filter_duplicates = 0, // Don't filter duplicates
        .limited = 0,           // Don't limit discovery
        .passive = 0,           // Use active scanning
        .filter_policy = 0      // No filtering
    };

    // Start regular scanning instead of extended
    rc = ble_gap_disc(own_addr_type, 0, // Duration (0 = scan continuously)
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

    // Need at least 7 bytes for header plus enough space for the marker
    if (total_length < 7 + marker_len)
    {
        return false;
    }

    // Check if the data after manufacturer specific data contains our marker
    // Start checking from position 7 (after flags and manufacturer data header)
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

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() */
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

// Add this global buffer to store the complete response
static char response_buffer[4096];
static int response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_HEADER:
        // Reset buffer at the start of a new request
        if (evt->header_key != NULL)
        {
            response_len = 0;
            memset(response_buffer, 0, sizeof(response_buffer));
        }
        break;

    case HTTP_EVENT_ON_DATA:
        // Prevent buffer overflow and duplicates
        if (response_len + evt->data_len < sizeof(response_buffer))
        {
            memcpy(response_buffer + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_buffer[response_len] = 0; // Null terminate
        }
        break;

    case HTTP_EVENT_ON_FINISH:
        // Ensure buffer is null-terminated
        if (response_len < sizeof(response_buffer))
        {
            response_buffer[response_len] = 0;
        }
        break;

    case HTTP_EVENT_ERROR:
        // Clear buffer on error
        response_len = 0;
        memset(response_buffer, 0, sizeof(response_buffer));
        break;

    default:
        break;
    }
    return ESP_OK;
}
#define CERT_BUFFER_SIZE 2048 // Adjust if needed
static char certificate_of_device[CERT_BUFFER_SIZE] = {0};
// Buffer to store the extracted public key
#define PUBKEY_BUFFER_SIZE 1024
static char public_key[PUBKEY_BUFFER_SIZE] = {0};

void fix_certificate_format(const char *src_cert, char *fixed_cert, size_t fixed_cert_size)
{
    const char *src = src_cert;
    char *dst = fixed_cert;

    while (*src)
    {
        if (*src == '\\' && *(src + 1) == 'n')
        {
            if (dst - fixed_cert < fixed_cert_size - 1) // Make sure we don't overflow the buffer
            {
                *dst = '\n'; // Replace "\n" with actual newline
                dst++;
            }
            src += 2; // Skip over the escaped characters
        }
        else
        {
            if (dst - fixed_cert < fixed_cert_size - 1) // Prevent buffer overflow
            {
                *dst = *src;
                dst++;
            }
            src++;
        }
    }

    *dst = '\0'; // Ensure null termination
}

#define CERT_SIZE 2048 // Make sure this size is sufficient for your certificate

void extract_public_key()
{
    if (strlen(certificate_of_device) == 0)
    {
        ESP_LOGI(TAG, "Certificate is empty, cannot extract public key!");
        return;
    }

    // Fix certificate format (if necessary)
    char fixed_cert[CERT_SIZE];
    fix_certificate_format(certificate_of_device, fixed_cert, CERT_SIZE);

    mbedtls_x509_crt cert;
    mbedtls_x509_crt_init(&cert);

    // Parse the certificate from the PEM string
    int ret = mbedtls_x509_crt_parse(&cert, (const unsigned char *)fixed_cert, strlen(fixed_cert) + 1);
    if (ret != 0)
    {
        ESP_LOGI(TAG, "Failed to parse certificate, error: -0x%X", -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    // Extract the public key
    mbedtls_pk_context *pk = &cert.pk;

    // Check if it's an RSA key instead of EC
    if (!mbedtls_pk_can_do(pk, MBEDTLS_PK_ECKEY))
    {
        ESP_LOGE(TAG, "Certificate does not contain an EC public key!");
        mbedtls_x509_crt_free(&cert);
        return;
    }

    // Convert the public key to PEM format
    unsigned char buf[PUBKEY_BUFFER_SIZE]; // Ensure this is large enough
    size_t olen = 0;
    ret = mbedtls_pk_write_pubkey_pem(pk, buf, PUBKEY_BUFFER_SIZE);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to write public key in PEM format, error: -0x%X", -ret);
        mbedtls_x509_crt_free(&cert);
        return;
    }

    // Ensure the public key buffer is large enough and copy the public key to global buffer
    strncpy(public_key, (const char *)buf, PUBKEY_BUFFER_SIZE - 1);
    public_key[PUBKEY_BUFFER_SIZE - 1] = '\0'; // Null terminate the string

    ESP_LOGI(TAG, "Extracted EC Public Key:\n%s", public_key);

    // Clean up
    mbedtls_x509_crt_free(&cert);
}

void display_paisa_info(const char *url)
{
    ESP_LOGI(TAG, "Device URL: %s", url);

    esp_http_client_config_t config = {
        .host = "bit.ly",
        //.path = "/3HnHwEu",
        //.path = "/4glPu0g",
        //.path = "/4hAjeaU",
        //.path = "/430XMb1",
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

        // Extract the certificate from the response
        const char *cert_start = strstr(response_buffer, "certificate_of_device:");
        if (cert_start != NULL)
        {
            cert_start += strlen("certificate_of_device:");

            const char *cert_end = strstr(cert_start, "-----END CERTIFICATE-----");
            if (cert_end != NULL)
            {
                size_t cert_length = cert_end - cert_start + strlen("-----END CERTIFICATE-----");

                // Ensure we do not exceed the buffer size
                if (cert_length < CERT_BUFFER_SIZE - 1)
                {
                    strncpy(certificate_of_device, cert_start, cert_length);
                    certificate_of_device[cert_length] = '\0'; // Null-terminate

                    ESP_LOGI(TAG, "Certificate stored successfully!");
                }
                else
                {
                    ESP_LOGE(TAG, "Certificate too large for buffer!");
                }
            }
            else
            {
                ESP_LOGE(TAG, "Certificate end not found");
            }
        }
        else
        {
            ESP_LOGE(TAG, "Certificate of device not found in response");
        }
    }
    else
    {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// Helper function to extract URL from BLE announcement
static void extract_and_display_url(const uint8_t *data, uint8_t length)
{
    const char *https_marker = "https://";
    const size_t marker_len = strlen(https_marker);
    char url_buffer[100] = {0};

    // Search for the start of HTTPS in the raw data
    for (int i = 0; i < length - marker_len; i++)
    {
        if (memcmp(&data[i], https_marker, marker_len) == 0)
        {
            // Found the start of the URL, copy until the end of the data
            size_t remaining_length = length - i;
            size_t url_length = remaining_length < sizeof(url_buffer) ? remaining_length : sizeof(url_buffer) - 1;
            memcpy(url_buffer, &data[i], url_length);
            url_buffer[url_length] = '\0';

            display_paisa_info(url_buffer);
            break;
        }
    }
}
/*
static int encrypt_with_public_key(const uint8_t *data, size_t data_len,
                                   const char *public_key,
                                   uint8_t *encrypted_data, size_t *encrypted_len)
{
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "encrypt_with_public_key";
    int ret = 0;

    mbedtls_pk_init(&pk);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    do
    {
        ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                    (const unsigned char *)pers, strlen(pers));
        if (ret != 0)
        {
            ESP_LOGE(TAG, "Failed to seed CTR_DRBG: -0x%x", -ret);
            break;
        }

        ESP_LOGI(TAG, "Parsing public key of length %d", strlen(public_key));
        ret = mbedtls_pk_parse_public_key(&pk, (const unsigned char *)public_key, strlen(public_key) + 1);
        if (ret != 0)
        {
            ESP_LOGE(TAG, "Failed to parse public key: -0x%x", -ret);
            break;
        }

        ESP_LOGI(TAG, "Encrypting data of length %d", data_len);
        ret = mbedtls_pk_encrypt(&pk, data, data_len,
                                 encrypted_data, encrypted_len,
                                 *encrypted_len,
                                 mbedtls_ctr_drbg_random, &ctr_drbg);

        if (ret != 0)
        {
            ESP_LOGE(TAG, "Failed to encrypt data: -0x%x", -ret);
            char error_buf[100];
            mbedtls_strerror(ret, error_buf, 100);
            ESP_LOGE(TAG, "Error details: %s", error_buf);
        }
        else
        {
            ESP_LOGI(TAG, "Data encrypted successfully. Encrypted length: %d", *encrypted_len);
        }
    } while (0);

    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);

    return ret;
}*/
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
                                   uint8_t *out_ephemeral_public, // Add this
                                   uint8_t *out_iv)               // Add this
{
    mbedtls_ecdh_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context peer_pk;
    const char *pers = "gen_key";
    int ret = 0;
    uint8_t *decrypted = NULL; // Move declaration to top
    uint8_t shared_secret[32];
    uint8_t iv[12] = {0};
    uint8_t tag[16];

    // Init all contexts
    mbedtls_ecdh_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&peer_pk);

    // Right after your context initializations, add:
    decrypted = (uint8_t *)malloc(data_len);
    if (decrypted == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate decryption buffer");
        ret = -1;
        goto cleanup;
    }

    // Seed RNG
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to seed RNG: -0x%x", -ret);
        goto cleanup;
    }

    // Load curve
    ret = mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to setup ECDH: -0x%x", -ret);
        goto cleanup;
    }

    // Generate our ephemeral keypair
    ret = mbedtls_ecdh_gen_public(&ctx.MBEDTLS_PRIVATE(grp),
                                  &ctx.MBEDTLS_PRIVATE(d),
                                  &ctx.MBEDTLS_PRIVATE(Q),
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to generate keypair: -0x%x", -ret);
        goto cleanup;
    }

    // Parse peer's public key from PEM
    ret = mbedtls_pk_parse_public_key(&peer_pk, (const unsigned char *)public_key_pem,
                                      strlen(public_key_pem) + 1);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to parse public key: -0x%x", -ret);
        goto cleanup;
    }

    // Extract peer's public point
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

    // Use shared secret for AES-GCM encryption
    // uint8_t shared_secret[32];
    size_t secret_len;
    ret = mbedtls_mpi_write_binary(&ctx.MBEDTLS_PRIVATE(z), shared_secret, sizeof(shared_secret));
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Failed to extract shared secret: -0x%x", -ret);
        goto cleanup;
    }

    // Encrypt data using AES-GCM
    mbedtls_gcm_context gcm;
    // uint8_t iv[12] = {0}; // In production, use random IV
    // uint8_t tag[16];

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

    // Copy tag after encrypted data
    memcpy(encrypted_data + data_len, tag, 16);
    *encrypted_len = data_len + 16; // encrypted data + tag

    ESP_LOGI(TAG, "Encryption successful");

    size_t public_key_len;
    ret = mbedtls_ecp_point_write_binary(&ctx.MBEDTLS_PRIVATE(grp),
                                         &ctx.MBEDTLS_PRIVATE(Q),
                                         MBEDTLS_ECP_PF_UNCOMPRESSED,
                                         &public_key_len,
                                         out_ephemeral_public,
                                         65);

    // Diagnostic prints
    ESP_LOGI(TAG, "Ephemeral public key export:");
    ESP_LOGI(TAG, "Return code: -0x%x", -ret);
    ESP_LOGI(TAG, "Exported key length: %zu", public_key_len);

    // Print the first few bytes of the exported key
    ESP_LOGI(TAG, "First 8 bytes of ephemeral public key:");
    for (int i = 0; i < 65; i++)
    {
        printf("%02x ", out_ephemeral_public[i]);
    }
    printf("\n");

    // Copy the IV
    memcpy(out_iv, iv, 12);

    // Print shared secret in hex
    ESP_LOGI(TAG, "Shared secret:");
    for (int i = 0; i < 32; i++)
    {
        printf("%02x", shared_secret[i]);
    }
    printf("\n");

    // Print encrypted data in hex
    ESP_LOGI(TAG, "Encrypted data + tag (%d bytes):", *encrypted_len);
    for (int i = 0; i < *encrypted_len; i++)
    {
        printf("%02x", encrypted_data[i]);
    }
    printf("\n");

    // Separate the tag from encrypted data for decryption
    // uint8_t *decrypted = (uint8_t*)malloc(data_len);
    // if (decrypted == NULL) {
    //    ESP_LOGE(TAG, "Failed to allocate decryption buffer");
    //    ret = -1;
    //     goto cleanup;
    // }

    // Extract tag from the end of encrypted data
    memcpy(tag, encrypted_data + data_len, 16);

    // Decrypt using only the encrypted portion
    ret = mbedtls_gcm_auth_decrypt(&gcm, data_len,
                                   iv, sizeof(iv),
                                   NULL, 0,
                                   tag, 16,
                                   encrypted_data, // Just the encrypted portion
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
        free(decrypted); // Add this line
    mbedtls_ecdh_free(&ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_pk_free(&peer_pk);
    return ret;
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
            ESP_LOGI(TAG, "Found LOC-PAISA advertisement");

            uint8_t total_length = debug_disc->data[3];
            ESP_LOGI(TAG, "Processing advertisement with length: %d", total_length);
            print_adv_data(debug_disc->data, total_length);

            // Log the sender's address
            char addr_str[18];
            snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                     disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                     disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
            ESP_LOGI(TAG, "Sender address: %s", addr_str);

            // TODO
            // CHECK TIMESTAMP FRESHNESS

            // TODO
            // VERIFY MANIFEST IDEV SIGNATURE USIGN PKMSR

            // TODO
            // VERIFY MSGANNO SIGNATURE USING PKIDEV

            // PRINT OUT PAISA INFORMATION
            extract_and_display_url(debug_disc->data, total_length);
            // PRINT out the manifest + the announcement information (neatly)
            if (strlen(certificate_of_device) > 0)
            {
                extract_public_key();
                // Temporarily stop scanning while we send our response
                ble_gap_disc_cancel();

                // Generate STS Key
                sts_key_t sts_key;
                sts_iv_t sts_iv;

                // Generate random values for key and IV using ESP32's hardware RNG
                // esp_fill_random(&sts_key, sizeof(sts_key));
                // esp_fill_random(&sts_iv, sizeof(sts_iv));

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

                // Send Announcement data over to UWB board.
                // send_uart_data(debug_disc->data, total_length);

                // Create and send STS and AES data over UART
                //uint8_t crypto_data[64]; // Increased to hold both STS and AES data
                //memcpy(crypto_data, &sts_key, sizeof(sts_key));
                //memcpy(crypto_data + sizeof(sts_key), &sts_iv, sizeof(sts_iv));
                //memcpy(crypto_data + sizeof(sts_key) + sizeof(sts_iv), &aes_key, sizeof(aes_key));
                //send_uart_data(crypto_data, sizeof(crypto_data));

                uint8_t crypto_data[36]; // 16 (STS key) + 16 (STS IV) + 2 (src_addr) + 2 (dst_addr)
                memcpy(crypto_data, &sts_key, sizeof(sts_key));
                memcpy(crypto_data + sizeof(sts_key), &sts_iv, sizeof(sts_iv));
                memcpy(crypto_data + sizeof(sts_key) + sizeof(sts_iv), &src_addr, sizeof(src_addr));
                memcpy(crypto_data + sizeof(sts_key) + sizeof(sts_iv) + sizeof(src_addr), &dst_addr, sizeof(dst_addr));
                send_uart_data(crypto_data, sizeof(crypto_data));

                /*
                uint8_t encrypted_sts_data[256]; // Adjust size as needed
                size_t encrypted_length = sizeof(encrypted_sts_data);
                // int ret = encrypt_with_ecdh(crypto_data, sizeof(crypto_data),
                //                             public_key,
                //                             encrypted_sts_data, &encrypted_length);
                int ret = encrypt_with_public_key(crypto_data, sizeof(crypto_data),
                                                  public_key,
                                                  encrypted_sts_data, &encrypted_length);
                // send_loc_resp();
                send_ble_message(crypto_data, sizeof(crypto_data));
                */
                uint8_t encrypted_sts_data[256];
                size_t encrypted_length = sizeof(encrypted_sts_data);
                uint8_t complete_message[256 + 65 + 12];
                uint8_t ephemeral_public[65];
                uint8_t iv[12];

                // Updated function call with new parameters
                int ret = encrypt_with_public_key(crypto_data, sizeof(crypto_data),
                                                  public_key,
                                                  encrypted_sts_data, &encrypted_length,
                                                  ephemeral_public, // Pass buffer for public key
                                                  iv);              // Pass buffer for IV

                if (ret == 0)
                {
                    size_t total_length = 0;
                    memcpy(complete_message, ephemeral_public, 65);
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
                ESP_LOGE(TAG, "Certificate not available!");
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
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}

static void ble_sync_cb(void)
{
    int rc;

    // Figure out address to use
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    // Initialize extended advertising
    ext_adv_init();

    // Initialize and start scanning
    ble_scanner_init();

    ESP_LOGI(TAG, "BLE stack synchronized");
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    uart_init();

    // ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_sta();

    // Initialize the NimBLE stack
    nimble_port_init();

    // Initialize our mbuf pool
    init_large_mbuf_pool();

    // Initialize the NimBLE host configuration
    ble_hs_cfg.sync_cb = ble_sync_cb; // Use our new sync callback
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;

    // Set security IOCap - match with your transmitter device
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    // Start the NimBLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE initialization completed");
}