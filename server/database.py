import sqlite3
from config import DB_FILE


def init_db():
    conn = sqlite3.connect(DB_FILE)
    c = conn.cursor()
    c.execute('''
        CREATE TABLE IF NOT EXISTS detections (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT,
            distance_mm INTEGER,
            object_name TEXT,
            confidence  REAL,
            ocr_text    TEXT,
            image_file  TEXT
        )
    ''')
    c.execute('''
        CREATE TABLE IF NOT EXISTS alerts (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp   TEXT,
            distance_mm INTEGER,
            message     TEXT
        )
    ''')
    conn.commit()
    conn.close()


def get_connection():
    """Return a new SQLite connection. Caller is responsible for closing it."""
    return sqlite3.connect(DB_FILE)


def insert_detection(timestamp, distance, object_name, confidence, ocr_text, image_file):
    conn = get_connection()
    c = conn.cursor()
    c.execute(
        '''INSERT INTO detections
           (timestamp, distance_mm, object_name, confidence, ocr_text, image_file)
           VALUES (?, ?, ?, ?, ?, ?)''',
        (timestamp, distance, object_name, confidence, ocr_text, image_file)
    )
    conn.commit()
    conn.close()


def insert_alert(timestamp, distance, message):
    conn = get_connection()
    c = conn.cursor()
    c.execute(
        "INSERT INTO alerts (timestamp, distance_mm, message) VALUES (?, ?, ?)",
        (timestamp, distance, message)
    )
    conn.commit()
    conn.close()


def fetch_all_detections():
    conn = get_connection()
    c = conn.cursor()
    c.execute("SELECT * FROM detections ORDER BY timestamp DESC")
    rows = c.fetchall()
    conn.close()
    return rows


def count_detections():
    conn = get_connection()
    c = conn.cursor()
    c.execute("SELECT count(*) FROM detections")
    count = c.fetchone()[0]
    conn.close()
    return count