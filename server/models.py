from ultralytics import YOLO
import easyocr
import cv2  # <-- ADDED: OpenCV for safe image processing

yolo_model = None
ocr_reader = None


def load_models():
    global yolo_model, ocr_reader

    print("[INFO] Loading YOLOv10-X model...")
    yolo_model = YOLO("yolov10x.pt")
    print("[INFO] YOLO loaded successfully!")

    print("[INFO] Loading EasyOCR model...")
    ocr_reader = easyocr.Reader(['en'])
    print("[INFO] EasyOCR loaded successfully!")


def run_yolo(filepath: str) -> tuple[str, float]:
    """
    Run YOLO on an image file.
    Returns (object_name, confidence) for the highest-confidence detection.
    Returns ("unknown", 0.0) when nothing is detected.
    """
    if yolo_model is None:
        raise RuntimeError("YOLO model is not loaded. Call load_models() first.")

    results = yolo_model(filepath, verbose=False)
    detected_name, best_conf = "unknown", 0.0
    for r in results:
        for box in r.boxes:
            conf = float(box.conf[0])
            if conf > best_conf:
                best_conf = conf
                detected_name = yolo_model.names[int(box.cls[0])]
    return detected_name, best_conf


def run_ocr(filepath: str) -> str:
    """
    Run EasyOCR on an image file safely.
    Converts the image to grayscale to prevent unpacking bugs on corrupted/blurry images.
    Returns all detected text joined into a single string.
    """
    if ocr_reader is None:
        raise RuntimeError("OCR reader is not loaded. Call load_models() first.")

    try:
        # 1. Read the image explicitly using OpenCV
        img = cv2.imread(filepath)
        if img is None:
            print(f"[OCR WARNING] Could not read image at {filepath}")
            return ""
            
        # 2. Force the image into 2D Grayscale to prevent the EasyOCR unpacking bug
        gray_img = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
        
        # 3. Pass the clean, 2D grayscale array directly to EasyOCR
        ocr_results = ocr_reader.readtext(gray_img)
        
        # 4. Extract and join the text
        return " ".join(text for _, text, _ in ocr_results).strip()
        
    except Exception as e:
        # Catch crashes from completely corrupted or heavily compressed images
        print(f"\n[OCR WARNING] EasyOCR could not process this image: {e}")
        return ""