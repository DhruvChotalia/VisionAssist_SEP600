#include "vision_tof.h"
#include "i2c_hal.h"
#include "fsl_uart.h"
#include "fsl_gpio.h"
#include "fsl_debug_console.h"
#include "vl53l0x_api.h"
#include "vibration_motor.h"
#include "audio_player.h"
#include "display_manager.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ESP_UART UART1
#define DETECTION_THRESHOLD 800

/* ---- MASSIVELY INCREASED BUFFER TO HANDLE LONG TTS FILES ---- */
#define AUDIO_BUF_MAX  80000
VL53L0X_Dev_t MyDevice;
VL53L0X_DEV   Dev = &MyDevice;
static uint8_t audioBuffer[AUDIO_BUF_MAX];

/* Shared variables between tasks */
static volatile uint16_t g_latestDistance = 0;
static volatile int g_objectDetected = 0;
static volatile int g_needsSend = 0;

/* Structured variable to hold JSON data for the milestone */
typedef struct {
    char label[128];
    uint16_t distance;
    char danger[16];
} DetectionData_t;

DetectionData_t currentDetection;

/* Thread-Safe LCD Updater: Takes a header (Line 1) and content (Line 2) */
static void Safe_Display_Update(const char* header, const char* content) {
    vTaskSuspendAll();
    Display_UpdateScreen(header, content);
    xTaskResumeAll();
}

/* Lightweight JSON Extractor for K66F */
static void ParseDetectionJSON(const char* jsonStr, DetectionData_t* data) {
    memset(data->label, 0, sizeof(data->label));
    data->distance = 0;
    memset(data->danger, 0, sizeof(data->danger));

    char* ptr = strstr(jsonStr, "\"label\":\"");
    if (ptr) {
        ptr += 9;
        int i = 0;
        while (*ptr != '\"' && *ptr != '\0' && i < 127) {
            data->label[i++] = *ptr++;
        }
    }

    ptr = strstr(jsonStr, "\"distance\":");
    if (ptr) {
        ptr += 11;
        data->distance = (uint16_t)atoi(ptr);
    }

    ptr = strstr(jsonStr, "\"danger\":\"");
    if (ptr) {
        ptr += 10;
        int i = 0;
        while (*ptr != '\"' && *ptr != '\0' && i < 15) {
            data->danger[i++] = *ptr++;
        }
    }
}

