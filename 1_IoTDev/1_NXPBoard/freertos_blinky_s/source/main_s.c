/*
 * FreeRTOS Pre-Release V1.0.0
 * Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/* Board specific includes. */

/* Trustzone config. */
#include "tzm_config.h"

/* FreeRTOS includes. */
#include "secure_port_macros.h"

#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "fsl_power.h"
#include "fsl_rtc.h"
#include "fsl_usart.h"
#include "fsl_debug_console.h"
#include "fsl_ctimer.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/sha256.h"
#include "mbedtls/pk.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

#include <stdbool.h>

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#include "hacl-c/Hacl_Ed25519.h"
#else
#include MBEDTLS_CONFIG_FILE
#include "ksdk_mbedtls.h"
#endif
#include <stdio.h>
#include <time.h>
#include "fsl_iap.h"
#include "mbedtls/gcm.h"
#include "mbedtls/cipher.h"
#include "mbedtls/platform.h"
#include "mbedtls/error.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
/**
 * @brief Start address of non-secure application.
 */
#define mainNONSECURE_APP_START_ADDRESS DEMO_CODE_START_NS

/**
 * @brief LED port and pins.
 */
#define LED_PORT      BOARD_LED_BLUE_GPIO_PORT
#define GREEN_LED_PIN BOARD_LED_GREEN_GPIO_PIN
#define BLUE_LED_PIN  BOARD_LED_BLUE_GPIO_PIN

/**
 * @brief CTimer configuration.
 */
#define CTIMER          	CTIMER2         /* Timer 2 */
#define CTIMER_MAT_OUT 		kCTIMER_Match_1 /* Match output */
#define CTIMER_CLK_FREQ 	CLOCK_GetCTimerClkFreq(2U)

/**
 * @brief typedef for non-secure Reset Handler.
 */
#if defined(__IAR_SYSTEMS_ICC__)
typedef __cmse_nonsecure_call void (*NonSecureResetHandler_t)(void);
#else
typedef void (*NonSecureResetHandler_t)(void) __attribute__((cmse_nonsecure_call));
#endif

#define WIFI_USART          	USART4
#define WIFI_USART_CLK_SRC  	kCLOCK_Flexcomm4
#define WIFI_USART_IRQn         FLEXCOMM4_IRQn
#define WIFI_USART_IRQHandler   FLEXCOMM4_IRQHandler
#define WIFI_USART_CLK_FREQ 	CLOCK_GetFlexCommClkFreq(0U)

#define WIFI_USART2         USART2
#define WIFI_USART2_CLK_SRC kCLOCK_Flexcomm2
#define WIFI_USART2_IRQn    FLEXCOMM2_IRQn
#define WIFI_USART2_IRQHandler   FLEXCOMM2_IRQHandler
#define WIFI_USART2_CLK_FREQ CLOCK_GetFlexCommClkFreq(2U)

#define PACKET_SEND_TIMER	5
#define ATTESTATION_TIMER	5

#define BUF_SIZE			(256)
#define SIG_SIZE			(256)
#define NONCE_SIZE			(32)
#define TIME_SIZE			(4)
#define ID_SIZE				(4)
#define HASH_SIZE			(32)
#define ATT_SIZE			(1)
#define TIME_PREV			(1681506039)
#define ID_DEV				(19682938)
#define PRV_DEV_KEY_PEM			"-----BEGIN EC PRIVATE KEY-----\r\nMHcCAQEEILU1UM1iS7FVYItkMFIjXzAoZ15ggKPolhQpkm8Fpvb+oAoGCCqGSM49\r\nAwEHoUQDQgAE25n7ySdcmAANQrUIgnTl0cMdUy4fkWOW/coAj0af2h56PveM7sqe\r\nMGraQzrlNghIOU2Q3We5HyZhhEypxUJIKg==\r\n-----END EC PRIVATE KEY-----"
#define PUB_M_SRV_KEY_PEM 		"-----BEGIN PUBLIC KEY-----\r\nMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEUoT5Ct1reTJanqFLf6NAm5Kt6G8x\r\n4ngbrnjzTH1fDkQTr0eTDCJeoBPBxSP3kT3IJ8+7UZ/vBGKfYr/MuP6vYg==\r\n-----END PUBLIC KEY-----"
#define M_SRV_URL				"https://bit.ly/3EJadxK"//"https://bit.ly/430XMb1"//"https://bit.ly/4hAjeaU"//"https://bit.ly/4glPu0g"//"https://bit.ly/3HnHwEu"
#define MSG_END_CHAR		"MSGEND"
#define ACK_END_CHAR		"ACKEND"

#define CONVERT_MS_TO_S		(1000)

/*-----------------------------------------------------------*/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/**
 * @brief Application-specific implementation of the SystemInitHook().
 */
void SystemInitHook(void);

/**
 * @brief Boots into the non-secure code.
 *
 * @param[in] ulNonSecureStartAddress Start address of the non-secure application.
 */
void BootNonSecure(uint32_t ulNonSecureStartAddress);

/**
 * @brief Broadcast the message with signature.
 */
void announcement();
/*-----------------------------------------------------------*/


void ctimer_match_callback(uint32_t flags);

/* Array of function pointers for callback for each channel */
ctimer_callback_t ctimer_callback_table[] = {
    NULL, ctimer_match_callback, NULL, NULL, NULL, NULL, NULL, NULL};

/*******************************************************************************
 * Global Variables
 ******************************************************************************/

