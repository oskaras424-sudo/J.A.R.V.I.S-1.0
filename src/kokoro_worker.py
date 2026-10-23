"""One-shot local Kokoro speech worker called by the JARVIS Qt application."""

from __future__ import annotations

import json
import sys
import wave
from pathlib import Path


def main() -> int:
    try:
        request = json.load(sys.stdin)
        from kokoro_onnx import Kokoro
        import numpy as np

        model_path = Path(request["model"])
        voices_path = Path(request["voices"])
        output_path = Path(request["output"])
        text = str(request["text"]).strip()
        voice = str(request.get("voice", "bm_george"))
        test_voices = request.get("test_voices")
        voices_to_test = [str(item) for item in test_voices] if isinstance(test_voices, list) else [voice]
        speed = min(1.3, max(0.7, float(request.get("speed", 1.0))))
        volume = min(100, max(0, int(request.get("volume", 85)))) / 100.0

        if not model_path.is_file() or not voices_path.is_file():
            raise FileNotFoundError("The Kokoro model or voice bank is missing.")
        if not text:
            raise ValueError("No text was provided for speech generation.")

        engine = Kokoro(str(model_path), str(voices_path))
        available_voices = []
        voice_errors = []
        selected_audio = None
        sample_rate = 0
        try:
            for voice_id in voices_to_test:
                try:
                    audio, rate = engine.create(
                        text,
                        voice=voice_id,
                        speed=speed,
                        lang="en-gb",
                    )
                    samples = np.asarray(audio, dtype=np.float32)
                    if samples.ndim != 1 or samples.size == 0 or int(rate) <= 0:
                        raise ValueError("Kokoro returned an empty or unsupported audio shape.")
                    available_voices.append(voice_id)
                    if selected_audio is None or voice_id == voice:
                        selected_audio = samples.copy()
                        sample_rate = int(rate)
                except Exception as error:
                    voice_errors.append(f"{voice_id}: {type(error).__name__}: {error}")
        finally:
            voices = getattr(engine, "voices", None)
            close = getattr(voices, "close", None)
            if callable(close):
                close()

        if not available_voices or selected_audio is None:
            raise RuntimeError("No configured British voice could synthesize the validation sentence. "
                               + "; ".join(voice_errors))
        pcm = (np.clip(selected_audio * volume, -1.0, 1.0) * 32767.0).astype("<i2")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with wave.open(str(output_path), "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            wav_file.writeframes(pcm.tobytes())

        print(json.dumps({"ok": True, "output": str(output_path),
                          "available_voices": available_voices,
                          "voice_errors": voice_errors}, ensure_ascii=False))
        return 0
    except Exception as error:  # Report a useful message to the Qt UI.
        print(f"Kokoro speech generation failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
