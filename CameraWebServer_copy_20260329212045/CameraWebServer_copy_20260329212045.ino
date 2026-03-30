#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h> 

const char* WIFI_SSID     = "Mi 11X";
const char* WIFI_PASSWORD = "dhruv.chot";
const char* SERVER_IP     = "10.133.149.112";
const int   SERVER_PORT   = 8090;

#define DETECTION_THRESHOLD 800
#define FLASH_GPIO_NUM 4

bool audioTransferActive = false;
unsigned long lastCaptureTime = 0;
unsigned long lastDistanceTime = 0;
const unsigned long captureInterval = 3000;
const unsigned long distanceTimeout = 2000; 
uint16_t lastDistance = 0;

String currentMode = "obj";
bool isCapturing = false;
String pendingWebCommand = "";

#define DEBUG_PRINT(msg)    Serial.println(msg)
#define DEBUG_PRINTF(...)   Serial.printf(__VA_ARGS__)

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80); 

void sendPhoto(uint16_t distance, String mode);

bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk  = XCLK_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn  = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_QVGA; 
        config.jpeg_quality = 10;
        config.fb_count     = 2;
        config.grab_mode    = CAMERA_GRAB_LATEST;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA; 
        config.jpeg_quality = 12;
        config.fb_count     = 1;
        config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) return false;
    sensor_t* s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    return true;
}

void handleDashboardCommand() {
    if (server.hasArg("val")) {
        pendingWebCommand = server.arg("val");
        server.send(200, "text/plain", "Mode switched on ESP32");
    } else {
        server.send(400, "text/plain", "Missing mode value");
    }
}

void setup() {
    Serial.begin(460800);
    delay(2000);
    pinMode(FLASH_GPIO_NUM, OUTPUT);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    DEBUG_PRINT("\n[ESP32] Booting VisionAssist v4.0...");
    if (!initCamera()) DEBUG_PRINT("[ESP32] Camera init FAILED!");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    DEBUG_PRINT("\n====================================");
    DEBUG_PRINT("[ESP32] WiFi Connected successfully!");
    DEBUG_PRINT("[ESP32] ---> YOUR LOCAL IP ADDRESS IS: ");
    DEBUG_PRINT(WiFi.localIP().toString());
    DEBUG_PRINT("====================================\n");

    server.on("/mode", HTTP_GET, handleDashboardCommand);
    server.begin();
}

void sendAlert(uint16_t distance) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(String("http://") + SERVER_IP + ":" + SERVER_PORT + "/alert");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    String payload = "{\"type\":\"proximity_alert\",\"distance\":" + String(distance) + ",\"message\":\"Object dangerously close!\"}";
    http.POST(payload);
    http.end();
}

void sendLog(String logData) {
    if (WiFi.status() != WL_CONNECTED) return;
    HTTPClient http;
    http.begin(String("http://") + SERVER_IP + ":" + SERVER_PORT + "/log");
    http.addHeader("Content-Type", "text/plain");
    http.setTimeout(5000);
    http.POST(logData);
    http.end();
}

void forwardAudioToK66F(HTTPClient &http, int audioLen) {
    WiFiClient* stream = http.getStreamPtr();
    while (Serial.available()) Serial.read(); 
    audioTransferActive = true;
    Serial.printf("AUDIO:%d\n", audioLen);
    Serial.flush();
    delay(200);   
    
    uint8_t buf[256];
    int totalSent = 0;
    unsigned long startTime = millis();
    while (totalSent < audioLen && (millis() - startTime < 15000)) {
        int available = stream->available();
        if (available > 0) {
            int toRead = min(available, min((int)sizeof(buf), audioLen - totalSent));
            int bytesRead = stream->readBytes(buf, toRead);
            Serial.write(buf, bytesRead);
            Serial.flush();
            totalSent += bytesRead;
        }
        delay(1);
    }
    audioTransferActive = false;
}