/* Atomic UART Reader */
static int UART_ReadLine(UART_Type *base, char *buf, int maxLen, uint32_t timeoutMs) {
    int idx = 0;
    TickType_t start = xTaskGetTickCount();

    while (idx < maxLen - 1) {
        if (idx == 0) {
            if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(timeoutMs)) {
                break;
            }
            if (kUART_RxDataRegFullFlag & UART_GetStatusFlags(base)) {
                uint8_t c;
                UART_ReadBlocking(base, &c, 1);
                if (c != '\n' && c != '\r') {
                    buf[idx++] = (char)c;
                    vTaskSuspendAll();
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        } else {
            uint32_t spins = 0;
            while (!(kUART_RxDataRegFullFlag & UART_GetStatusFlags(base))) {
                spins++;
                if (spins > 500000) {
                    xTaskResumeAll();
                    buf[idx] = '\0';
                    return idx;
                }
            }

            uint8_t c;
            UART_ReadBlocking(base, &c, 1);
            if (c == '\n' || c == '\r') {
                xTaskResumeAll();
                buf[idx] = '\0';
                return idx;
            }
            buf[idx++] = (char)c;
        }
    }
    if (idx > 0) xTaskResumeAll();
    buf[idx] = '\0';
    return idx;
}

/* Read exact number of bytes from UART (Used for Audio) */
/* Hyper-Optimized Binary UART Reader (No dropped bytes!) */
/* Resilient UART Reader: Handles Wi-Fi Network Gaps gracefully! */
static int UART_ReadBytes(UART_Type *base, uint8_t *buf, uint32_t length, uint32_t timeoutMs) {
    uint32_t received = 0;
    TickType_t last_rx_time = xTaskGetTickCount();

    while (received < length) {
        /* If 3 full seconds pass without a SINGLE byte arriving, abort the transfer */
        if ((xTaskGetTickCount() - last_rx_time) > pdMS_TO_TICKS(3000)) {
            break;
        }

        if (kUART_RxDataRegFullFlag & UART_GetStatusFlags(base)) {
            uint8_t c;
            UART_ReadBlocking(base, &c, 1);
            buf[received++] = c;

            /* We got a byte! Reset the timeout clock. */
            last_rx_time = xTaskGetTickCount();
        } else {
            /* No byte yet (Wi-Fi is buffering).
               Yield to let FreeRTOS background tasks (like ToF) run briefly! */
            taskYIELD();
        }
    }

    return received;
}

/* Drain any pending UART RX data */
static void UART_DrainRx(UART_Type *base) {
    uint8_t dummy;
    while (kUART_RxDataRegFullFlag & UART_GetStatusFlags(base)) {
        UART_ReadBlocking(base, &dummy, 1);
    }
}

/* TASK 1: Motor + ToF */
static void Motor_Task(void *pvParameters) {
    VL53L0X_RangingMeasurementData_t RangingData;
    VL53L0X_Error status = VL53L0X_ERROR_NONE;
    uint8_t VhvSettings, PhaseCal;
    uint32_t refSpadCount;
    uint8_t isApertureSpads;

    Dev->I2cDevAddr = 0x29;
    status |= VL53L0X_DataInit(Dev);
    status |= VL53L0X_StaticInit(Dev);
    status |= VL53L0X_PerformRefCalibration(Dev, &VhvSettings, &PhaseCal);
    status |= VL53L0X_PerformRefSpadManagement(Dev, &refSpadCount, &isApertureSpads);
    status |= VL53L0X_SetDeviceMode(Dev, VL53L0X_DEVICEMODE_CONTINUOUS_RANGING);
    status |= VL53L0X_StartMeasurement(Dev);

    if (status != VL53L0X_ERROR_NONE) {
        PRINTF("[!] ToF Sensor Hardware Error.\r\n");
        while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    PRINTF("Radar Online. Monitoring for obstacles...\r\n");
    uint16_t last_sent_distance = 0xFFFF;

    for (;;) {
        VL53L0X_GetRangingMeasurementData(Dev, &RangingData);

        if (RangingData.RangeStatus == 0) {
            uint16_t distance = RangingData.RangeMilliMeter;

            if (distance < DETECTION_THRESHOLD) {
                Vibration_Motor_SetIntensity(distance, DETECTION_THRESHOLD);
                g_latestDistance = distance;
                g_objectDetected = 1;

                int diff = abs((int)distance - (int)last_sent_distance);
                if (diff >= 10 || last_sent_distance == 0xFFFF) {
                    g_needsSend = 1;
                    last_sent_distance = distance;
                }
            } else {
                Vibration_Motor_Off();
                g_objectDetected = 0;
                last_sent_distance = 0xFFFF;
            }
        } else {
            Vibration_Motor_Off();
            g_objectDetected = 0;
            last_sent_distance = 0xFFFF;
        }

        VL53L0X_ClearInterruptMask(Dev, VL53L0X_REG_SYSTEM_INTERRUPT_GPIO_NEW_SAMPLE_READY);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* TASK 2: Communication Task */
static void Comm_Task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    PRINTF("[COMM TASK] Ready. Listening to ESP32...\r\n");

    for (;;) {
        if (g_needsSend && g_objectDetected) {
            g_needsSend = 0;
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "ALERT:%u\n", g_latestDistance);

            vTaskSuspendAll();
            UART_WriteBlocking(ESP_UART, (uint8_t*)buf, len);
            xTaskResumeAll();
        }

        char rxLine[256];
        int rxLen = UART_ReadLine(ESP_UART, rxLine, sizeof(rxLine), 100);

        if (rxLen > 0) {
            if (strncmp(rxLine, "CMD:", 4) == 0) {
                PRINTF("[UART-RX] Web Command: %s\r\n", rxLine);
                if (rxLine[4] == 'o' && rxLine[5] == 'c') {
                    Safe_Display_Update("Current Mode:", "OCR Mode (Web)");
                } else if (rxLine[4] == 'o' && rxLine[5] == 'b') {
                    Safe_Display_Update("Current Mode:", "Object (Web)");
                } else if (rxLine[4] == 'o' && rxLine[5] == 'a') {
                    Safe_Display_Update("Current Mode:", "OCR+Obj (Web)");
                }
            }
            else if (strncmp(rxLine, "JSN:", 4) == 0) {
                ParseDetectionJSON(&rxLine[4], &currentDetection);
                PRINTF("\r\n--- [JSON STRUCTURED DATA] ---\r\n");
                PRINTF("Extracted Label    : %s\r\n", currentDetection.label);
                PRINTF("Extracted Distance : %d mm\r\n", currentDetection.distance);
                PRINTF("Extracted Danger   : %s\r\n", currentDetection.danger);
                PRINTF("------------------------------\r\n");
                Safe_Display_Update("Result:", currentDetection.label);
            }
            else if (strncmp(rxLine, "AUDIO:", 6) == 0) {
                uint32_t audioLen = (uint32_t)atoi(&rxLine[6]);

                // ---- INCREASED BUFFER CHECK ----
                if (audioLen > 0 && audioLen <= AUDIO_BUF_MAX) {
                    PRINTF("[AUDIO] Expecting %d bytes...\r\n", audioLen);
                    int received = UART_ReadBytes(ESP_UART, audioBuffer, audioLen, 15000);
                    PRINTF("[AUDIO] Received %d / %d bytes\r\n", received, audioLen);

                    if (received > 1000) {
                        Audio_Player_Play(audioBuffer, received);
                        while (Audio_Player_IsBusy()) {
                            vTaskDelay(pdMS_TO_TICKS(50));
                        }
                    }
                } else {
                    PRINTF("[AUDIO] ERROR: File size %d exceeds %d byte limit!\r\n", audioLen, AUDIO_BUF_MAX);
                    UART_DrainRx(ESP_UART); // Throw away the binary garbage to protect the OS
                }
            }
            // Optional: Catch garbage characters if things desync
            else if (rxLine[0] != '\0' && rxLine[0] != '\n' && rxLine[0] != '\r') {
                 // PRINTF("[DEBUG-RAW] Dropped unformatted line: %s\r\n", rxLine);
            }
        }
    }
}

/* TASK 3: Button Task */
static void Button_Task(void *pvParameters) {
    uint8_t last_state = 0;

    for (;;) {
        uint32_t pta1  = GPIO_PinRead(GPIOA, 1U);
        uint32_t ptc16 = GPIO_PinRead(GPIOC, 16U);
        uint8_t current_state = 0;

        if (pta1 && ptc16) {
            current_state = 3;
        } else if (ptc16) {
            current_state = 1;
        } else if (pta1) {
            current_state = 2;
        }

        if (current_state != last_state && current_state != 0) {
            if (current_state == 1) {
                vTaskSuspendAll();
                UART_WriteBlocking(ESP_UART, (uint8_t*)"ocr\n", 4);
                xTaskResumeAll();
                Safe_Display_Update("Current Mode:", "OCR Mode (Btn)");
                PRINTF("[MODE] Sent: ocr\r\n");
            } else if (current_state == 2) {
                vTaskSuspendAll();
                UART_WriteBlocking(ESP_UART, (uint8_t*)"obj\n", 4);
                xTaskResumeAll();
                Safe_Display_Update("Current Mode:", "Object (Btn)");
                PRINTF("[MODE] Sent: obj\r\n");
            } else if (current_state == 3) {
                vTaskSuspendAll();
                UART_WriteBlocking(ESP_UART, (uint8_t*)"oando\n", 6);
                xTaskResumeAll();
                Safe_Display_Update("Current Mode:", "OCR+Obj (Btn)");
                PRINTF("[MODE] Sent: oando\r\n");
            }
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void Vision_ToF_Init(void) {
    xTaskCreate(Motor_Task, "Motor_Task", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 3, NULL);
    xTaskCreate(Comm_Task, "Comm_Task", configMINIMAL_STACK_SIZE + 512, NULL, tskIDLE_PRIORITY + 2, NULL);
    xTaskCreate(Button_Task, "Button_Task", configMINIMAL_STACK_SIZE + 256, NULL, tskIDLE_PRIORITY + 2, NULL);
}
