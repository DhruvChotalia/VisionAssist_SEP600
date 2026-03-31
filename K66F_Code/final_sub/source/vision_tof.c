/**
 * vision_tof.c — VisionAssist K66F Task Implementation
 * ======================================================
 */

#include "vision_tof.h"
#include "i2c_hal.h"
#include "fsl_uart.h"
#include "fsl_gpio.h"
#include "fsl_port.h"
#include "fsl_debug_console.h"
#include "vl53l0x_api.h"
#include "vibration_motor.h"
#include "audio_player.h"
#include "display_manager.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"    /* Added for the Interrupt Ring Buffer */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ── Configuration ──────────────────────────────────────────────────────── */
#define ESP_UART              UART1
#define DETECTION_THRESHOLD   800
#define DANGER_THRESHOLD      200
#define AUDIO_BUF_MAX         80000
#define WAV_HEADER_BYTES      44
#define UART_LINE_TIMEOUT_MS  120

#define AUDIO_RX_TOTAL_TIMEOUT_MS 15000
#define AUDIO_RX_BYTE_TIMEOUT_MS  1000

#define ALERT_COOLDOWN_MS     1000
#define DIST_TX_INTERVAL_MS   100
#define COMM_STARTUP_DELAY_MS 3500

#define MOTOR_STACK_WORDS    (configMINIMAL_STACK_SIZE + 512)
#define COMM_STACK_WORDS     (configMINIMAL_STACK_SIZE + 1024)
#define BUTTON_STACK_WORDS   (configMINIMAL_STACK_SIZE + 256)

/* ── Extern mutexes (created in main.c) ─────────────────────────────────── */
extern SemaphoreHandle_t xDisplayMutex;
extern SemaphoreHandle_t xAudioMutex;
extern SemaphoreHandle_t xUartTxMutex;

/* ── Static allocation ──────────────────────────────────────────────────── */
static VL53L0X_Dev_t MyDevice;
static VL53L0X_DEV   Dev = &MyDevice;

static uint8_t audioBuffer[AUDIO_BUF_MAX];
static QueueHandle_t uartRxQueue = NULL; /* Software Ring Buffer */

static char lcd_line1[17] = "Dist: -- mm";
static char lcd_line2[17] = "AI: Waiting...";

static volatile uint16_t g_lastDistance  = 0;
static volatile bool     g_ai_paused     = false;
static volatile bool     g_audio_playing = false;
static volatile TickType_t g_audio_ready_ts = 0;

static volatile TickType_t g_mode_msg_expires = 0;
static volatile bool       g_mode_msg_active  = false;

static volatile bool       g_dist_override_active = false;
static volatile uint16_t   g_dist_override_val    = 0;

/* ═══════════════════════════════════════════════════════════════════════════
 * UART Hardware Interrupt & Queue Helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Instant hardware interrupt - catches bytes regardless of FreeRTOS task priority */
void UART1_RX_TX_IRQHandler(void) {
    uint32_t flags = UART_GetStatusFlags(ESP_UART);
    BaseType_t woken = pdFALSE;

    // Clear any hardware overruns immediately
    if (flags & (kUART_RxOverrunFlag | kUART_NoiseErrorFlag | kUART_FramingErrorFlag)) {
        UART_ClearStatusFlags(ESP_UART, kUART_RxOverrunFlag | kUART_NoiseErrorFlag | kUART_FramingErrorFlag);
    }

    // Safely pull the byte into our FreeRTOS queue
    if (flags & kUART_RxDataRegFullFlag) {
        uint8_t data = UART_ReadByte(ESP_UART);
        xQueueSendFromISR(uartRxQueue, &data, &woken);
    }

    portYIELD_FROM_ISR(woken);
}

static void UART_FlushRX(UART_Type *base) {
    uint8_t dummy;
    // Drain our software queue
    while (xQueueReceive(uartRxQueue, &dummy, 0) == pdTRUE) {}
    UART_ClearStatusFlags(base, kUART_RxOverrunFlag | kUART_NoiseErrorFlag | kUART_FramingErrorFlag);
}