void sendPhoto(uint16_t distance, String mode) {
    if (WiFi.status() != WL_CONNECTED) {
        DEBUG_PRINT("[ESP32] WiFi not connected — skipping capture");
        return;
    }

    digitalWrite(FLASH_GPIO_NUM, HIGH);
    delay(300); 
    camera_fb_t* stale_fb = esp_camera_fb_get();
    if (stale_fb) esp_camera_fb_return(stale_fb);
    
    delay(50);
    camera_fb_t* fb = esp_camera_fb_get();
    digitalWrite(FLASH_GPIO_NUM, LOW);

    if (!fb) {
        DEBUG_PRINT("[ESP32] Camera capture failed!");
        return;
    }
    
    DEBUG_PRINTF("[CAM] Captured %d bytes, mode=%s\n", fb->len, mode.c_str());

    HTTPClient http;
    http.begin(String("http://") + SERVER_IP + ":" + SERVER_PORT + "/detect");
    const char* headerKeys[] = {"X-Audio-Length", "X-Result"};
    http.collectHeaders(headerKeys, 2);
    http.addHeader("Content-Type", "multipart/form-data; boundary=boundary");

    String distancePart = "--boundary\r\nContent-Disposition: form-data; name=\"distance\"\r\n\r\n" + String(distance) + "\r\n";
    String modePart = "--boundary\r\nContent-Disposition: form-data; name=\"mode\"\r\n\r\n" + mode + "\r\n";
    String filePart = "--boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--boundary--\r\n";

    uint32_t totalLen = distancePart.length() + modePart.length() + filePart.length() + fb->len + tail.length();
    uint8_t* body = (uint8_t*)malloc(totalLen);
    if (!body) {
        esp_camera_fb_return(fb);
        http.end();
        return;
    }

    uint32_t offset = 0;
    memcpy(body + offset, distancePart.c_str(), distancePart.length()); offset += distancePart.length();
    memcpy(body + offset, modePart.c_str(),     modePart.length());     offset += modePart.length();
    memcpy(body + offset, filePart.c_str(),     filePart.length());     offset += filePart.length();
    memcpy(body + offset, fb->buf,              fb->len);               offset += fb->len;
    memcpy(body + offset, tail.c_str(),         tail.length());

    int httpCode = http.POST(body, totalLen);
    free(body);
    esp_camera_fb_return(fb);

    if (httpCode == 200) {
        String audioLenStr = http.header("X-Audio-Length");
        String result      = http.header("X-Result");
        int audioLen       = audioLenStr.toInt();

        if (result.length() > 0) {
            String dangerLevel = "Safe";
            if (distance > 0 && distance <= 400) {
                dangerLevel = "High";
            } else if (distance > 400 && distance <= 800) {
                dangerLevel = "Medium";
            }

            result.replace("\"", "'"); 
            String jsonPayload = "JSN:{\"label\":\"" + result + "\",\"distance\":" + String(distance) + ",\"danger\":\"" + dangerLevel + "\"}";
            Serial.printf("%s\n", jsonPayload.c_str());
            
            // -------------------------------------------------------------
            // INCREASED DELAY: Give the K66F LCD plenty of time to finish!
            // -------------------------------------------------------------
            Serial.flush(); 
            delay(800); 
        }

        // -------------------------------------------------------------
        // SAFETY CHECK: A real WAV file is ALWAYS bigger than 44 bytes!
        // -------------------------------------------------------------
        if (audioLen > 44) {
            forwardAudioToK66F(http, audioLen);
        } else if (audioLen > 0) {
            DEBUG_PRINTF("[ESP32] Dropping invalid audio payload (Size: %d bytes)\n", audioLen);
        }
    } 
    http.end();
}

void loop() {
    server.handleClient();

    if (pendingWebCommand != "") {
        DEBUG_PRINTF("[DASHBOARD] Mode switched to: %s\n", pendingWebCommand.c_str());
        Serial.printf("CMD:%s\n", pendingWebCommand.c_str());
        currentMode = pendingWebCommand;
        pendingWebCommand = ""; 
    }

    if (!audioTransferActive && Serial.available() > 0) {
        String received = Serial.readStringUntil('\n');
        received.trim();

        if (received.startsWith("AUDIO:")) return;

        if (received == "ocr" || received == "obj" || received == "oando") {
            currentMode = received;
            DEBUG_PRINTF("[K66F] Mode switched to: %s\n", currentMode.c_str());
            return;
        }

        if (received.startsWith("ALERT:")) {
            String distStr = received.substring(6);
            uint16_t distance = (uint16_t)distStr.toInt();
            sendAlert(distance);
            lastDistance = distance;
            lastDistanceTime = millis();
            isCapturing = true;
            return;
        }

        if (received.startsWith("DIST:")) {
            uint16_t distance = (uint16_t)received.substring(5).toInt();
            if (distance > 0) {
                lastDistance = distance;
                lastDistanceTime = millis();
                if (distance < DETECTION_THRESHOLD) isCapturing = true;
            }
            return;
        }

        if (received.startsWith("LOG:")) {
            sendLog(received);
            return;
        }

        if (received.length() > 0) {
            uint16_t distance = (uint16_t)received.toInt();
            if (distance > 0 && distance < DETECTION_THRESHOLD) {
                lastDistance = distance;
                lastDistanceTime = millis();
                isCapturing = true;
            }
        }
    }

    if (isCapturing && (millis() - lastDistanceTime > distanceTimeout)) {
        isCapturing = false;
        lastDistance = 0;
    }

    if (isCapturing && !audioTransferActive) {
        if (millis() - lastCaptureTime >= captureInterval) {
            lastCaptureTime = millis();
            DEBUG_PRINTF("[CAM] Auto-capture, distance: %d mm, mode: %s\n", lastDistance, currentMode.c_str());
            sendPhoto(lastDistance, currentMode);
        }
    }

    delay(10);
}