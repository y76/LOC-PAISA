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
#include "sdkconfig.h"

// Configure the maximum advertisement size
#define MAX_ADV_DATA_LEN 255  // Maximum extended advertisement data length
struct __attribute__((packed)) ble_gap_disc_desc_debug {
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
    uint8_t *data;  // This should be a pointer
};
static const char *TAG = "BLE_RECEIVER";
static int ble_gap_event(struct ble_gap_event *event, void *arg);
static uint8_t own_addr_type;

// Function to print advertisement data in a readable format
static void print_adv_data(const uint8_t *data, uint16_t length) {
    ESP_LOGI(TAG, "Advertisement data (length %d):", length);
    
    // Print entire raw data first
    ESP_LOGI(TAG, "Full raw data:");
    for (int i = 0; i < length; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    // Now we know it's structured as:
    // [02 01 06] [XX FF Company_ID Data...]
    if (length >= 3 && data[0] == 0x02 && data[1] == 0x01) {
        ESP_LOGI(TAG, "Flags field: %02X", data[2]);
    }

    // Find manufacturer data (should start at index 3)
    if (length > 4 && data[4] == 0xFF) {
        uint16_t company_id = data[5] | (data[6] << 8);
        ESP_LOGI(TAG, "Manufacturer Data:");
        ESP_LOGI(TAG, "  Company ID: 0x%04X", company_id);
        ESP_LOGI(TAG, "  Data (%d bytes):", length - 7);
        
        // Print manufacturer data in hex
        for (int i = 7; i < length; i++) {
            printf("%02X ", data[i]);
            if ((i - 6) % 16 == 0) printf("\n");
        }
        printf("\n");

        // Try ASCII interpretation
        ESP_LOGI(TAG, "ASCII interpretation:");
        for (int i = 7; i < length; i++) {
            if (isprint(data[i])) {
                printf("%c", data[i]);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
}

static void ble_scanner_init(void) {
    int rc;
    
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error determining address type; rc=%d", rc);
        return;
    }

    struct ble_gap_disc_params scan_params = {
        .itvl = BLE_GAP_SCAN_ITVL_MS(100),
        .window = BLE_GAP_SCAN_WIN_MS(50),
        .filter_duplicates = 0,           // Don't filter duplicates
        .limited = 0,                     // Don't limit discovery
        .passive = 0,                     // Use active scanning
        .filter_policy = 0                // No filtering
    };

    // Start regular scanning instead of extended
    rc = ble_gap_disc(own_addr_type, 0, // Duration (0 = scan continuously)
                      &scan_params,
                      ble_gap_event, NULL);
    
    if (rc != 0) {
        ESP_LOGE(TAG, "Error initiating scan; rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "Scanner started successfully");
}

// Helper function to check for LOC-PAISA in advertisement data
static bool contains_loc_paisa(const uint8_t *data) {
    const char* loc_paisa = "LOC-PAISA";
    const size_t marker_len = strlen(loc_paisa);
    uint8_t total_length = data[3];
    
    // Need at least 7 bytes for header plus enough space for the marker
    if (total_length < 7 + marker_len) {
        return false;
    }
    
    // Check if the data after manufacturer specific data contains our marker
    // Start checking from position 7 (after flags and manufacturer data header)
    for (size_t i = 7; i <= total_length - marker_len; i++) {
        if (memcmp(&data[i], loc_paisa, marker_len) == 0) {
            return true;
        }
    }
    return false;
}

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_EXT_DISC: {
            const struct ble_gap_disc_desc *disc = &event->disc;
            struct ble_gap_disc_desc_debug *debug_disc = (struct ble_gap_disc_desc_debug *)disc;
            
            if (debug_disc->data != NULL) {
                // Check for manufacturer specific data type (0xFF) and our marker
                if (debug_disc->data[4] == 0xFF && contains_loc_paisa(debug_disc->data)) {
                    ESP_LOGI(TAG, "Found LOC-PAISA advertisement");
                    
                    // First byte of manufacturer data contains the total length
                    uint8_t total_length = debug_disc->data[3];
                    ESP_LOGI(TAG, "Processing advertisement with length: %d", total_length);
                    print_adv_data(debug_disc->data, total_length);
                    
                    // Log the sender's address
                    char addr_str[18];
                    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                            disc->addr.val[5], disc->addr.val[4], disc->addr.val[3],
                            disc->addr.val[2], disc->addr.val[1], disc->addr.val[0]);
                    ESP_LOGI(TAG, "Sender address: %s", addr_str);
                }
            }
            break;
        }
        
        case BLE_GAP_EVENT_DISC_COMPLETE:
            ESP_LOGI(TAG, "Discovery complete event (type 8) - restarting scan");
            // Restart scanning
            ble_scanner_init();
            break;
            
        default:
            ESP_LOGI(TAG, "Other unhandled BLE GAP event: %d", event->type);
            break;
    }
    return 0;
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run(); // This function will return only when nimble_port_stop() is executed
    nimble_port_freertos_deinit();
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize the NimBLE stack
    nimble_port_init();

    // Initialize the NimBLE host configuration
    ble_hs_cfg.sync_cb = ble_scanner_init;
    ble_hs_cfg.reset_cb = NULL;
    ble_hs_cfg.store_status_cb = NULL;
    
    // Set security IOCap - match with your transmitter device
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 0;

    // Start the NimBLE host task
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE Scanner initialized and running");
}