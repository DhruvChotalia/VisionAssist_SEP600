/**
 * VisionAssist — ESP32-CAM Firmware
 * ===================================
 * Architecture: Dual-core FreeRTOS
 * Core 0 → CameraTask   : capture + POST /detect + audio forward
 * Core 1 → loop()       : WebServer + Serial RX + Twin polling
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ═══════════════════════════════════════════════════════════════════════════
// CONFIGURATION
// ═══════════════════════════════════════════════════════════════════════════

const char* WIFI_SSID     = "krish";
const char* WIFI_PASSWORD = "12345678";
const char* SERVER_IP     = "10.250.203.108";
const int   SERVER_PORT   = 8080;

#define BAUD_RATE             115200
#define FLASH_GPIO_NUM        4
#define DETECTION_THRESHOLD   800
#define ALERT_THRESHOLD       200
#define CAPTURE_INTERVAL_MS   3000
#define DISTANCE_TIMEOUT_MS   2000
#define TELEMETRY_INTERVAL_MS  500
#define TWIN_POLL_INTERVAL_MS  500
#define WIFI_RETRY_INTERVAL_MS 5000

// Camera pins (AI-Thinker ESP32-CAM)
#define PWDN_GPIO_NUM   32
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM   26
#define SIOC_GPIO_NUM   27
#define Y9_GPIO_NUM     35
#define Y8_GPIO_NUM     34
#define Y7_GPIO_NUM     39
#define Y6_GPIO_NUM     36
#define Y5_GPIO_NUM     21
#define Y4_GPIO_NUM     19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM  25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM   22

// ═══════════════════════════════════════════════════════════════════════════
// SHARED STATE
// ═══════════════════════════════════════════════════════════════════════════

static SemaphoreHandle_t stateMutex;

volatile int      activeMode          = 1;         // 0=ocr 1=obj 2=oando
volatile uint16_t lastDistance        = 0;
volatile bool     isCapturing         = false;
volatile unsigned long lastDistTs     = 0;

volatile bool     distOverrideActive  = false;
volatile uint16_t distOverrideVal     = 0;
volatile bool     triggerCaptureFlag  = false;

static char  lcdOverrideBuf[33]   = "";
static char  lcdOverrideLast[33]  = "";
volatile bool lcdOverrideActive   = false;
volatile bool lcdOverrideSent     = false;

static String        lastObjectLabel  = "";
static bool          lastServerOnline = true;
static bool          wifiWasConnected = false;
static String        pendingWebCommand = "";
volatile bool        audioTransferActive = false;

static unsigned long lastTelemetryTs  = 0;
static unsigned long lastTwinPollTs   = 0;
static unsigned long lastWifiRetryTs  = 0;

static char serialRxBuf[128];
static int  serialRxIdx = 0;

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

String getModeString(int mode) {
    switch (mode) { case 0: return "ocr"; case 2: return "oando"; default: return "obj"; }
}
int modeFromString(const String& s) {
    if (s == "ocr") return 0; if (s == "oando") return 2; return 1;
}
String serverBase() {
    return String("http://") + SERVER_IP + ":" + SERVER_PORT;
}

// ═══════════════════════════════════════════════════════════════════════════
// CAMERA INIT
// ═══════════════════════════════════════════════════════════════════════════

bool initCamera() {
    camera_config_t cfg;
    cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer = LEDC_TIMER_0;
    cfg.pin_d0=Y2_GPIO_NUM; cfg.pin_d1=Y3_GPIO_NUM; cfg.pin_d2=Y4_GPIO_NUM;
    cfg.pin_d3=Y5_GPIO_NUM; cfg.pin_d4=Y6_GPIO_NUM; cfg.pin_d5=Y7_GPIO_NUM;
    cfg.pin_d6=Y8_GPIO_NUM; cfg.pin_d7=Y9_GPIO_NUM;
    cfg.pin_xclk=XCLK_GPIO_NUM; cfg.pin_pclk=PCLK_GPIO_NUM;
    cfg.pin_vsync=VSYNC_GPIO_NUM; cfg.pin_href=HREF_GPIO_NUM;
    cfg.pin_sccb_sda=SIOD_GPIO_NUM; cfg.pin_sccb_scl=SIOC_GPIO_NUM;
    cfg.pin_pwdn=PWDN_GPIO_NUM; cfg.pin_reset=RESET_GPIO_NUM;
    cfg.xclk_freq_hz=20000000; cfg.pixel_format=PIXFORMAT_JPEG;
    cfg.frame_size=FRAMESIZE_QVGA;
    if (psramFound()) {
        cfg.jpeg_quality=10; cfg.fb_count=2;
        cfg.grab_mode=CAMERA_GRAB_LATEST; cfg.fb_location=CAMERA_FB_IN_PSRAM;
    } else {
        cfg.jpeg_quality=12; cfg.fb_count=1;
        cfg.grab_mode=CAMERA_GRAB_WHEN_EMPTY; cfg.fb_location=CAMERA_FB_IN_DRAM;
    }
    if (esp_camera_init(&cfg) != ESP_OK) return false;
    esp_camera_sensor_get()->set_vflip(esp_camera_sensor_get(), 1);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// AUDIO FORWARD  (Core 0)
// ═══════════════════════════════════════════════════════════════════════════
// ═══════════════════════════════════════════════════════════════════════════
// AUDIO FORWARD  (Core 0)
// ═══════════════════════════════════════════════════════════════════════════

void forwardAudioToK66F(HTTPClient& http, int audioLen) {
    WiFiClient* stream = http.getStreamPtr();
    while (Serial.available()) Serial.read(); // Clear RX buffer
    audioTransferActive = true;

    // 1. Tell K66F we want to send audio
    Serial.printf("AUDIO_READY\n"); 
    Serial.flush();

    // 2. WAIT FOR THE ACKNOWLEDGE HANDSHAKE
    unsigned long waitStart = millis();
    bool gotAck = false;
    String rxStr = "";
    
    while (millis() - waitStart < 2000) { // 2 second timeout
        if (Serial.available()) {
            char c = Serial.read();
            if (c == '\n') {
                rxStr.trim();
                if (rxStr == "ACK_AUDIO") {
                    gotAck = true;
                    break;
                }
                rxStr = ""; // Clear string for next line
            } else if (c != '\r') {
                rxStr += c;
            }
        }
        vTaskDelay(1); // Yield to watchdog
    }

    if (!gotAck) {
        Serial.printf("ERR:Audio ACK Timeout\n");
        audioTransferActive = false;
        return; // Abort transmission, K66F isn't ready
    }

    // 3. Handshake successful, tell K66F the size and blast the data
    Serial.printf("AUDIO:%d\n", audioLen); 
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t buf[512];
    int totalSent = 0;
    unsigned long t0 = millis();

    while (totalSent < audioLen && (millis() - t0 < 15000)) {
        int avail = stream->available();
        if (avail > 0) {
            int n = stream->readBytes(buf, min(avail, min((int)sizeof(buf), audioLen - totalSent)));
            Serial.write(buf, n);
            totalSent += n;
        } else if (!stream->connected()) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    Serial.flush();
    audioTransferActive = false;
}
// ═══════════════════════════════════════════════════════════════════════════
// DETECT REQUEST  (Core 0)
// ═══════════════════════════════════════════════════════════════════════════

void sendPhoto(uint16_t distance, int mode) {
    camera_fb_t* stale = esp_camera_fb_get();
    if (stale) esp_camera_fb_return(stale);

    digitalWrite(FLASH_GPIO_NUM, HIGH);
    vTaskDelay(pdMS_TO_TICKS(250));
    camera_fb_t* fb = esp_camera_fb_get();
    digitalWrite(FLASH_GPIO_NUM, LOW);
    if (!fb) return;

    String ms = getModeString(mode);
    String dp = "--boundary\r\nContent-Disposition: form-data; name=\"distance\"\r\n\r\n" + String(distance) + "\r\n";
    String mp = "--boundary\r\nContent-Disposition: form-data; name=\"mode\"\r\n\r\n"     + ms               + "\r\n";
    String fp = "--boundary\r\nContent-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tl = "\r\n--boundary--\r\n";

    uint32_t len = dp.length() + mp.length() + fp.length() + fb->len + tl.length();
    uint8_t* body = (uint8_t*)malloc(len);
    if (!body) { esp_camera_fb_return(fb); return; }

    uint32_t off = 0;
    memcpy(body+off, dp.c_str(), dp.length()); off+=dp.length();
    memcpy(body+off, mp.c_str(), mp.length()); off+=mp.length();
    memcpy(body+off, fp.c_str(), fp.length()); off+=fp.length();
    memcpy(body+off, fb->buf,    fb->len);     off+=fb->len;
    memcpy(body+off, tl.c_str(), tl.length());
    esp_camera_fb_return(fb);

    HTTPClient http;
    http.begin(serverBase() + "/detect");
    http.setTimeout(10000);
    const char* hdrs[] = {"X-Result", "X-Audio-Length"};
    http.collectHeaders(hdrs, 2);
    http.addHeader("Content-Type", "multipart/form-data; boundary=boundary");

    int code = http.POST(body, len);
    free(body);

    if (code == 200) {
        if (!lastServerOnline) { Serial.printf("SYS:Server OK\n"); lastServerOnline = true; }

        String result   = http.header("X-Result");
        int    audioLen = http.header("X-Audio-Length").toInt();

        if (result.length() > 0) {
            lastObjectLabel = result;

            // Brief sync wait for any in-flight DIST: on Core 1
            unsigned long oldTs = lastDistTs, sw = millis();
            while (lastDistTs == oldTs && millis() - sw < 200) vTaskDelay(1);
            vTaskDelay(pdMS_TO_TICKS(5));

            Serial.printf("OBJ:%s\n", result.c_str());
            Serial.flush();

            if (audioLen > 44) {
                vTaskDelay(pdMS_TO_TICKS(150));
                forwardAudioToK66F(http, audioLen);
            }
        }
    } else {
        if (lastServerOnline) { Serial.printf("ERR:Srv Offline\n"); lastServerOnline = false; }
    }
    http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// ALERT + LOG  (Core 1)
// ═══════════════════════════════════════════════════════════════════════════

void sendAlert(uint16_t distance) {
    HTTPClient http;
    http.begin(serverBase() + "/alert"); http.setTimeout(4000);
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"type\":\"proximity_alert\",\"distance\":" + String(distance) + ",\"message\":\"Object dangerously close!\"}");
    http.end();
}
void sendLog(const String& msg) {
    HTTPClient http;
    http.begin(serverBase() + "/log"); http.setTimeout(4000);
    http.addHeader("Content-Type", "text/plain");
    http.POST(msg); http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// TELEMETRY  (Core 1)
// ═══════════════════════════════════════════════════════════════════════════

void postTelemetry() {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    uint16_t dist = distOverrideActive ? distOverrideVal : lastDistance;
    bool     vib  = (dist > 0 && dist < DETECTION_THRESHOLD);
    String   mode = getModeString(activeMode);
    char     snap[33]; strncpy(snap, lcdOverrideBuf, 32); snap[32]='\0';
    xSemaphoreGive(stateMutex);

    HTTPClient http;
    http.begin(serverBase() + "/telemetry"); http.setTimeout(3000);
    http.addHeader("Content-Type", "application/json");
    http.POST("{\"distance_mm\":"  + String(dist) +
              ",\"vibration_on\":" + (vib?"true":"false") +
              ",\"lcd_text\":\""   + String(snap) + "\"" +
              ",\"mode\":\""       + mode + "\"" +
              ",\"object_name\":\"" + lastObjectLabel + "\"}");
    http.end();
}

// ═══════════════════════════════════════════════════════════════════════════
// TWIN POLL  (Core 1)
// ═══════════════════════════════════════════════════════════════════════════

void pollTwinState() {
    HTTPClient http;
    http.begin(serverBase() + "/twin/state"); http.setTimeout(3000);
    int code = http.GET();
    if (code != 200) { http.end(); return; }
    String body = http.getString();
    http.end();

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok) return;

    xSemaphoreTake(stateMutex, portMAX_DELAY);

    // ── Distance override ──────────────────────────────────────────────────
    if (!doc["distance_override"].isNull()) {
        uint16_t newVal = (uint16_t)doc["distance_override"].as<int>();
        bool wasActive  = distOverrideActive;
        bool valChanged = (distOverrideVal != newVal);

        distOverrideActive = true;
        distOverrideVal    = newVal;

        if (!wasActive || valChanged) {
            isCapturing = (newVal < DETECTION_THRESHOLD);
            lastDistTs  = millis();   
            
            // Notify K66F of the override distance
            Serial.printf("OVR_DIST:%u\n", newVal);
            Serial.flush();
        }
    } else {
        if (distOverrideActive) {
            // Tell K66F the override is cleared
            Serial.printf("OVR_DIST_CLR\n");
            Serial.flush();
        }
        distOverrideActive = false;
    }

    // ── LCD override (Handles empty strings correctly) ─────────────────────
    String lcdTxt = "";
    bool hasLcdCmd = false;
    
    if (!doc["lcd_override"].isNull()) {
        lcdTxt = doc["lcd_override"].as<String>();
        if (lcdTxt.length() > 0 && lcdTxt != "null") {
            hasLcdCmd = true;
        }
    }

    if (hasLcdCmd) {
        bool textChanged = (strncmp(lcdOverrideBuf, lcdTxt.c_str(), 32) != 0);

        strncpy(lcdOverrideBuf, lcdTxt.c_str(), 32);
        lcdOverrideBuf[32] = '\0';
        lcdOverrideActive  = true;

        if (textChanged || !lcdOverrideSent) {
            Serial.printf("LCD:%s\n", lcdOverrideBuf);
            Serial.flush();
            strncpy(lcdOverrideLast, lcdOverrideBuf, 32);
            lcdOverrideLast[32] = '\0';
            lcdOverrideSent     = true;
        }

    } else {
        if (lcdOverrideActive) {
            // Just cleared — notify K66F once
            Serial.printf("LCD_CLEAR\n");
            Serial.flush();
        }
        lcdOverrideActive  = false;
        lcdOverrideSent    = false;    
        lcdOverrideBuf[0]  = '\0';
        lcdOverrideLast[0] = '\0';
    }

    // ── Mode override ──────────────────────────────────────────────────────
    if (!doc["mode_override"].isNull()) {
        int newInt = modeFromString(doc["mode_override"].as<String>());
        if (newInt != activeMode) {
            activeMode = newInt;
            Serial.printf("CMD:%s\n", getModeString(newInt).c_str());
            Serial.flush();
        }
    }

    // ── Capture trigger (one-shot) ─────────────────────────────────────────
    if (doc["trigger_capture"].as<bool>()) {
        triggerCaptureFlag = true;
    }

    xSemaphoreGive(stateMutex);
}

// ═══════════════════════════════════════════════════════════════════════════
// WEBSERVER  (Core 1)
// ═══════════════════════════════════════════════════════════════════════════

WebServer webServer(80);
void handleModeEndpoint() {
    if (!webServer.hasArg("val")) { webServer.send(400, "text/plain", "Missing ?val="); return; }
    pendingWebCommand = webServer.arg("val");
    webServer.send(200, "text/plain", "OK");
}

// ═══════════════════════════════════════════════════════════════════════════
// CAMERA TASK  (Core 0)
// ═══════════════════════════════════════════════════════════════════════════

void CameraTask(void* pvParameters) {
    unsigned long lastCaptureTs = 0;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

        bool doCapture = false; uint16_t capDist = 0; int capMode = 0;

        xSemaphoreTake(stateMutex, portMAX_DELAY);
        if (triggerCaptureFlag) {
            triggerCaptureFlag = false; doCapture = true;
            capDist = distOverrideActive ? distOverrideVal : lastDistance;
            capMode = activeMode;
        } else if (isCapturing && !audioTransferActive) {
            unsigned long now = millis();
            if (now - lastCaptureTs >= CAPTURE_INTERVAL_MS) {
                doCapture = true; lastCaptureTs = now;
                capDist = distOverrideActive ? distOverrideVal : lastDistance;
                capMode = activeMode;
            }
        }
        xSemaphoreGive(stateMutex);

        if (doCapture) sendPhoto(capDist, capMode);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(BAUD_RATE);
    delay(500);
    stateMutex = xSemaphoreCreateMutex();
    pinMode(FLASH_GPIO_NUM, OUTPUT);
    digitalWrite(FLASH_GPIO_NUM, LOW);

    if (!initCamera()) {
        Serial.printf("ERR:Cam Init Fail\n");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("SYS:WiFi Connecting\n");
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis()-t0 < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED) { wifiWasConnected=true; Serial.printf("SYS:WiFi Connected\n"); }

    webServer.on("/mode", HTTP_GET, handleModeEndpoint);
    webServer.begin();
    xTaskCreatePinnedToCore(CameraTask, "CameraTask", 12288, NULL, 1, NULL, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// MAIN LOOP  (Core 1)
// ═══════════════════════════════════════════════════════════════════════════

void loop() {
    unsigned long now = millis();
    webServer.handleClient();

    bool wifiNow = (WiFi.status() == WL_CONNECTED);
    if (wifiNow != wifiWasConnected) {
        wifiWasConnected = wifiNow;
        Serial.printf(wifiNow ? "SYS:WiFi Connected\n" : "ERR:WiFi Lost\n");
    }
    if (!wifiNow && now - lastWifiRetryTs > WIFI_RETRY_INTERVAL_MS) {
        lastWifiRetryTs = now; WiFi.reconnect();
    }

    if (pendingWebCommand.length() > 0) {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        activeMode = modeFromString(pendingWebCommand);
        xSemaphoreGive(stateMutex);
        Serial.printf("CMD:%s\n", pendingWebCommand.c_str());
        Serial.flush();
        pendingWebCommand = "";
    }

    if (!audioTransferActive) {
        while (Serial.available() > 0) {
            char c = Serial.read();
            if (c == '\n') {
                serialRxBuf[serialRxIdx] = '\0'; serialRxIdx = 0;
                String line = String(serialRxBuf); line.trim();
                if (line.length() == 0) continue;

                if (line == "obj" || line == "ocr" || line == "oando") {
                    xSemaphoreTake(stateMutex, portMAX_DELAY);
                    activeMode = modeFromString(line);
                    xSemaphoreGive(stateMutex);
                } else if (line.startsWith("DIST:")) {
                    uint16_t dist = (uint16_t)line.substring(5).toInt();
                    if (dist > 0) {
                        xSemaphoreTake(stateMutex, portMAX_DELAY);
                        // Do not overwrite override with physical distance
                        if (!distOverrideActive) lastDistance = dist;
                        lastDistTs  = millis();
                        isCapturing = (dist < DETECTION_THRESHOLD);
                        xSemaphoreGive(stateMutex);
                    }
                } else if (line.startsWith("ALERT:")) {
                    uint16_t dist = (uint16_t)line.substring(6).toInt();
                    if (wifiNow) sendAlert(dist);
                } else if (line.startsWith("LOG:")) {
                    if (wifiNow) sendLog(line);
                }
            } else if (c != '\r' && serialRxIdx < (int)sizeof(serialRxBuf)-1) {
                serialRxBuf[serialRxIdx++] = c;
            }
        }
    }

    xSemaphoreTake(stateMutex, portMAX_DELAY);
    if (isCapturing && !distOverrideActive && (millis()-lastDistTs > DISTANCE_TIMEOUT_MS)) {
        isCapturing = false; lastDistance = 0;
    }
    xSemaphoreGive(stateMutex);

    if (wifiNow && now-lastTelemetryTs >= TELEMETRY_INTERVAL_MS) { lastTelemetryTs=now; postTelemetry(); }
    if (wifiNow && now-lastTwinPollTs  >= TWIN_POLL_INTERVAL_MS)  { lastTwinPollTs=now;  pollTwinState(); }

    delay(5);
}