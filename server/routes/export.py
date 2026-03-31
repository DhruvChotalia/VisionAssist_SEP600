import os
import csv
import zipfile
from io import StringIO, BytesIO
from flask import Blueprint, request, make_response, current_app
from config import SAVE_DIR
from database import fetch_all_detections

export_bp = Blueprint('export', __name__)


@export_bp.route('/export')
def export_csv():
    """Download all detections as a CSV file with clickable image URLs."""
    try:
        rows = fetch_all_detections()

        si = StringIO()
        cw = csv.writer(si)
        cw.writerow(['ID', 'Timestamp', 'Distance_mm', 'Object',
                     'Confidence', 'OCR_Text', 'Image_File', 'Image_URL'])

        base_url = f"http://{request.host}/images"
        for row in rows:
            row_list = list(row)
            image_file = row_list[6] or ""
            image_url = f"{base_url}/{image_file}" if image_file else ""
            row_list.append(image_url)
            cw.writerow(row_list)

        # Encode to bytes so Flask/Werkzeug handles it correctly on all versions
        csv_bytes = si.getvalue().encode("utf-8")
        response = make_response(csv_bytes)
        response.headers["Content-Disposition"] = \
            "attachment; filename=visionassist_history.csv"
        response.headers["Content-Type"] = "text/csv; charset=utf-8"
        response.headers["Content-Length"] = len(csv_bytes)
        return response

    except Exception as e:
        current_app.logger.error(f"CSV export error: {e}", exc_info=True)
        return f"CSV export failed: {e}", 500


@export_bp.route('/export_zip')
def export_zip():
    """Download all detections as a ZIP archive containing a CSV + captured images."""
    try:
        rows = fetch_all_detections()

        zip_buffer = BytesIO()
        with zipfile.ZipFile(zip_buffer, 'w', zipfile.ZIP_DEFLATED) as zf:
            # ── CSV inside the ZIP ───────────────────────────────────────────
            si = StringIO()
            cw = csv.writer(si)
            cw.writerow(['ID', 'Timestamp', 'Distance_mm', 'Object',
                         'Confidence', 'OCR_Text', 'Image_File'])
            cw.writerows(rows)
            zf.writestr("detections.csv", si.getvalue())

            # ── Images ───────────────────────────────────────────────────────
            for row in rows:
                image_file = row[6]
                if image_file:
                    img_path = os.path.join(SAVE_DIR, image_file)
                    if os.path.exists(img_path):
                        zf.write(img_path, f"images/{image_file}")

        # Must seek(0) BEFORE reading, then pass bytes directly to make_response
        zip_buffer.seek(0)
        zip_bytes = zip_buffer.read()

        response = make_response(zip_bytes)
        response.headers["Content-Disposition"] = \
            "attachment; filename=visionassist_export.zip"
        response.headers["Content-Type"] = "application/zip"
        response.headers["Content-Length"] = len(zip_bytes)
        # Prevent browsers and proxies from buffering/caching the download
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
        return response

    except Exception as e:
        current_app.logger.error(f"ZIP export error: {e}", exc_info=True)
        return f"ZIP export failed: {e}", 500