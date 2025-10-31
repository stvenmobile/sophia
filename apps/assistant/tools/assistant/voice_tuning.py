#!/usr/bin/env python3
"""
voice_tuning.py — dedicated speech tuning helper for Sophia.

The goal of this module is to explore text-to-speech (TTS) and speech-to-text (STT)
parameters without involving the full dialogue stack. Once you are happy with the
settings, run `python voice_tuning.py snapshot` (or pass `--print-config` to any
command) to copy the values into `assistant.py` or your deployment environment.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
import textwrap
import wave
from typing import Dict, Tuple

import sounddevice as sd
from faster_whisper import WhisperModel

# =============================================================================
# Tunable parameters (copy/paste these into assistant.py when finalized)
# =============================================================================
PIPER_CONTAINER = os.getenv("PIPER_CONTAINER", "piper-tts")
PIPER_MODEL_TEMPLATE = os.getenv(
    "VOICE_TUNING_MODEL_TEMPLATE", "/opt/voices/en_US-{voice}-medium.onnx"
)
PIPER_CONFIG_TEMPLATE = os.getenv(
    "VOICE_TUNING_CONFIG_TEMPLATE", "/opt/voices/en_US-{voice}-medium.onnx.json"
)

TTS_DEFAULTS: Dict[str, float | str] = {
    "voice": os.getenv("VOICE_TUNING_DEFAULT_VOICE", "amy"),
    "length_scale": float(os.getenv("VOICE_TUNING_LENGTH_SCALE", "1.0")),
    "noise_scale": float(os.getenv("VOICE_TUNING_NOISE_SCALE", "0.667")),
    "noise_w": float(os.getenv("VOICE_TUNING_NOISE_W", "0.8")),
}

ASR_DEFAULTS: Dict[str, float | str | int] = {
    "model": os.getenv("VOICE_TUNING_MODEL", "small.en"),
    "device": os.getenv("VOICE_TUNING_DEVICE", "auto"),
    "compute_type": os.getenv("VOICE_TUNING_COMPUTE", "float16"),
    "beam_size": int(os.getenv("VOICE_TUNING_BEAM_SIZE", "1")),
    "sample_rate": int(os.getenv("VOICE_TUNING_SAMPLE_RATE", "16000")),
    "record_seconds": float(os.getenv("VOICE_TUNING_RECORD_SECONDS", "5.0")),
}

# =============================================================================
# Support utilities
# =============================================================================
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
ASSETS_DIR = os.path.join(CURRENT_DIR, "assets")
BEEP_START = os.path.join(ASSETS_DIR, "bip.wav")
BEEP_END = os.path.join(ASSETS_DIR, "bip2.wav")
ESPEAK_DATA_DIR = "/usr/share/espeak-ng-data"

_MODEL_CACHE: Dict[Tuple[str, str, str], WhisperModel] = {}


def play_sound(path: str) -> None:
    if not os.path.isfile(path):
        return
    try:
        subprocess.run(
            ["aplay", path],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        pass


def record_audio(
    *,
    seconds: float,
    sample_rate: int,
    save_path: str | None = None,
    beeps: bool = True,
) -> str:
    """Record audio from the default microphone and write a WAV file."""
    if beeps:
        play_sound(BEEP_START)
    frames = int(seconds * sample_rate)
    print(f"[rec] recording {seconds:.1f}s @ {sample_rate} Hz …")
    audio = sd.rec(frames, samplerate=sample_rate, channels=1, dtype="int16")
    sd.wait()

    if save_path is None:
        tmp = tempfile.NamedTemporaryFile(suffix=".wav", delete=False)
        save_path = tmp.name
        tmp.close()

    with wave.open(save_path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)  # int16
        wf.setframerate(sample_rate)
        wf.writeframes(audio.tobytes())

    if beeps:
        play_sound(BEEP_END)
    print(f"[rec] saved to {save_path}")
    return save_path


def build_whisper_model(name: str, device_pref: str, compute_type: str) -> WhisperModel:
    if device_pref == "cpu":
        return WhisperModel(
            name,
            device="cpu",
            compute_type="int8" if compute_type == "float16" else compute_type,
        )

    if device_pref == "cuda":
        try:
            return WhisperModel(name, device="cuda", compute_type=compute_type)
        except Exception as exc:  # pragma: no cover - GPU might not be available
            print(f"[asr] CUDA init failed: {exc}; falling back to CPU int8")
            return WhisperModel(name, device="cpu", compute_type="int8")

    # auto preference
    try:
        return WhisperModel(name, device="cuda", compute_type=compute_type)
    except Exception as exc:
        print(f"[asr] CUDA init failed: {exc}; using CPU int8")
        return WhisperModel(name, device="cpu", compute_type="int8")


def get_whisper_model(name: str, device_pref: str, compute_type: str) -> WhisperModel:
    key = (name, device_pref, compute_type)
    if key not in _MODEL_CACHE:
        _MODEL_CACHE[key] = build_whisper_model(name, device_pref, compute_type)
    return _MODEL_CACHE[key]


def speak_text(
    text: str,
    *,
    voice: str | None = None,
    length_scale: float | None = None,
    noise_scale: float | None = None,
    noise_w: float | None = None,
) -> None:
    """Speak the provided text using Piper running inside Docker."""
    if not text:
        print("[tts] nothing to say")
        return

    voice = (voice or TTS_DEFAULTS["voice"]).lower()
    model_path = PIPER_MODEL_TEMPLATE.format(voice=voice)
    config_path = PIPER_CONFIG_TEMPLATE.format(voice=voice)

    cmd = [
        "sudo",
        "docker",
        "exec",
        "-i",
        PIPER_CONTAINER,
        "sh",
        "-lc",
        (
            f"/opt/piper/build/piper -q "
            f"-m {model_path} -c {config_path} "
            f"--espeak_data {ESPEAK_DATA_DIR} -f /dev/stdout"
        ),
    ]

    tunings = {
        "--length_scale": length_scale if length_scale is not None else TTS_DEFAULTS["length_scale"],
        "--noise_scale": noise_scale if noise_scale is not None else TTS_DEFAULTS["noise_scale"],
        "--noise_w": noise_w if noise_w is not None else TTS_DEFAULTS["noise_w"],
    }
    for flag, value in tunings.items():
        cmd[-1] += f" {flag} {value}"

    print(f"[tts] voice={voice} length={tunings['--length_scale']} noise={tunings['--noise_scale']} noise_w={tunings['--noise_w']}")
    try:
        p1 = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
        p2 = subprocess.Popen(
            ["aplay"], stdin=p1.stdout, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
        )
        if p1.stdin:
            p1.stdin.write((text.strip() + "\n").encode("utf-8"))
            p1.stdin.close()
        p2.communicate()
    except FileNotFoundError:
        print("[tts] 'aplay' not found. Install alsa-utils on the host.")
    except Exception as exc:
        print(f"[tts] error: {exc}")


def transcribe_file(
    path: str,
    *,
    model_name: str,
    device_pref: str,
    compute_type: str,
    beam_size: int,
) -> str:
    model = get_whisper_model(model_name, device_pref, compute_type)
    try:
        segments, info = model.transcribe(path, beam_size=beam_size, vad_filter=True)
        text = "".join(seg.text for seg in segments).strip()
        print(f"[asr] language={getattr(info, 'language', 'en')} avg_lat={getattr(info, 'avg_logprob', 0):.2f}")
        return text
    except Exception as exc:
        print(f"[asr] error: {exc}")
        return ""


# =============================================================================
# CLI helpers
# =============================================================================
def add_print_flag(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--print-config",
        action="store_true",
        help="Print a copy/paste snapshot of the current tuning defaults.",
    )


def format_config_snapshot() -> str:
    return textwrap.dedent(
        f"""
        # --- TTS defaults ---
        VOICE_DEFAULT = {repr(TTS_DEFAULTS['voice'])}
        TTS_TUNING_DEFAULTS = {json.dumps({k: TTS_DEFAULTS[k] for k in ('length_scale', 'noise_scale', 'noise_w')}, indent=2)}

        # --- ASR defaults ---
        WHISPER_DEFAULTS = {json.dumps({k: ASR_DEFAULTS[k] for k in ('model', 'device', 'compute_type', 'beam_size', 'sample_rate', 'record_seconds')}, indent=2)}
        """
    ).strip()


def print_config_snapshot() -> None:
    print("\nConfig snapshot (copy into assistant.py or your env):\n")
    print(format_config_snapshot())


def command_speak(args: argparse.Namespace) -> int:
    text = args.text or ""
    if args.file:
        try:
            with open(args.file, "r", encoding="utf-8") as handle:
                text += handle.read()
        except OSError as exc:
            print(f"[speak] could not read {args.file}: {exc}")
            return 1

    if not text:
        print("Enter text to speak. Finish with Ctrl-D / Ctrl-Z:")
        text = sys.stdin.read().strip()

    speak_text(
        text,
        voice=args.voice,
        length_scale=args.length_scale,
        noise_scale=args.noise_scale,
        noise_w=args.noise_w,
    )

    if args.print_config:
        print_config_snapshot()
    return 0


def command_listen(args: argparse.Namespace) -> int:
    dest = args.save_wav
    if dest:
        os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)

    path = record_audio(
        seconds=args.seconds,
        sample_rate=args.sample_rate,
        save_path=dest,
        beeps=not args.no_beep,
    )
    text = transcribe_file(
        path,
        model_name=args.model,
        device_pref=args.device,
        compute_type=args.compute_type,
        beam_size=args.beam_size,
    )
    print(f"[heard] {text or '(silence)'}")

    if not args.save_wav and os.path.exists(path):
        os.unlink(path)

    if args.print_config:
        print_config_snapshot()
    return 0


def command_snapshot(args: argparse.Namespace) -> int:  # noqa: ARG001
    print_config_snapshot()
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Speech tuning utility for Sophia.")
    sub = parser.add_subparsers(dest="command", required=True)

    speak = sub.add_parser("speak", help="Speak a prompt with Piper TTS.")
    speak.add_argument("--text", help="Text to speak. If absent, read from stdin.")
    speak.add_argument("--file", help="Load text from file.")
    speak.add_argument("--voice", help=f"Voice id (default: {TTS_DEFAULTS['voice']}).")
    speak.add_argument("--length-scale", type=float, help="Override Piper --length_scale.")
    speak.add_argument("--noise-scale", type=float, help="Override Piper --noise_scale.")
    speak.add_argument("--noise-w", type=float, help="Override Piper --noise_w.")
    add_print_flag(speak)
    speak.set_defaults(func=command_speak)

    listen = sub.add_parser("listen", help="Record audio and transcribe with faster-whisper.")
    listen.add_argument(
        "--seconds",
        type=float,
        default=ASR_DEFAULTS["record_seconds"],
        help=f"Recording length in seconds (default: {ASR_DEFAULTS['record_seconds']}).",
    )
    listen.add_argument(
        "--sample-rate",
        type=int,
        default=ASR_DEFAULTS["sample_rate"],
        help=f"Sample rate in Hz (default: {ASR_DEFAULTS['sample_rate']}).",
    )
    listen.add_argument(
        "--model",
        default=ASR_DEFAULTS["model"],
        help=f"Whisper model name (default: {ASR_DEFAULTS['model']}).",
    )
    listen.add_argument(
        "--device",
        choices=["auto", "cuda", "cpu"],
        default=ASR_DEFAULTS["device"],
        help=f"Device preference (default: {ASR_DEFAULTS['device']}).",
    )
    listen.add_argument(
        "--compute-type",
        default=ASR_DEFAULTS["compute_type"],
        help=f"Whisper compute_type (default: {ASR_DEFAULTS['compute_type']}).",
    )
    listen.add_argument(
        "--beam-size",
        type=int,
        default=ASR_DEFAULTS["beam_size"],
        help=f"Beam search size (default: {ASR_DEFAULTS['beam_size']}).",
    )
    listen.add_argument(
        "--save-wav",
        help="Optional path to save the captured WAV instead of deleting it.",
    )
    listen.add_argument(
        "--no-beep",
        action="store_true",
        help="Disable start/end beeps during recording.",
    )
    add_print_flag(listen)
    listen.set_defaults(func=command_listen)

    snap = sub.add_parser("snapshot", help="Print the current tuning defaults.")
    snap.set_defaults(func=command_snapshot)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