mbedtls_pk_context private_key = {0, };
mbedtls_ctr_drbg_context ctr_drbg = {0, };
static uint8_t last_sent_nonce[NONCE_SIZE] = {0};
static uint8_t previous_sent_nonce[NONCE_SIZE] = {0};

/*-----------------------------------------------------------*/


/*******************************************************************************
 * Code
 ******************************************************************************/

void SystemInitHook(void)
{
    /* The TrustZone should be configured as early as possible after RESET.
     * Therefore it is called from SystemInit() during startup. The
     * SystemInitHook() weak function overloading is used for this purpose.
     */
    BOARD_InitTrustZone();
}
/*-----------------------------------------------------------*/

void BootNonSecure(uint32_t ulNonSecureStartAddress)
{
    NonSecureResetHandler_t pxNonSecureResetHandler;

    /* Main Stack Pointer value for the non-secure side is the first entry in
     * the non-secure vector table. Read the first entry and assign the same to
     * the non-secure main stack pointer(MSP_NS). */
    secureportSET_MSP_NS(*((uint32_t *)(ulNonSecureStartAddress)));

    /* Reset handler for the non-secure side is the second entry in the
     * non-secure vector table. */
    pxNonSecureResetHandler = (NonSecureResetHandler_t)(*((uint32_t *)((ulNonSecureStartAddress) + 4U)));

    /* Start non-secure state software application by jumping to the non-secure
     * reset handler. */
    pxNonSecureResetHandler();
}


/*-----------------------------------------------------------*/
static volatile uint32_t s_MsCount = 0U;
static volatile uint32_t adjustedSyncTime = 0U;
/*!
 * @brief Milliseconds counter since last POR/reset.
 */
void SysTick_Handler(void)
{
    s_MsCount++;
}

void ctimer_match_callback(uint32_t flags)
{
    DWT->CYCCNT = 0;
    announcement();
	uint32_t cycles = DWT->CYCCNT;

	// times will be adjusted as much as the time it takes to finish the broadcast
	s_MsCount += (double)cycles/(CLOCK_GetCoreSysClkFreq()/CONVERT_MS_TO_S);

	// To prevent overflow of 's_MsCount'
	adjustedSyncTime += (s_MsCount/CONVERT_MS_TO_S);
	s_MsCount -= (s_MsCount/CONVERT_MS_TO_S)*CONVERT_MS_TO_S;
}

int getEntropyItfFunction(void* userData,uint8_t* buffer,size_t bytes)
{
	int i;
	for(i = 0; i < bytes ; i++)
	{
		buffer[i] = i;
	}

	return 0;
}

void __sha256(const char *msg, size_t msg_len, char *digest)
{
	mbedtls_sha256_context sha256 = {0, };

	mbedtls_sha256_init(&sha256);
	mbedtls_sha256_starts(&sha256, 0);
	mbedtls_sha256_update(&sha256, msg, msg_len);
	mbedtls_sha256_finish(&sha256, digest);
}

#define MAX_BUFFER_SIZE 256  

#define RX_BUFFER_SIZE 256
#define START_MARKER "PAISASTART:"
#define END_MARKER ":PAISAEND"
#define START_MARKER_LENGTH (sizeof("PAISASTART:") - 1)
#define END_MARKER_LENGTH (sizeof(":PAISAEND") - 1)

static uint8_t rxBuffer[MAX_BUFFER_SIZE];
static uint16_t bufferIndex = 0;