static int UART_ReadLine(UART_Type *base, char *buf, int maxLen, uint32_t timeoutMs) {
    int idx = 0;
    while (idx < maxLen - 1) {
        uint8_t c;
        // Block task cleanly until a byte arrives or we timeout
        if (xQueueReceive(uartRxQueue, &c, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
            if (c == '\n') { buf[idx] = '\0'; return idx; }
            if (c == '\r') continue;
            if (idx == 0 && (c < 0x20 || c > 0x7E)) continue;
            buf[idx++] = (char)c;
        } else {
            break;
        }
    }
    buf[idx] = '\0';
    return idx;
}

static uint32_t UART_ReadBytes(UART_Type *base, uint8_t *buf, uint32_t length, uint32_t totalTimeoutMs) {
    uint32_t received = 0;
    TickType_t start = xTaskGetTickCount();

    while (received < length) {
        TickType_t elapsed = (xTaskGetTickCount() - start) * portTICK_PERIOD_MS;
        if (elapsed >= totalTimeoutMs) break;

        uint8_t c;
        if (xQueueReceive(uartRxQueue, &c, pdMS_TO_TICKS(AUDIO_RX_BYTE_TIMEOUT_MS)) == pdTRUE) {
            buf[received++] = c;
        } else {
            break;
        }
    }
    return received;
}

static inline void UART_SendStringSafe(UART_Type *base, const char *s)
{
    if (xSemaphoreTake(xUartTxMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        UART_WriteBlocking(base, (const uint8_t *)s, strlen(s));
        xSemaphoreGive(xUartTxMutex);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * LCD helper — must be called while holding xDisplayMutex
 * ═══════════════════════════════════════════════════════════════════════════ */
static inline void SetLine2AndFlush(const char *text)
{
    snprintf(lcd_line2, sizeof(lcd_line2), "%s", text);
    Display_UpdateScreen(lcd_line1, lcd_line2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ToF_Init
 * ═══════════════════════════════════════════════════════════════════════════ */
static bool ToF_Init(void)
{
    VL53L0X_Error status;
    uint8_t  vhvSettings, phaseCal;
    uint32_t refSpadCount;
    uint8_t  isApertureSpads;

    memset(Dev, 0, sizeof(VL53L0X_Dev_t));
    Dev->I2cDevAddr      = 0x29;
    Dev->comms_type      = 1;
    Dev->comms_speed_khz = 100;

    status = VL53L0X_DataInit(Dev);              if (status) return false; vTaskDelay(pdMS_TO_TICKS(100));
    status = VL53L0X_StaticInit(Dev);            if (status) return false; vTaskDelay(pdMS_TO_TICKS(100));
    status = VL53L0X_PerformRefCalibration(Dev, &vhvSettings, &phaseCal);   if (status) return false; vTaskDelay(pdMS_TO_TICKS(100));
    status = VL53L0X_PerformRefSpadManagement(Dev, &refSpadCount, &isApertureSpads); if (status) return false; vTaskDelay(pdMS_TO_TICKS(100));
    status = VL53L0X_SetDeviceMode(Dev, VL53L0X_DEVICEMODE_CONTINUOUS_RANGING); if (status) return false;
    status = VL53L0X_StartMeasurement(Dev);      if (status) return false;

    return true;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Motor_Task  (Priority: 3)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void Motor_Task(void *pvParameters)
{
    if (!ToF_Init()) {
        if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            snprintf(lcd_line1, sizeof(lcd_line1), "ToF: retry...");
            Display_UpdateScreen(lcd_line1, lcd_line2);
            xSemaphoreGive(xDisplayMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        I2C_HAL_BusRecover();
        vTaskDelay(pdMS_TO_TICKS(200));
        if (!ToF_Init()) {
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                snprintf(lcd_line1, sizeof(lcd_line1), "ToF: OFFLINE");
                SetLine2AndFlush("Check wiring");
                xSemaphoreGive(xDisplayMutex);
            }
            vTaskSuspend(NULL);
            return;
        }
    }

    VL53L0X_RangingMeasurementData_t rangingData;
    char       distTxBuf[24];
    TickType_t lastAlertTx = 0;

    for (;;) {
        VL53L0X_GetRangingMeasurementData(Dev, &rangingData);

        uint16_t dist = 0;
        bool valid_reading = false;

        if (g_dist_override_active) {
            dist = g_dist_override_val;
            valid_reading = true;
        } else {
            if (rangingData.RangeStatus == 0) {
                dist = rangingData.RangeMilliMeter;
                valid_reading = true;
            }
        }

        if (valid_reading) {
            g_lastDistance = dist;

            if (dist < DETECTION_THRESHOLD) Vibration_Motor_SetIntensity(dist, DETECTION_THRESHOLD);
            else                            Vibration_Motor_Off();

            if (!g_audio_playing) {
                if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    if (g_dist_override_active) {
                        snprintf(lcd_line1, sizeof(lcd_line1), "OvrD: %u mm", dist);
                    } else if (dist < DANGER_THRESHOLD) {
                        snprintf(lcd_line1, sizeof(lcd_line1), "DANGER: %u mm", dist);
                    } else {
                        snprintf(lcd_line1, sizeof(lcd_line1), "Dist: %u mm",   dist);
                    }
                    Display_UpdateScreen(lcd_line1, lcd_line2);
                    xSemaphoreGive(xDisplayMutex);
                }

                if (!g_ai_paused && !g_dist_override_active) {
                    snprintf(distTxBuf, sizeof(distTxBuf), "DIST:%u\n", dist);
                    UART_SendStringSafe(ESP_UART, distTxBuf);

                    if (dist < DANGER_THRESHOLD) {
                        TickType_t now = xTaskGetTickCount();
                        if ((now - lastAlertTx) >= pdMS_TO_TICKS(ALERT_COOLDOWN_MS)) {
                            snprintf(distTxBuf, sizeof(distTxBuf), "ALERT:%u\n", dist);
                            UART_SendStringSafe(ESP_UART, distTxBuf);
                            lastAlertTx = now;
                        }
                    }
                }
            }
        } else {
            Vibration_Motor_Off();
            g_lastDistance = 0;
            if (!g_audio_playing) {
                if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    snprintf(lcd_line1, sizeof(lcd_line1), "Dist: Clear");
                    Display_UpdateScreen(lcd_line1, lcd_line2);
                    xSemaphoreGive(xDisplayMutex);
                }
            }
        }

        VL53L0X_ClearInterruptMask(Dev, VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);
        vTaskDelay(pdMS_TO_TICKS(DIST_TX_INTERVAL_MS));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Comm_Task  (Priority: 2)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void Comm_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(COMM_STARTUP_DELAY_MS));
    char rxLine[160];

    for (;;) {
        TickType_t now = xTaskGetTickCount();

        if (g_audio_playing && (now - g_audio_ready_ts > pdMS_TO_TICKS(4000))) {
            PRINTF("[Comm] AUDIO RECOVERY\r\n");
            g_audio_playing = false;
            UART_FlushRX(ESP_UART);
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                SetLine2AndFlush(g_ai_paused ? "AI: PAUSED" : "AI: Waiting...");
                xSemaphoreGive(xDisplayMutex);
            }
        }

        if (g_mode_msg_active && now > g_mode_msg_expires) {
            g_mode_msg_active = false;
            if (!g_audio_playing) {
                if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    SetLine2AndFlush(g_ai_paused ? "AI: PAUSED" : "AI: Waiting...");
                    xSemaphoreGive(xDisplayMutex);
                }
            }
        }

        int rxLen = UART_ReadLine(ESP_UART, rxLine, sizeof(rxLine), UART_LINE_TIMEOUT_MS);
        if (rxLen <= 0) continue;

        if (strncmp(rxLine, "OBJ:", 4) == 0) {
            g_mode_msg_active = false;
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                char tmp[17]; snprintf(tmp, sizeof(tmp), "AI: %s", &rxLine[4]);
                SetLine2AndFlush(tmp);
                xSemaphoreGive(xDisplayMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        else if (strncmp(rxLine, "LCD:", 4) == 0) {
            g_mode_msg_active = false;
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                SetLine2AndFlush(&rxLine[4]);
                xSemaphoreGive(xDisplayMutex);
            }
            PRINTF("[Comm] LCD override: %s\r\n", &rxLine[4]);
        }

        else if (strcmp(rxLine, "LCD_CLEAR") == 0) {
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                SetLine2AndFlush(g_ai_paused ? "AI: PAUSED" : "AI: Waiting...");
                xSemaphoreGive(xDisplayMutex);
            }
            PRINTF("[Comm] LCD override cleared\r\n");
        }

        else if (strncmp(rxLine, "OVR_DIST:", 9) == 0) {
            g_dist_override_val = (uint16_t)atoi(&rxLine[9]);
            g_dist_override_active = true;

            if (g_dist_override_val < DETECTION_THRESHOLD) {
                Vibration_Motor_SetIntensity(g_dist_override_val, DETECTION_THRESHOLD);
            } else {
                Vibration_Motor_Off();
            }

            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                snprintf(lcd_line1, sizeof(lcd_line1), "OvrD: %u mm", g_dist_override_val);
                Display_UpdateScreen(lcd_line1, lcd_line2);
                xSemaphoreGive(xDisplayMutex);
            }
            PRINTF("[Comm] Distance Override ON: %u\r\n", g_dist_override_val);
        }

        else if (strcmp(rxLine, "OVR_DIST_CLR") == 0) {
            g_dist_override_active = false;
            Vibration_Motor_Off();
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                snprintf(lcd_line1, sizeof(lcd_line1), "Dist: Clear");
                Display_UpdateScreen(lcd_line1, lcd_line2);
                xSemaphoreGive(xDisplayMutex);
            }
            PRINTF("[Comm] Distance Override OFF\r\n");
        }

        else if (strncmp(rxLine, "CMD:", 4) == 0) {
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                char tmp[17]; snprintf(tmp, sizeof(tmp), "Mode: %s", &rxLine[4]);
                SetLine2AndFlush(tmp);
                xSemaphoreGive(xDisplayMutex);
                g_mode_msg_expires = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
                g_mode_msg_active  = true;
            }
        }

        else if (strncmp(rxLine, "SYS:", 4) == 0 || strncmp(rxLine, "ERR:", 4) == 0) {
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                SetLine2AndFlush(&rxLine[4]);
                xSemaphoreGive(xDisplayMutex);
            }
        }

        else if (strcmp(rxLine, "AUDIO_READY") == 0) {
            g_mode_msg_active = false;
            g_audio_playing   = true;
            g_audio_ready_ts  = xTaskGetTickCount();
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                SetLine2AndFlush("Playing...");
                xSemaphoreGive(xDisplayMutex);
            }
            // Send the required ACK Handshake to the ESP32
            UART_SendStringSafe(ESP_UART, "ACK_AUDIO\n");
        }

        else if (strncmp(rxLine, "AUDIO:", 6) == 0) {
            uint32_t totalBytes = (uint32_t)atoi(&rxLine[6]);

            bool headerValid = true;
            for (int i = 6; i < rxLen && rxLine[i] != '\0'; i++) {
                if (rxLine[i] < '0' || rxLine[i] > '9') { headerValid = false; break; }
            }

            if (!headerValid || totalBytes <= WAV_HEADER_BYTES || totalBytes > AUDIO_BUF_MAX) {
                PRINTF("[Comm] AUDIO invalid size %u\r\n", totalBytes);
                g_audio_playing = false;
                continue;
            }

            if (xSemaphoreTake(xAudioMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
                uint32_t received = UART_ReadBytes(ESP_UART, audioBuffer, totalBytes, AUDIO_RX_TOTAL_TIMEOUT_MS);
                PRINTF("[Comm] AUDIO %u/%u bytes\r\n", received, totalBytes);

                if (received > WAV_HEADER_BYTES) {
                    Audio_Player_Play(audioBuffer + WAV_HEADER_BYTES, received - WAV_HEADER_BYTES);

                    TickType_t playStart   = xTaskGetTickCount();
                    TickType_t maxPlayTime = pdMS_TO_TICKS(((received * 1000) / 8000) + 1000);

                    while (Audio_Player_IsBusy()) {
                        if (xTaskGetTickCount() - playStart > maxPlayTime) {
                            PRINTF("[Comm] Audio hang — forcing unblock\r\n");
                            break;
                        }
                        vTaskDelay(pdMS_TO_TICKS(50));
                    }
                }
                xSemaphoreGive(xAudioMutex);
            }

            vTaskDelay(pdMS_TO_TICKS(500));
            UART_FlushRX(ESP_UART);
            g_audio_playing = false;

            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                SetLine2AndFlush(g_ai_paused ? "AI: PAUSED" : "AI: Waiting...");
                xSemaphoreGive(xDisplayMutex);
            }
        }

        else {
            PRINTF("[Comm] Unknown: '%s'\r\n", rxLine);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Button_Task  (Priority: 2)
 * ═══════════════════════════════════════════════════════════════════════════ */
static void Button_Task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t lastState = 0;
    bool    wasPaused = false;

    for (;;) {
        bool isPaused = (bool)GPIO_PinRead(GPIOA, 6U);
        if (isPaused != wasPaused) {
            wasPaused   = isPaused;
            g_ai_paused = isPaused;
            if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                SetLine2AndFlush(isPaused ? "AI: PAUSED" : "AI: Resumed...");
                xSemaphoreGive(xDisplayMutex);
            }
        }

        uint32_t p1 = GPIO_PinRead(GPIOA, 1U);
        uint32_t p2 = GPIO_PinRead(GPIOC, 16U);

        if (p1 || p2) {
            vTaskDelay(pdMS_TO_TICKS(40));
            p1 = GPIO_PinRead(GPIOA, 1U);
            p2 = GPIO_PinRead(GPIOC, 16U);

            uint8_t state = (p1 && p2) ? 3 : p2 ? 1 : p1 ? 2 : 0;

            if (state != 0 && state != lastState) {
                const char *label = NULL, *cmd = NULL;
                switch (state) {
                    case 1: label = "Mode: OCR";     cmd = "ocr\n";   break;
                    case 2: label = "Mode: Object";  cmd = "obj\n";   break;
                    case 3: label = "Mode: OCR+Obj"; cmd = "oando\n"; break;
                }
                if (cmd) {
                    UART_SendStringSafe(ESP_UART, cmd);
                    if (xSemaphoreTake(xDisplayMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                        SetLine2AndFlush(label);
                        xSemaphoreGive(xDisplayMutex);
                        g_mode_msg_expires = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
                        g_mode_msg_active  = true;
                    }
                    vTaskDelay(pdMS_TO_TICKS(300));
                }
            }
            lastState = state;
        } else {
            lastState = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Vision_ToF_Init
 * ═══════════════════════════════════════════════════════════════════════════ */
void Vision_ToF_Init(void)
{
    /* Initialize the massive 2KB software buffer for the UART */
    uartRxQueue = xQueueCreate(2048, sizeof(uint8_t));

    /* Enable IRQs so FreeRTOS catches every byte automatically */
    UART_EnableInterrupts(ESP_UART, kUART_RxDataRegFullInterruptEnable | kUART_RxOverrunInterruptEnable | kUART_NoiseErrorInterruptEnable | kUART_FramingErrorInterruptEnable);
    EnableIRQ(UART1_RX_TX_IRQn);
    NVIC_SetPriority(UART1_RX_TX_IRQn, 5); /* Safe FreeRTOS Priority */

    /* PTA6 — AI pause toggle button */
    CLOCK_EnableClock(kCLOCK_PortA);
    port_pin_config_t pta6_cfg = {
        .pullSelect          = kPORT_PullDown,
        .slewRate            = kPORT_FastSlewRate,
        .passiveFilterEnable = kPORT_PassiveFilterEnable,
        .openDrainEnable     = kPORT_OpenDrainDisable,
        .driveStrength       = kPORT_LowDriveStrength,
        .mux                 = kPORT_MuxAsGpio,
        .lockRegister        = kPORT_UnlockRegister
    };
    PORT_SetPinConfig(PORTA, 6U, &pta6_cfg);
    gpio_pin_config_t pta6_gpio = { kGPIO_DigitalInput, 0 };
    GPIO_PinInit(GPIOA, 6U, &pta6_gpio);

    BaseType_t ok = pdPASS;
    ok &= xTaskCreate(Motor_Task,  "Motor",  MOTOR_STACK_WORDS,  NULL, tskIDLE_PRIORITY + 3, NULL);
    ok &= xTaskCreate(Comm_Task,   "Comm",   COMM_STACK_WORDS,   NULL, tskIDLE_PRIORITY + 2, NULL);
    ok &= xTaskCreate(Button_Task, "Button", BUTTON_STACK_WORDS, NULL, tskIDLE_PRIORITY + 2, NULL);

    if (ok != pdPASS) {
        PRINTF("[FATAL] Task creation failed\r\n");
        while (1);
    }
}
