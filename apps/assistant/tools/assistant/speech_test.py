"""
`speech_test.py` has been replaced by `voice_tuning.py`.

The legacy helper pulled in heavy runtime dependencies that are no longer part of
the supported toolchain. This stub exists so imports fail loudly while the file
remains in source control; the CLI now lives in `voice_tuning.py`.
"""

raise RuntimeError(
    "speech_test.py was deprecated. Please run voice_tuning.py for speech/ASR tuning."
)