void findMessage(const uint8_t* buffer, uint16_t length) {
    if (!buffer || length == 0) {
        return; 
    }

    uint16_t max_search = (length < 1000) ? length : 1000;

    for (uint16_t i = 0; i < max_search - START_MARKER_LENGTH + 1; i++) {
        if (memcmp(buffer + i, START_MARKER, START_MARKER_LENGTH) == 0) {
            uint16_t msgStart = i + START_MARKER_LENGTH;

            for (uint16_t j = msgStart; j < max_search - END_MARKER_LENGTH + 1; j++) {
                if (memcmp(buffer + j, END_MARKER, END_MARKER_LENGTH) == 0) {
                    for (uint16_t k = msgStart; k < j; k++) {
                        if (isprint(buffer[k])) {
                            PRINTF("%c", buffer[k]);
                        }
                    }
                    return;
                }
            }
        }
    }
}
static int decrypt_with_private_key(const uint8_t *encrypted_data, size_t encrypted_len,
                                  const char *private_key_pem,
                                  const uint8_t *peer_public_key, size_t peer_public_key_len,
                                  const uint8_t *iv,
                                  uint8_t *decrypted_data, size_t *decrypted_len) {
    mbedtls_ecdh_context ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context our_pk;
    const char *pers = "decrypt_key";
    int ret = 0;
    uint8_t shared_secret[32];
    uint8_t tag[16];

    mbedtls_ecdh_init(&ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(&our_pk);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        PRINTF("Failed to seed RNG: -0x%x\n", -ret);
        goto cleanup;
    }

    ret = mbedtls_pk_parse_key(&our_pk, (const unsigned char *)private_key_pem,
                              strlen(private_key_pem) + 1, NULL, 0);
    if (ret != 0) {
        PRINTF("Failed to parse private key: -0x%x\n", -ret);
        goto cleanup;
    }

    ret = mbedtls_ecdh_setup(&ctx, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        PRINTF("Failed to setup ECDH: -0x%x\n", -ret);
        goto cleanup;
    }

    const mbedtls_ecp_keypair *our_keypair = mbedtls_pk_ec(our_pk);
    ret = mbedtls_ecdh_get_params(&ctx, our_keypair, MBEDTLS_ECDH_OURS);
    if (ret != 0) {
        PRINTF("Failed to load our params: -0x%x\n", -ret);
        goto cleanup;
    }

    ret = mbedtls_ecp_point_read_binary(&ctx.grp, &ctx.Qp,
                                       peer_public_key, peer_public_key_len);
    if (ret != 0) {
        PRINTF("Failed to load peer public key point: -0x%x\n", -ret);
        goto cleanup;
    }

    ret = mbedtls_ecdh_calc_secret(&ctx, &ret, shared_secret, sizeof(shared_secret),
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        PRINTF("Failed to compute shared secret: -0x%x\n", -ret);
        goto cleanup;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, shared_secret, 256);
    if (ret != 0) {
        PRINTF("Failed to set GCM key: -0x%x\n", -ret);
        goto cleanup_gcm;
    }

    *decrypted_len = encrypted_len - 16;
    memcpy(tag, encrypted_data + *decrypted_len, 16);

    ret = mbedtls_gcm_auth_decrypt(&gcm, *decrypted_len,
                                  iv, 12,
                                  NULL, 0,
                                  tag, 16,
                                  encrypted_data, 
                                  decrypted_data + START_MARKER_LENGTH);

    if (ret != 0) {
        PRINTF("Decryption failed: -0x%x\n", -ret);
        goto cleanup_gcm;
    }

    // Add markers
    memcpy(decrypted_data, START_MARKER, START_MARKER_LENGTH);
    memcpy(decrypted_data + START_MARKER_LENGTH + *decrypted_len, END_MARKER, END_MARKER_LENGTH);

    *decrypted_len = *decrypted_len + START_MARKER_LENGTH + END_MARKER_LENGTH;

    PRINTF("Decryption successful\n");

    PRINTF("Final data with markers (hex):\n");
    for (int i = 0; i < *decrypted_len; i++) {
        PRINTF("%02x", decrypted_data[i]);
    }
    PRINTF("\n");

    PRINTF("Final data with markers (ASCII):\n");
    for (int i = 0; i < *decrypted_len; i++) {
        PRINTF("%c", decrypted_data[i]);
    }
    PRINTF("\n");

    PRINTF("Shared secret:\n");
    for (int i = 0; i < 32; i++) {
        PRINTF("%02x", shared_secret[i]);
    }
    PRINTF("\n");
cleanup_gcm:
    mbedtls_gcm_free(&gcm);
cleanup:
    mbedtls_ecdh_free(&ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_pk_free(&our_pk);
    return ret;
}
void WIFI_USART_IRQHandler(void)
{
   uint16_t safety_counter = 0;
   const uint16_t MAX_ITERATIONS = 5; 

   while ((kUSART_RxFifoNotEmptyFlag | kUSART_RxError) & USART_GetStatusFlags(WIFI_USART) &&
          safety_counter++ < MAX_ITERATIONS)
   {
       uint8_t receivedByte = USART_ReadByte(WIFI_USART);

       if (bufferIndex < MAX_BUFFER_SIZE)
       {
           rxBuffer[bufferIndex++] = receivedByte;

           if (bufferIndex >= END_MARKER_LENGTH &&
               memcmp(&rxBuffer[bufferIndex - END_MARKER_LENGTH],
                     END_MARKER, END_MARKER_LENGTH) == 0)
           {
               findMessage(rxBuffer, bufferIndex);

               uint16_t dataStart = 0;
               uint16_t dataEnd = bufferIndex - END_MARKER_LENGTH;

               for (uint16_t i = 0; i < bufferIndex - START_MARKER_LENGTH; i++) {
                   if (memcmp(&rxBuffer[i], START_MARKER, START_MARKER_LENGTH) == 0) {
                       dataStart = i + START_MARKER_LENGTH + strlen("LOC-RESP");
                       break;
                   }
               }

               PRINTF("Raw message starting at offset %d:\n", dataStart);
               for(int i = 0; i < 32; i++) {
                   PRINTF("%02x ", rxBuffer[dataStart + i]);
               }
               PRINTF("\n");

               if (dataStart == 0 || dataEnd - dataStart < (32 + 65 + 12 + 16)) {
                   PRINTF("Invalid message format - not enough data\r\n");
                   bufferIndex = 0;
                   continue;
               }

               size_t total_message_length = dataEnd - dataStart;

               PRINTF("Raw message data (%lu bytes):\n", (unsigned long)total_message_length);

               PRINTF("Raw message data (%zd bytes):\n", total_message_length);

               PRINTF("Raw message data (%d bytes):\n", (int)total_message_length);
               for (size_t i = 0; i < total_message_length; i++) {
                   PRINTF("%02x ", rxBuffer[dataStart + i]);
               }
               PRINTF("\n");

               if (total_message_length < (65 + 12 + 16)) {
                   PRINTF("Message too short for encryption components\r\n");
                   bufferIndex = 0;
                   continue;
               }

               uint8_t *message_start = rxBuffer + dataStart;
               uint8_t *nonce = message_start;  // First 32 bytes - just for printing
               uint8_t *ephemeral_public = message_start + 32;  // Next 65 bytes - used for decryption
               uint8_t *iv = message_start + 32 + 65;  // Next 12 bytes - used for decryption
               uint8_t *encrypted_data = message_start + 32 + 65 + 12;  // Rest is encrypted data
               size_t encrypted_length = total_message_length - (32 + 65 + 12);

               if (memcmp(nonce, last_sent_nonce, NONCE_SIZE) != 0 &&
                   memcmp(nonce, previous_sent_nonce, NONCE_SIZE) != 0) {
                   PRINTF("Nonce mismatch - breaking\n");
                   PRINTF("Received nonce: ");
                   for (int i = 0; i < 32; i++) {
                       PRINTF("%02x ", nonce[i]);
                   }
                   PRINTF("\nExpected recent nonce: ");
                   for (int i = 0; i < 32; i++) {
                       PRINTF("%02x ", last_sent_nonce[i]);
                   }
                   PRINTF("\nExpected previous nonce: ");
                   for (int i = 0; i < 32; i++) {
                       PRINTF("%02x ", previous_sent_nonce[i]);
                   }
                   PRINTF("\n");
                   bufferIndex = 0;
                   return;
               }

               PRINTF("Nonce match - continuing with decryption\n");

               PRINTF("Ephemeral public key length: 65\n");
               PRINTF("Detailed ephemeral public key:\n");
               for (int i = 0; i < 65; i++) {
                   PRINTF("%02x ", ephemeral_public[i]);
               }
               PRINTF("\n");

               PRINTF("IV length: 12\n");
               PRINTF("IV data:\n");
               for (int i = 0; i < 12; i++) {
                   PRINTF("%02x ", iv[i]);
               }
               PRINTF("\n");

               PRINTF("Encrypted data length: %zu\n", encrypted_length);
               PRINTF("First 16 bytes of encrypted data:\n");
               for (int i = 0; i < 16 && i < encrypted_length; i++) {
                   PRINTF("%02x ", encrypted_data[i]);
               }
               PRINTF("\n");

               uint8_t decrypted_data[MAX_BUFFER_SIZE];
               size_t decrypted_length = 0;

               int ret = decrypt_with_private_key(encrypted_data, encrypted_length,
                                                  PRV_DEV_KEY_PEM,
                                                  ephemeral_public, 65,
                                                  iv,
                                                  decrypted_data, &decrypted_length);

               PRINTF("Decryption return code: %d (0x%x)\n", ret, -ret);

               if (ret == 0) {
                   USART_WriteBlocking(WIFI_USART2, decrypted_data, decrypted_length);
               }
            else {
                   PRINTF("Decryption failed with error: %d\r\n");
               }

               bufferIndex = 0;
           }
       }
       else
       {
           bufferIndex = 0;
       }
   }

   if (safety_counter >= MAX_ITERATIONS) {
       bufferIndex = 0;
       PRINTF("USART Interrupt: Maximum iterations reached. Potential communication issue.\r\n");
   }

   USART_ClearStatusFlags(WIFI_USART, kUSART_RxError);
   SDK_ISR_EXIT_BARRIER;
}

void WIFI_USART2_IRQHandler(void)
{
    while ((kUSART_RxFifoNotEmptyFlag | kUSART_RxError) & USART_GetStatusFlags(WIFI_USART2))
    {
        uint8_t receivedByte = USART_ReadByte(WIFI_USART2);
        PRINTF("USART2 Rex`ceived: 0x%02X\n", receivedByte);

    }

    USART_ClearStatusFlags(WIFI_USART2, kUSART_RxError);
    SDK_ISR_EXIT_BARRIER;
}

void syncReq(uint8_t *req_buffer)
{
	uint32_t time_prev_int = TIME_PREV + 1;
	const uint32_t id_dev = ID_DEV;

	uint8_t n1_dev[NONCE_SIZE]= {0, };
	uint8_t digest[HASH_SIZE] = {0, };
	uint8_t signature[SIG_SIZE] = {0, };

	size_t msg_len = 0;
	int ret = 0;


	uint8_t time_prev[TIME_SIZE] = {0, };
	memcpy(time_prev, &time_prev_int, TIME_SIZE);

	ret = mbedtls_ctr_drbg_random(&ctr_drbg, n1_dev, sizeof(n1_dev));
	memcpy(req_buffer+msg_len, &id_dev, ID_SIZE);
	msg_len += ID_SIZE;
	memcpy(req_buffer+msg_len, n1_dev, NONCE_SIZE);
	msg_len += NONCE_SIZE;
	memcpy(req_buffer+msg_len, time_prev, TIME_SIZE);
	msg_len += TIME_SIZE;

	__sha256(req_buffer, msg_len, digest);

	size_t sig_len = 0;

	ret = mbedtls_pk_sign (&private_key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature,
			&sig_len, mbedtls_ctr_drbg_random, &ctr_drbg);
	if(ret != 0){while(1);}
	memcpy(req_buffer+msg_len, signature, sig_len);
	msg_len += sig_len;

	memcpy(req_buffer+msg_len, MSG_END_CHAR, strlen(MSG_END_CHAR));
	msg_len += strlen(MSG_END_CHAR);
	USART_WriteBlocking(WIFI_USART, req_buffer, msg_len);
}

void cmp_ts_and_save(const uint8_t *time_prev, const uint8_t *time_cur)
{
	uint32_t time_prev_int = time_prev[0] + (time_prev[1] << 8) + (time_prev[2] << 16) + (time_prev[3] << 24);
	uint32_t time_prev_cur = time_cur[0] + (time_cur[1] << 8) + (time_cur[2] << 16) + (time_cur[3] << 24);

	if (time_prev_int > time_prev_cur) {while(1);}

	s_MsCount = 0;
	adjustedSyncTime = time_prev_cur;
}

void syncResp(uint8_t *req_buffer, uint8_t *resp_buffer)
{
	uint32_t msg_len = 0;
	uint32_t sig_len = 0;
	mbedtls_pk_context public_key = {0, };
	uint8_t digest[HASH_SIZE] = {0, };
	int ret = 0;

	USART_ReadBlocking(WIFI_USART, (uint8_t*)&msg_len, sizeof(msg_len));

	USART_ReadBlocking(WIFI_USART, resp_buffer, msg_len);

	ret = memcmp(req_buffer, resp_buffer, ID_SIZE+NONCE_SIZE);
	if(ret != 0){while(1);}

	// Compare current time from m_srv with time_prev and save it to the flash memory
	cmp_ts_and_save(req_buffer+ID_SIZE+NONCE_SIZE, resp_buffer+ID_SIZE+NONCE_SIZE*2);

	__sha256(resp_buffer, ID_SIZE + NONCE_SIZE*2 + TIME_SIZE, digest);

	sig_len = msg_len - (ID_SIZE + NONCE_SIZE*2 + TIME_SIZE);
	mbedtls_pk_init(&public_key);

	ret = mbedtls_pk_parse_public_key(&public_key, PUB_M_SRV_KEY_PEM, strlen(PUB_M_SRV_KEY_PEM)+1);
	if(ret != 0){while(1);}

	ret = mbedtls_pk_verify(&public_key, MBEDTLS_MD_SHA256, digest, sizeof(digest), resp_buffer+msg_len-sig_len, sig_len);
	if(ret != 0){while(1);}
}

void syncAck(const uint8_t *resp_buffer)
{
	const uint32_t id_dev = ID_DEV;
	size_t msg_len = 0;
	uint8_t digest[HASH_SIZE] = {0, };
	uint8_t signature[SIG_SIZE] = {0, };
	uint8_t n2_dev[NONCE_SIZE] = {0, };
	uint8_t ack_buffer[BUF_SIZE] = {0, };
	int ret = 0;

	ret = mbedtls_ctr_drbg_random(&ctr_drbg, n2_dev, sizeof(n2_dev));

	memcpy(ack_buffer+msg_len, &id_dev, ID_SIZE);
	msg_len += ID_SIZE;
	memcpy(ack_buffer+msg_len, n2_dev, NONCE_SIZE);
	msg_len += NONCE_SIZE;
	memcpy(ack_buffer+msg_len, resp_buffer + ID_SIZE + NONCE_SIZE, NONCE_SIZE);
	msg_len += NONCE_SIZE;
	memcpy(ack_buffer+msg_len, resp_buffer + ID_SIZE + NONCE_SIZE*2, TIME_SIZE);
	msg_len += TIME_SIZE;

	__sha256(ack_buffer, msg_len, digest);

	size_t sig_len = 0;
	ret = mbedtls_pk_sign (&private_key, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature,
			&sig_len, mbedtls_ctr_drbg_random, &ctr_drbg);
	if(ret != 0){while(1);}

	memcpy(ack_buffer+msg_len, signature, sig_len);
	msg_len += sig_len;

	memcpy(ack_buffer+msg_len, ACK_END_CHAR, strlen(ACK_END_CHAR));
	msg_len += strlen(ACK_END_CHAR);

	PRINTF("Sending message (length %d): ", msg_len);
	for(int i = 0; i < msg_len; i++) {
	    PRINTF("%02X ", ack_buffer[i]);  // Print each byte in hexadecimal
	}
	PRINTF("\r\n");

	USART_WriteBlocking(WIFI_USART, ack_buffer, msg_len);


}

void delay(uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++)
    {
        __NOP();
    }
}

