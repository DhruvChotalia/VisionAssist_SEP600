import os
import subprocess
import tempfile
from pydub import AudioSegment
from config import AUDIO_DIR

# ---------------------------------------------------------------------------
# Offline TTS using espeak directly via subprocess.
#
# Why not pyttsx3?
#   pyttsx3 wraps espeak but initialises a COM/dbus event loop that crashes
#   on headless servers when called from a Flask worker thread.
#
# Why espeak directly?
#   - Works on any headless Linux server
#   - Writes a WAV file natively — no MP3/ffmpeg needed for the TTS step
#   - pydub only needs ffmpeg for the re-encode step (WAV→WAV is native)
#
# Install:  sudo apt-get install espeak
# ---------------------------------------------------------------------------


def generate_8bit_wav(text: str, filename: str) -> str | None:
    """
    Generate spoken audio using espeak and re-encode to K66F-compatible WAV:
      - 8000 Hz sample rate
      - Mono (1 channel)
      - 8-bit PCM unsigned
      - WAV container (44-byte RIFF header — skip in ESP32 firmware if reading raw PCM)

    Returns the final WAV path on success, or None on failure.
    """
    if not os.path.exists(AUDIO_DIR):
        os.makedirs(AUDIO_DIR)

    final_wav_path = os.path.join(AUDIO_DIR, filename)

    # espeak writes its native WAV to a temp file (16-bit, 22050 Hz by default)
    # We then let pydub re-encode it to the exact K66F spec.
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        temp_wav_path = tmp.name

    try:
        print(f"[TTS] Generating espeak audio for: '{text}'...")

        # ── 1. espeak → temp WAV (16-bit 22050 Hz, espeak default) ──────────
        result = subprocess.run(
            [
                "espeak",
                "-v", "en",          # English voice
                "-s", "130",         # Speed: 130 words/min (slightly slower = clearer)
                "-a", "180",         # Amplitude 0-200
                "-w", temp_wav_path, # Write to file
                text
            ],
            capture_output=True,
            timeout=30
        )

        if result.returncode != 0:
            print(f"[TTS ERROR] espeak failed (code {result.returncode}): "
                  f"{result.stderr.decode().strip()}")
            return None

        if not os.path.exists(temp_wav_path) or os.path.getsize(temp_wav_path) == 0:
            print(f"[TTS ERROR] espeak produced empty/missing file at {temp_wav_path}")
            return None

        # ── 2. Load espeak WAV into pydub ────────────────────────────────────
        audio = AudioSegment.from_wav(temp_wav_path)

        # ── 3. Re-encode to K66F DAC spec ────────────────────────────────────
        audio = audio.set_frame_rate(8000)  # 8 kHz
        audio = audio.set_channels(1)       # Mono
        audio = audio.set_sample_width(1)   # 8-bit (1 byte/sample, unsigned PCM)

        # ── 4. Export final WAV ───────────────────────────────────────────────
        audio.export(final_wav_path, format="wav")
        size = os.path.getsize(final_wav_path)
        print(f"[TTS] ✅ Saved: {final_wav_path}  ({size} bytes)")
        return final_wav_path

    except FileNotFoundError:
        print("[TTS ERROR] 'espeak' not found. Install with: sudo apt-get install espeak")
        return None
    except subprocess.TimeoutExpired:
        print("[TTS ERROR] espeak timed out after 30s")
        return None
    except Exception as e:
        print(f"[TTS ERROR] ❌ {e}")
        return None
    finally:
        if os.path.exists(temp_wav_path):
            os.remove(temp_wav_path)