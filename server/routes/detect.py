import os
from datetime import datetime
from flask import Blueprint, request, jsonify, make_response, send_from_directory
from config import SAVE_DIR, AUDIO_DIR, DETECTION_THRESHOLD, ALERT_THRESHOLD
from database import insert_detection, insert_alert
from models import run_yolo, run_ocr
from tts import generate_8bit_wav
from sse import broadcast_sse

detect_bp = Blueprint('detect', __name__)

@detect_bp.route('/images/<filename>')
def serve_image(filename):
    return send_from_directory(SAVE_DIR, filename)

@detect_bp.route('/audio/<filename>')
def serve_audio(filename):
    return send_from_directory(AUDIO_DIR, filename)


@detect_bp.route('/detect', methods=['POST'])
def detect_object():
    # ── 1. Validate distance ──────────────────────────────────────────────────
    distance_str = request.form.get('distance')
    if distance_str is None:
        return jsonify({"error": "No distance provided"}), 400
    try:
        distance = int(distance_str)
    except ValueError:
        return jsonify({"error": "Invalid distance value — must be an integer"}), 400

    timestamp_now = datetime.now().isoformat()
    broadcast_sse("distance", {"distance_mm": distance, "timestamp": timestamp_now})

    # ── 2. Auto-alert for dangerously close objects ───────────────────────────
    if 0 < distance < ALERT_THRESHOLD:
        alert_msg = f"DANGER: Object at {distance}mm!"
        alert_event = {"distance": distance, "message": alert_msg, "timestamp": timestamp_now}
        insert_alert(timestamp_now, distance, alert_msg)
        broadcast_sse("alert", alert_event)
        print(f"[🚨 AUTO-ALERT] Object at {distance}mm!")

    # ── 3. Skip full detection when object is too far ─────────────────────────
    if distance >= DETECTION_THRESHOLD:
        return jsonify({"result": "out_of_range", "distance": distance}), 200

    # ── 4. Validate uploaded image ────────────────────────────────────────────
    if 'file' not in request.files:
        return jsonify({"error": "No file uploaded"}), 400

    file = request.files['file']
    if not file or file.filename == '':
        return jsonify({"error": "Empty file"}), 400

    # ── 5. Read mode (default: obj) ───────────────────────────────────────────
    mode = request.form.get('mode', 'obj').strip().lower()
    if mode not in ('obj', 'ocr', 'oando'):
        return jsonify({"error": f"Invalid mode '{mode}'. Use obj, ocr, or oando"}), 400

    # ── 6. Save image ─────────────────────────────────────────────────────────
    safe_time = timestamp_now.replace(":", "-").replace(".", "-")
    image_filename = f"photo_{safe_time}.jpg"
    image_filepath = os.path.join(SAVE_DIR, image_filename)
    file.save(image_filepath)

    # ── 7. Run models based on mode ───────────────────────────────────────────
    detected_name, best_conf = "unknown", 0.0
    extracted_text = ""
    tts_text = ""

    if mode == 'obj':
        detected_name, best_conf = run_yolo(image_filepath)
        tts_text = detected_name if detected_name != "unknown" else ""
        print(f"\n[MODE: obj] {detected_name.upper()} ({best_conf:.2f}) | Dist: {distance}mm")

    elif mode == 'ocr':
        print(f"\n[MODE: ocr] Running OCR on image...")
        extracted_text = run_ocr(image_filepath)
        tts_text = extracted_text
        print(f"\n[MODE: ocr] Text: '{extracted_text}' | Dist: {distance}mm")

    elif mode == 'oando':
        detected_name, best_conf = run_yolo(image_filepath)
        extracted_text = run_ocr(image_filepath)
        parts = []
        if detected_name != "unknown":
            parts.append(detected_name)
        if extracted_text.strip():
            parts.append(extracted_text)
        tts_text = ". ".join(parts)
        print(f"\n[MODE: oando] {detected_name.upper()} ({best_conf:.2f}) | "
              f"Text: '{extracted_text}' | Dist: {distance}mm")

    # ── 8. Persist to database ────────────────────────────────────────────────
    insert_detection(timestamp_now, distance, detected_name, best_conf,
                     extracted_text, image_filename)

    # ── 9. Generate TTS from mode-specific text ───────────────────────────────
    audio_url = None
    audio_filepath = None
    if tts_text.strip():
        audio_filename = f"audio_{safe_time}.wav"
        audio_filepath = generate_8bit_wav(tts_text, audio_filename)
        if audio_filepath:
            audio_url = f"/audio/{audio_filename}"

    # ── 10. Broadcast detection event to dashboard ────────────────────────────
    # FIX: audio_url is always included in the SSE payload (was previously
    # only populated when ocr_text was non-empty, which silently dropped
    # audio for obj-mode detections on the dashboard).
    broadcast_sse("detection", {
        "result": detected_name,
        "confidence": round(best_conf, 2),
        "distance": distance,
        "ocr_text": extracted_text,
        "mode": mode,
        "image_url": f"/images/{image_filename}",
        "audio_url": audio_url,   # None serialises to JSON null — JS checks for this
        "timestamp": timestamp_now,
    })

    # ── 11. Respond to the microcontroller ────────────────────────────────────

    # Sanitize text for HTTP Headers to prevent ESP32 parsing crashes
    safe_ocr = extracted_text.replace('\n', ' ').replace('\r', '').strip()
    safe_ocr = safe_ocr.encode('ascii', 'ignore').decode('ascii')

    lcd_display_text = ""
    if mode == 'ocr':
        lcd_display_text = safe_ocr
    elif mode == 'obj':
        lcd_display_text = detected_name
    elif mode == 'oando':
        lcd_display_text = f"{detected_name} - {safe_ocr}"

    if not lcd_display_text.strip():
        lcd_display_text = "No result"

    # ── 12. Send Binary Audio + Headers to ESP32 ─────────────────────────────
    if audio_filepath and os.path.exists(audio_filepath):
        with open(audio_filepath, "rb") as f:
            wav_data = f.read()

        # NOTE FOR ESP32 FIRMWARE: The WAV file has a standard 44-byte RIFF header.
        # If your firmware reads raw PCM bytes from offset 0 it will play garbage.
        # Skip the first 44 bytes in your firmware's audio playback routine, e.g.:
        #   size_t data_start = 44;
        #   DAC_write(wav_buffer + data_start, wav_length - data_start);
        response = make_response(wav_data)
        response.headers["Content-Type"] = "audio/wav"
        response.headers["X-Result"] = lcd_display_text
        response.headers["X-Confidence"] = str(round(best_conf, 2))
        response.headers["X-Distance"] = str(distance)
        response.headers["X-Mode"] = mode
        response.headers["X-OCR-Text"] = safe_ocr
        response.headers["X-Audio-Length"] = str(len(wav_data))
        return response, 200

    # ── 13. Fallback: no audio generated ─────────────────────────────────────
    response = jsonify({
        "result": detected_name,
        "confidence": round(best_conf, 2),
        "distance": distance,
        "ocr_text": safe_ocr,
        "mode": mode,
    })
    response.headers["X-Result"] = lcd_display_text
    response.headers["X-Mode"] = mode
    response.headers["X-Audio-Length"] = "0"
    return response, 200