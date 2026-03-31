import os
from flask import Flask
from config import SAVE_DIR, AUDIO_DIR
from database import init_db
from models import load_models
from routes.detect    import detect_bp
from routes.alert     import alert_bp
from routes.export    import export_bp
from routes.stream    import stream_bp
from routes.dashboard import dashboard_bp
from routes.misc      import misc_bp
from routes.twin      import twin_bp          # ← NEW

app = Flask(__name__)
os.makedirs(SAVE_DIR,  exist_ok=True)
os.makedirs(AUDIO_DIR, exist_ok=True)

init_db()
load_models()

app.register_blueprint(detect_bp)
app.register_blueprint(alert_bp)
app.register_blueprint(export_bp)
app.register_blueprint(stream_bp)
app.register_blueprint(dashboard_bp)
app.register_blueprint(misc_bp)
app.register_blueprint(twin_bp)               # ← NEW

if __name__ == '__main__':
    print("=" * 60)
    print("   VisionAssist Server  (YOLO + EasyOCR + espeak TTS)")
    print("   Dashboard : http://localhost:8080/dashboard")
    print("=" * 60)
    app.run(host='0.0.0.0', port=8080, debug=True, threaded=True)