uint8_t* expand_msg(uint8_t* newMsg, uint8_t* msg, size_t msg_len){

    for (int i=0; i<msg_len; i++)
    {
        sprintf(&newMsg[i*2], "%X%X", msg[i]/16, msg[i]%16);
     }
     return newMsg;
}

/* Memory location to attest. */
#define MEM_SIZE 		0x10000				/* Length of FLASH 1 with NS-User privilege */
static unsigned long saddr = 0x40000;	/* Start of FLASH 1 with NS-User privilege */

// Due to the limitation of mbedTLS SHA256 library implemented on NXP,
// it couldn't hash data larger than 64KB in Secure World even with 192MB secure RAM,
// so it's inevitable to divide data into 4KB chucks.
#define MEM_SIZE_4K		0x1000



uint8_t attest()
{
	uint8_t expected_digest[HASH_SIZE] = { 0x4B, 0x95, 0x99, 0x39, 0xC0, 0xD7, 0xF5, 0x0A,
			0x34, 0xF2, 0xA5, 0xDB, 0x50, 0x66, 0x24, 0x22,
			0x75, 0x74, 0x60, 0x5C, 0x09, 0xB8, 0xE1, 0x3E,
			0x37, 0x5D, 0xBE, 0x73, 0xBF, 0xCB, 0xE1, 0xFD,
	};
	uint8_t digest[HASH_SIZE] = {0, };
	uint8_t digest_hash_chain[HASH_SIZE*MEM_SIZE/MEM_SIZE_4K];

	for (int i=0; i<MEM_SIZE/MEM_SIZE_4K; i++) {
		__sha256((uint8_t *)saddr+i*MEM_SIZE_4K, MEM_SIZE_4K, digest_hash_chain+i*HASH_SIZE);
	}
	__sha256(digest_hash_chain, sizeof(digest_hash_chain), digest);

	return 0;
}

