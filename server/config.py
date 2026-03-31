import os

SAVE_DIR = "received_images"
AUDIO_DIR = "generated_audio"
DB_FILE = "visionassist.db"

DETECTION_THRESHOLD = 800   # mm — beyond this, skip detection
ALERT_THRESHOLD = 200       # mm — below this, trigger danger alert
AUDIO_SAMPLE_RATE = 8000    # 8kHz for microcontroller playback