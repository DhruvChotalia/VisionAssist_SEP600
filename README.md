# VisionAssist: Smart Glasses for the Visually Impaired 👓

**Course:** SEP600 – Winter 2026 (Seneca Polytechnic)  
**Team Members:** Dhruv Chotalia, Navdisha Bhakri, Krish Patel  

## 📖 About the Project
VisionAssist is a low-cost (~$50) wearable embedded system designed to assist visually impaired individuals with safe navigation and environmental awareness. The system unifies laser-precision distance sensing, open-vocabulary AI object/text recognition, and multi-modal feedback (haptic vibration and audio announcements) into a single assistive device.

This repository contains the complete source code for all three subsystems: the central microcontroller firmware, the IoT camera firmware, and the local AI processing server.

## 🏗️ System Architecture & Repository Structure

The project is divided into three distinct segments, each residing in its own directory:

### 1. `K66F_Firmware/` (NXP FRDM-K66F)
The central orchestrator running **FreeRTOS** (compiled via MCUXpresso). 
* **Sensors & Actuators:** Interfaces with the VL53L0X Time-of-Flight sensor (I2C), SSD1306 OLED Display (I2C), Vibration Motor (PWM), and Mode Select Buttons (GPIO).
* **Audio Playback:** Receives raw 8-bit PCM audio streams over UART and plays them through the onboard DAC0 using an FTM0 timer interrupt to a PAM8403 amplifier and micro-speaker.
* **Communication:** Uses resilient `strstr()` parsing to communicate with the ESP32 over UART at 115200 baud.

### 2. `ESP32_Firmware/` (ESP32-CAM)
The edge-communication and vision module (compiled via Arduino IDE).
* **Vision:** Captures JPEG images upon receiving a UART trigger from the K66F.
* **Networking:** Connects to the local WiFi network and uploads images to the Python server via HTTP POST.
* **Data Relay:** Downloads JSON labels and binary WAV audio files from the server, streaming them down the UART wire to the K66F.

### 3. `Python_Server/` (Flask AI Server)
The local backend processing hub and Digital Twin dashboard.
* **AI Processing:** Runs YOLOv10-X for object detection and EasyOCR for text extraction. 
* **Offline Audio:** Uses `pyttsx3` and `pydub` to generate offline, 8-bit, 8000Hz mono `.wav` files tailored specifically for the K66F's DAC.
* **Digital Twin:** Serves an HTML/JS dashboard that receives live telemetry (distance, alerts, camera feed, and system logs) via Server-Sent Events (SSE).

---

## 🚀 Installation & Setup

### Prerequisites
* **NXP FRDM-K66F:** MCUXpresso IDE with FreeRTOS SDK installed.
* **ESP32-CAM:** Arduino IDE with ESP32 board manager installed.
* **Server:** Python 3.10+.


---

## ⚠️ Academic Integrity & GenAI Usage Declaration

In accordance with the Seneca Polytechnic Academic Integrity and GenAI usage policies for SEP600, the following declarations are made regarding the use of Generative AI tools as learning and development aids:

* **Anthropic Claude 3.5 Sonnet** was utilized to assist in porting the vendor-specific API for the Time-of-Flight sensor. Specifically, Claude helped generate and structure the custom I2C Hardware Abstraction Layer (`i2c_hal.c` and `i2c_hal.h`) and integrate the `vl53l0x_api_*.c/h` files into the MCUXpresso environment.
* **Anthropic Claude 3.5 Sonnet** was also consulted to assist with the architecture of the DAC-based audio playback system, helping to generate the FTM0 timer interrupt logic found in `audio_player.c` and `audio_player.h`.
* **Google Gemini 1.5 Pro** was used to debug UART buffer overflows between the K66F and ESP32, resulting in the resilient `strstr()` parsing logic implemented in the `Comm_Task` of `vision_tof.c`, and the `lastCaptureTime` instant-photo override in the ESP32 Arduino code.
* **Google Gemini 1.5 Pro** was used to engineer the offline 8-bit WAV formatting logic in the Python server (`tts.py`) to bypass FFmpeg dependencies using the `pydub` library.

*All AI-assisted code was thoroughly reviewed, tested, modified, and validated by the team members to ensure functional understanding and architectural integration.*

---
*Developed for SEP600 (Winter 2026) - School of Software Design & Data Science, Seneca Polytechnic.*