void announcement()
{
	uint8_t msg[BUF_SIZE] = {0, };
	size_t msg_len = 0;
	uint8_t digest[HASH_SIZE] = {0, };
	uint8_t signature[SIG_SIZE] = {0, };
	uint8_t n_dev[NONCE_SIZE] = {0, };
	mbedtls_sha256_context sha256 = {0, };
	const uint32_t id_dev = 19682938;
	int ret = 0;
	uint8_t m_srv_url_len = strlen(M_SRV_URL);

	uint32_t curTs = adjustedSyncTime + s_MsCount/CONVERT_MS_TO_S;
	static uint8_t attest_result[ATT_SIZE] = {0};
	static uint32_t time_attest = 0;
	static uint8_t attest_count = 0;
	uint32_t cycles_before, cycles_after = 0;

	if ( (++attest_count % ATTESTATION_TIMER == 0) || !time_attest ) {
		attest_count = 0;
		time_attest = curTs;

#ifdef PERFORMANCE_EVALUATION
		cycles_before = DWT->CYCCNT;
#endif
		attest_result[0] = attest();


#ifdef PERFORMANCE_EVALUATION
		cycles_after = DWT->CYCCNT;

		PRINTF("[Attestation] Cycle consumed: %u cycles\n\r", cycles_after - cycles_before);
#endif
	}

	ret = mbedtls_ctr_drbg_random(&ctr_drbg, n_dev, NONCE_SIZE);
	memcpy(previous_sent_nonce, last_sent_nonce, NONCE_SIZE);

	memcpy(last_sent_nonce, n_dev, NONCE_SIZE);
	memcpy(msg+msg_len, n_dev, NONCE_SIZE);
	msg_len += NONCE_SIZE;
	memcpy(msg+msg_len, &curTs, TIME_SIZE);
	msg_len += TIME_SIZE;

	size_t sig_body_len = msg_len;

	memcpy(msg+sig_body_len, &id_dev, sizeof(id_dev));
	sig_body_len += sizeof(id_dev);

	__sha256(M_SRV_URL, strlen(M_SRV_URL), msg+sig_body_len);
	sig_body_len += HASH_SIZE;

	memcpy(msg+sig_body_len, attest_result, ATT_SIZE);
	sig_body_len += ATT_SIZE;

	memcpy(msg+sig_body_len, &time_attest, TIME_SIZE);
	sig_body_len += TIME_SIZE;

	__sha256(msg, sig_body_len, digest);

	size_t sig_len = 0;
#ifdef PERFORMANCE_EVALUATION
	cycles_before = DWT->CYCCNT;
#endif
	ret = mbedtls_pk_sign (&private_key, MBEDTLS_MD_SHA256, digest, HASH_SIZE, signature,
			&sig_len, mbedtls_ctr_drbg_random, &ctr_drbg);
#ifdef PERFORMANCE_EVALUATION
	cycles_after = DWT->CYCCNT;
	PRINTF("[Signing] Cycle consumed: %u cycles\n\r", cycles_after - cycles_before);
#endif
	if(ret != 0){while(1);}

	memset(msg+msg_len, 0, BUF_SIZE-msg_len);
	memcpy(msg+msg_len, signature, sig_len);
	msg_len += sig_len;

	memcpy(msg+msg_len, M_SRV_URL, m_srv_url_len);
	msg_len += m_srv_url_len;

	memcpy(msg+msg_len, &m_srv_url_len, sizeof(m_srv_url_len));
	msg_len += sizeof(m_srv_url_len);

	memcpy(msg+msg_len, attest_result, ATT_SIZE);
	msg_len += ATT_SIZE;

	memcpy(msg+msg_len, &time_attest, TIME_SIZE);
	msg_len += TIME_SIZE;

	PRINTF("\r\n=== Message Components Breakdown ===\r\n");

		PRINTF("n_dev (32 bytes): ");
		for(int i = 0; i < NONCE_SIZE; i++) {
		    PRINTF("%02X ", msg[i]);
		}
		PRINTF("\r\n");

		PRINTF("curTS (4 bytes): ");
		for(int i = NONCE_SIZE; i < NONCE_SIZE + TIME_SIZE; i++) {
		    PRINTF("%02X ", msg[i]);
		}
		PRINTF(" (Decimal: %u)\r\n", curTs);

		PRINTF("signature (variable length): ");
		for(int i = NONCE_SIZE + TIME_SIZE; i < msg_len - m_srv_url_len - sizeof(m_srv_url_len) - ATT_SIZE - TIME_SIZE; i++) {
		    PRINTF("%02X ", msg[i]);
		}
		PRINTF("\r\n");

		PRINTF("M_SRV_URL (%d bytes): %s\r\n", m_srv_url_len, M_SRV_URL);

		PRINTF("m_srv_url_len (1 byte): %02X (Decimal: %u)\r\n", m_srv_url_len, m_srv_url_len);

		PRINTF("attest_result (1 byte): %02X\r\n", attest_result[0]);

		PRINTF("time_attest (4 bytes): ");
		uint32_t time_att_offset = msg_len - TIME_SIZE;
		for(int i = time_att_offset; i < msg_len; i++) {
		    PRINTF("%02X ", msg[i]);
		}
		PRINTF(" (Decimal: %u)\r\n", time_attest);

		PRINTF("\r\n=== Signature Components Breakdown ===\r\n");
		PRINTF("The signature was generated over:\r\n");
		PRINTF("- n_dev (32 bytes)\r\n");
		PRINTF("- time_cur (4 bytes)\r\n");
		PRINTF("- id_dev (4 bytes): %u\r\n", id_dev);
		PRINTF("- H(M_SRV_URL) (32 bytes)\r\n");
		PRINTF("- attest_result (1 byte)\r\n");
		PRINTF("- time_attest (4 bytes)\r\n");

		PRINTF("\r\n=== Complete Raw Message ===\r\n");
		PRINTF("Full message (length %d): ", msg_len);
		for(int i = 0; i < msg_len; i++) {
		    PRINTF("%02X ", msg[i]);
		}
		PRINTF("\r\n");

	USART_WriteBlocking(WIFI_USART, msg, msg_len);

}


