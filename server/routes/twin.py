"""
twin.py — Digital Twin state management for VisionAssist
=========================================================

Two-way protocol between Flask server and ESP32:

  ESP32  →  POST /telemetry          (every ~500ms, reports live hardware state)
  ESP32  →  GET  /twin/state         (every ~500ms, polls for any pending overrides)
  Server →  (response to /twin/state) carries pending commands as JSON

Override fields sent back to ESP32 in /twin/state response:
  {
    "distance_override": 350,     // int mm, or null = use real ToF
    "lcd_override":      "hello", // string, or null = use natural detection result
    "mode_override":     "ocr",   // "obj"|"ocr"|"oando", or null = keep current
    "trigger_capture":   true     // one-shot: capture image now, then auto-clears
  }

All overrides are one-shot except distance_override and lcd_override which persist
until explicitly cleared (dashboard sends null).
"""

import threading
from datetime import datetime
from flask import Blueprint, request, jsonify
from sse import broadcast_sse

twin_bp = Blueprint('twin', __name__)

# ── Shared state (thread-safe) ────────────────────────────────────────────────
_lock = threading.Lock()

# Live hardware state (written by ESP32 via /telemetry)
_hw_state = {
    "distance_mm":  None,
    "vibration_on": False,
    "lcd_text":     "",
    "mode":         "obj",
    "object_name":  "",
    "confidence":   0.0,
    "last_seen":    None,   # ISO timestamp of last telemetry packet
}

# Pending override commands (written by dashboard, read + cleared by ESP32)
_overrides = {
    "distance_override": None,   # int mm or None
    "lcd_override":      None,   # string or None
    "mode_override":     None,   # "obj"|"ocr"|"oando" or None
    "trigger_capture":   False,  # one-shot flag
}


def get_hw_state() -> dict:
    with _lock:
        return dict(_hw_state)

def get_overrides() -> dict:
    with _lock:
        return dict(_overrides)


# ── /telemetry — ESP32 reports its live state ─────────────────────────────────
@twin_bp.route('/telemetry', methods=['POST'])
def telemetry():
    """
    ESP32 POSTs this every ~500 ms.

    Expected JSON body:
    {
        "distance_mm":  423,
        "vibration_on": true,
        "lcd_text":     "bottle",
        "mode":         "obj"
    }
    Optional fields (sent after a detection completes):
    {
        "object_name": "bottle",
        "confidence":  0.82
    }
    """
    data = request.get_json(force=True, silent=True) or {}
    now  = datetime.now().isoformat()

    with _lock:
        if "distance_mm"  in data: _hw_state["distance_mm"]  = data["distance_mm"]
        if "vibration_on" in data: _hw_state["vibration_on"] = bool(data["vibration_on"])
        if "lcd_text"     in data: _hw_state["lcd_text"]     = str(data["lcd_text"])
        if "mode"         in data: _hw_state["mode"]         = str(data["mode"])
        if "object_name"  in data: _hw_state["object_name"]  = str(data["object_name"])
        if "confidence"   in data: _hw_state["confidence"]   = float(data["confidence"])
        _hw_state["last_seen"] = now

    # Push live state to dashboard via SSE
    broadcast_sse("telemetry", {**_hw_state, "timestamp": now})

    return jsonify({"status": "ok"}), 200


# ── /twin/state — ESP32 polls this for pending commands ───────────────────────
@twin_bp.route('/twin/state', methods=['GET'])
def twin_state():
    """
    ESP32 GETs this every ~500 ms.
    Returns current overrides.  trigger_capture is auto-cleared after one delivery.

    ESP32 firmware logic:
        resp = GET /twin/state
        body = parse JSON
        if body["distance_override"] != null  →  use body["distance_override"] as distance
        if body["lcd_override"]      != null  →  write body["lcd_override"] to LCD
        if body["mode_override"]     != null  →  set mode = body["mode_override"]
        if body["trigger_capture"]   == true  →  take photo + POST /detect immediately
    """
    with _lock:
        payload = dict(_overrides)
        # Clear the one-shot trigger after delivering it once
        if _overrides["trigger_capture"]:
            _overrides["trigger_capture"] = False

    return jsonify(payload), 200


# ── /twin/override — dashboard sets overrides ─────────────────────────────────
@twin_bp.route('/twin/override', methods=['POST'])
def set_override():
    """
    Dashboard POSTs any combination of override fields.
    Send null to clear an override (resume real hardware value).

    Example — set distance override:
        POST /twin/override  {"distance_override": 350}
    Example — clear distance override:
        POST /twin/override  {"distance_override": null}
    Example — trigger capture:
        POST /twin/override  {"trigger_capture": true}
    Example — change mode:
        POST /twin/override  {"mode_override": "ocr"}
    Example — override LCD:
        POST /twin/override  {"lcd_override": "Testing 123"}
    """
    data = request.get_json(force=True, silent=True) or {}

    with _lock:
        if "distance_override" in data:
            v = data["distance_override"]
            _overrides["distance_override"] = int(v) if v is not None else None

        if "lcd_override" in data:
            v = data["lcd_override"]
            _overrides["lcd_override"] = str(v) if v is not None else None

        if "mode_override" in data:
            v = data["mode_override"]
            if v in ("obj", "ocr", "oando", None):
                _overrides["mode_override"] = v

        if "trigger_capture" in data:
            _overrides["trigger_capture"] = bool(data["trigger_capture"])

    broadcast_sse("overrides", get_overrides())
    return jsonify({"status": "ok", "overrides": get_overrides()}), 200


# ── /twin/hw_state — dashboard fetches current hardware state on page load ────
@twin_bp.route('/twin/hw_state', methods=['GET'])
def hw_state_snapshot():
    return jsonify(get_hw_state()), 200