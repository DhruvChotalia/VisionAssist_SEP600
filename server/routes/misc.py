import os
import glob
from flask import Blueprint, request, jsonify
from datetime import datetime
from database import count_detections
from sse import broadcast_sse
from config import SAVE_DIR, AUDIO_DIR
import requests

misc_bp = Blueprint('misc', __name__)


@misc_bp.route('/db_check')
def db_check():
    """Health check — verifies database is reachable and reports row count."""
    try:
        count = count_detections()
        return jsonify({"status": "healthy", "detections_count": count}), 200
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 500


@misc_bp.route('/log', methods=['POST'])
def handle_log():
    """Accept a plain-text log message from the microcontroller and relay it to the dashboard."""
    log_data = request.get_data(as_text=True)
    timestamp_now = datetime.now().isoformat()
    print(f"[LOG] {timestamp_now} | {log_data}")
    broadcast_sse("log", {"data": log_data, "timestamp": timestamp_now})
    return jsonify({"status": "logged"}), 200


@misc_bp.route('/clear_data', methods=['POST'])
def clear_data():
    """
    Delete all captured images and generated audio files from disk.
    Database records are kept (for audit trail) but image_file references
    will become stale — acceptable since the CSV export still shows metadata.

    Called by the dashboard 'Clear Media' button.
    """
    deleted_images = 0
    deleted_audio  = 0
    errors = []

    # Delete images
    for path in glob.glob(os.path.join(SAVE_DIR, "photo_*.jpg")):
        try:
            os.remove(path)
            deleted_images += 1
        except OSError as e:
            errors.append(str(e))

    # Delete audio
    for path in glob.glob(os.path.join(AUDIO_DIR, "audio_*.wav")):
        try:
            os.remove(path)
            deleted_audio += 1
        except OSError as e:
            errors.append(str(e))

    msg = f"Deleted {deleted_images} image(s) and {deleted_audio} audio file(s)."
    print(f"[CLEAR] {msg}")
    broadcast_sse("log", {"data": f"🗑️ {msg}", "timestamp": datetime.now().isoformat()})

    return jsonify({
        "status":         "ok",
        "deleted_images": deleted_images,
        "deleted_audio":  deleted_audio,
        "errors":         errors,
        "message":        msg,
    }), 200


@misc_bp.route('/api/trigger_device', methods=['POST'])
def trigger_device():
    """Bridge route: send a mode command from dashboard to ESP32 WebServer."""
    data    = request.json or {}
    esp_ip  = data.get('ip')
    mode    = data.get('mode')

    if not esp_ip or not mode:
        return jsonify({"error": "Missing ESP32 IP or Mode"}), 400

    try:
        url  = f"http://{esp_ip}/mode?val={mode}"
        resp = requests.get(url, timeout=5)
        if resp.status_code == 200:
            return jsonify({"success": True,
                            "message": f"Triggered {mode.upper()} on ESP32"})
        return jsonify({"error": f"ESP32 returned HTTP {resp.status_code}"}), 500
    except requests.exceptions.RequestException as e:
        return jsonify({"error": f"Could not reach ESP32: {e}"}), 500