/* Secure main(). */
/*!
 * @brief Main function
 */

// TODO: Currently, if failure takes place, then just stops working. We might need to keep working normally, but without broadcasting
#define RX_RING_BUFFER_SIZE 100U

uint8_t g_rxRingBuffer[RX_RING_BUFFER_SIZE] = {0}; /* RX ring buffer. */
uint8_t g_resp_bufferfer[RX_RING_BUFFER_SIZE] = {0}; /* Buffer for receive data to echo. */


uint8_t ringBuffer[RX_RING_BUFFER_SIZE];
volatile uint16_t rxIndex = 0; /* Index of the memory to save new arrived data. */

void ctimer_init()
{
	ctimer_config_t config;
	ctimer_match_config_t matchConfig;


	CTIMER_GetDefaultConfig(&config);
	CTIMER_Init(CTIMER, &config);

	/* Configuration 0 */
	matchConfig.enableCounterReset = true;
	matchConfig.enableCounterStop  = false;
	matchConfig.matchValue         = CTIMER_CLK_FREQ * PACKET_SEND_TIMER;
	matchConfig.outControl         = kCTIMER_Output_Toggle;
	matchConfig.outPinInitState    = false;
	matchConfig.enableInterrupt    = true;

	CTIMER_RegisterCallBack(CTIMER, ctimer_callback_table, kCTIMER_MultipleCallback);
	CTIMER_SetupMatch(CTIMER, CTIMER_MAT_OUT, &matchConfig);
	CTIMER_StartTimer(CTIMER);

}

