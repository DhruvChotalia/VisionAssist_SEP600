from flask import Blueprint, request, jsonify
from datetime import datetime
from database import insert_alert
from sse import broadcast_sse

alert_bp = Blueprint('alert', __name__)


@alert_bp.route('/alert', methods=['POST'])
def handle_alert():
    data = request.get_json(force=True)
    distance = data.get('distance', 0)
    message = data.get('message', 'Proximity alert')
    timestamp_now = datetime.now().isoformat()

    print(f"\n[🚨 ALERT] Distance: {distance}mm — {message}")

    insert_alert(timestamp_now, distance, message)

    alert_event = {
        "distance": distance,
        "message": message,
        "timestamp": timestamp_now,
    }
    broadcast_sse("alert", alert_event)

    return jsonify({"status": "alert_received", "distance": distance}), 200