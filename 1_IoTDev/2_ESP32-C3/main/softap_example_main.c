#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define ECHO_TEST_TXD (CONFIG_EXAMPLE_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_EXAMPLE_UART_RXD)
#define ECHO_TEST_RTS (-1)
#define ECHO_TEST_CTS (-1)

#define ECHO_UART_PORT_NUM (CONFIG_EXAMPLE_UART_PORT_NUM)
#define ECHO_UART_BAUD_RATE (CONFIG_EXAMPLE_UART_BAUD_RATE)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

#define BUF_SIZE (1024)

static const char *TAG = "uart_echo";

static void uart_task(void *arg)
{
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

    while (1) {
        // Read data from UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            data[len] = '\0';  // Null terminate to safely print as string
            ESP_LOGI(TAG, "Received %d bytes: %s", len, data);
            // Also print hex values for non-printable characters
            ESP_LOGI(TAG, "Hex values:");
            for(int i = 0; i < len; i++) {
                printf("%02X ", data[i]);
                if((i + 1) % 16 == 0) printf("\n");
            }
            printf("\n");
            
            uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "UART echo test starting...");
    xTaskCreate(uart_task, "uart_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);
}