int main(void)
{
	usart_config_t config;
	uint8_t req_buffer[BUF_SIZE] = {0, };
	uint8_t resp_buffer[BUF_SIZE] = {0, };
	int ret = 0;
	uint32_t cycle_records[5] = {0, };
	mbedtls_entropy_context entropy = {0, };


	/* Init DWT at the beginning of main function*/
	DWT->CTRL |= (1 << DWT_CTRL_CYCCNTENA_Pos);
	DWT->CYCCNT = 0;

    /* Init board hardware. */
    /* set BOD VBAT level to 1.65V */
    POWER_SetBodVbatLevel(kPOWER_BodVbatLevel1650mv, kPOWER_BodHystLevel50mv, false);
    gpio_pin_config_t xLedConfig = {.pinDirection = kGPIO_DigitalOutput, .outputLogic = 1};

    /* Initialize GPIO for LEDs. */
    GPIO_PortInit(GPIO, LED_PORT);
    GPIO_PinInit(GPIO, LED_PORT, GREEN_LED_PIN, &(xLedConfig));
    GPIO_PinInit(GPIO, LED_PORT, BLUE_LED_PIN, &(xLedConfig));

    /* Set non-secure vector table */
    SCB_NS->VTOR = mainNONSECURE_APP_START_ADDRESS;

    /* attach main clock divide to FLEXCOMM0 (debug console) */
    CLOCK_AttachClk(BOARD_DEBUG_UART_CLK_ATTACH);

    /* Use FRO_HF clock as input clock source. */
    CLOCK_AttachClk(kFRO_HF_to_CTIMER2);

    /* enable clock for GPIO*/
	CLOCK_EnableClock(kCLOCK_Gpio0);
	CLOCK_EnableClock(kCLOCK_Gpio1);

    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitDebugConsole();

	if( CRYPTO_InitHardware() != kStatus_Success )
	{
		PRINTF( "Initialization of crypto HW failed\n\r" );
		while(1);
	}
	PRINTF ("HELLO WORLD\n\r");
	/* Init SysTick module */
	/* call CMSIS SysTick function. It enables the SysTick interrupt at low priority */
	SysTick_Config(CLOCK_GetCoreSysClkFreq() / CONVERT_MS_TO_S); /* 1 ms period */

    USART_GetDefaultConfig(&config);
	config.baudRate_Bps = BOARD_DEBUG_UART_BAUDRATE;
	config.enableTx     = true;
	config.enableRx     = true;

	USART_Init(WIFI_USART, &config, WIFI_USART_CLK_FREQ);

	USART_Init(WIFI_USART2, &config, WIFI_USART2_CLK_FREQ);

    USART_EnableInterrupts(WIFI_USART, kUSART_RxLevelInterruptEnable | kUSART_RxErrorInterruptEnable);
    USART_EnableInterrupts(WIFI_USART2, kUSART_RxLevelInterruptEnable | kUSART_RxErrorInterruptEnable);
    EnableIRQ(WIFI_USART_IRQn);
    EnableIRQ(WIFI_USART2_IRQn);

    /* Init RTC */
   	RTC_Init(RTC);

   	mbedtls_pk_init(&private_key);

	ret = mbedtls_pk_parse_key(&private_key, PRV_DEV_KEY_PEM, strlen(PRV_DEV_KEY_PEM)+1, NULL, 0);
	if(ret != 0){while(1);}
	mbedtls_entropy_init(&entropy);
	mbedtls_ctr_drbg_init( &ctr_drbg );
	ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
								mbedtls_entropy_func,
								&entropy,
								(const unsigned char *) "RANDOM_GEN",
								10);
	if(ret != 0){while(1);}

	DisableIRQ(WIFI_USART_IRQn);    
	DisableIRQ(WIFI_USART2_IRQn);   

#ifdef PERFORMANCE_EVALUATION
	cycle_records[0] = DWT->CYCCNT;
#endif
	//syncReq(req_buffer);
	PRINTF("sync req \n\r");
#ifdef PERFORMANCE_EVALUATION
	cycle_records[1] = DWT->CYCCNT;
#endif
	//syncResp(req_buffer, resp_buffer);
	PRINTF("sync resp \n\r");
#ifdef PERFORMANCE_EVALUATION
	cycle_records[2] = DWT->CYCCNT;
#endif

	//syncAck(resp_buffer);
	PRINTF("sync ack \n\r");
	EnableIRQ(WIFI_USART_IRQn);     // Re-enable USART interrupt
	EnableIRQ(WIFI_USART2_IRQn);    // Re-enable USART2 interrupt
   	/* init CTimer */
   	ctimer_init();
#ifdef PERFORMANCE_EVALUATION
   	cycle_records[3] = DWT->CYCCNT;
#endif

   	announcement();

   	PRINTF("Finish booting process with time sync \n\r");

#ifdef PERFORMANCE_EVALUATION
   	cycle_records[4] = DWT->CYCCNT;

   	PRINTF("Pure boot/Send/Read/ACK/Boot/Announcement/Total\n\r");
   	PRINTF("%u %u %u %u %u %u %u\n\r", cycle_records[0], cycle_records[1] - cycle_records[0], cycle_records[2] - cycle_records[1],
   			cycle_records[3] - cycle_records[2], cycle_records[3] - cycle_records[0], cycle_records[4] - cycle_records[3], cycle_records[4]);
#endif

   	/* Boot the non-secure code. */
	BootNonSecure(mainNONSECURE_APP_START_ADDRESS);

    /* Non-secure software does not return, this code is not executed. */
    for (;;)
    {
    }

    // clean up
	mbedtls_ctr_drbg_free(&ctr_drbg);
	mbedtls_entropy_free(&entropy);
}
/*-----------------------------------------------------------*/

void vGetRegistersFromStack(uint32_t *pulFaultStackAddress)
{
    /* These are volatile to try and prevent the compiler/linker optimising them
     * away as the variables never actually get used.  If the debugger won't show the
     * values of the variables, make them global my moving their declaration outside
     * of this function. */
    volatile uint32_t r0;
    volatile uint32_t r1;
    volatile uint32_t r2;
    volatile uint32_t r3;
    volatile uint32_t r12;
    volatile uint32_t lr;  /* Link register. */
    volatile uint32_t pc;  /* Program counter. */
    volatile uint32_t psr; /* Program status register. */
    volatile uint32_t _CFSR;
    volatile uint32_t _HFSR;
    volatile uint32_t _DFSR;
    volatile uint32_t _AFSR;
    volatile uint32_t _SFSR;
    volatile uint32_t _BFAR;
    volatile uint32_t _MMAR;
    volatile uint32_t _SFAR;

    r0 = pulFaultStackAddress[0];
    r1 = pulFaultStackAddress[1];
    r2 = pulFaultStackAddress[2];
    r3 = pulFaultStackAddress[3];

    r12 = pulFaultStackAddress[4];
    lr  = pulFaultStackAddress[5];
    pc  = pulFaultStackAddress[6];
    psr = pulFaultStackAddress[7];

    /* Configurable Fault Status Register. Consists of MMSR, BFSR and UFSR. */
    _CFSR = (*((volatile unsigned long *)(0xE000ED28)));

    /* Hard Fault Status Register. */
    _HFSR = (*((volatile unsigned long *)(0xE000ED2C)));

    /* Debug Fault Status Register. */
    _DFSR = (*((volatile unsigned long *)(0xE000ED30)));

    /* Auxiliary Fault Status Register. */
    _AFSR = (*((volatile unsigned long *)(0xE000ED3C)));

    /* Secure Fault Status Register. */
    _SFSR = (*((volatile unsigned long *)(0xE000EDE4)));

    /* Read the Fault Address Registers. Note that these may not contain valid
     * values. Check BFARVALID/MMARVALID to see if they are valid values. */
    /* MemManage Fault Address Register. */
    _MMAR = (*((volatile unsigned long *)(0xE000ED34)));

    /* Bus Fault Address Register. */
    _BFAR = (*((volatile unsigned long *)(0xE000ED38)));

    /* Secure Fault Address Register. */
    _SFAR = (*((volatile unsigned long *)(0xE000EDE8)));

    /* Remove compiler warnings about the variables not being used. */
    (void)r0;
    (void)r1;
    (void)r2;
    (void)r3;
    (void)r12;
    (void)lr;  /* Link register. */
    (void)pc;  /* Program counter. */
    (void)psr; /* Program status register. */
    (void)_CFSR;
    (void)_HFSR;
    (void)_DFSR;
    (void)_AFSR;
    (void)_SFSR;
    (void)_MMAR;
    (void)_BFAR;
    (void)_SFAR;

    /* When the following line is hit, the variables contain the register values. */
    for (;;)
    {
    }
}
/*-----------------------------------------------------------*/
