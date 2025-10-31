# Sophia Release TODO

Prioritized checklist to reach a clean public release.

## P0 Blockers
- Add per-package license notices or reference the root MIT license if duplication is unnecessary (`apps/assistant/LICENSE` vs root `LICENSE`).
- Package the assistant as an installable Python project with entry points, versioning, and configuration instead of standalone scripts (`apps/assistant/tools/assistant/assistant.py`, `assistant_fw.py`, `ask_once.py`).
- Unify and pin dependencies; resolve missing imports and duplicated requirement files (`apps/assistant/requirements.txt`, `apps/assistant/tools/assistant/requirements.txt`, `assistant.py`).
- Externalize backend URLs and privileged commands to config/env documentation; avoid hard-coded IPs and `sudo docker` assumptions (`assistant.py`, `ask_once.py`, `assistant_fw.py`).
- Deliver the promised shared speech modules or update scope/README expectations (`README.md` claims, empty `packages/speech/`).

## P1 High Priority
- Add a smoke/integration test suite so `pytest` and future CI have coverage (`README.md` quickstart).
- Create GitHub Actions workflows matching the documented contributor flow (empty `.github/workflows/`).
- Clean up and publish the Jetson setup guide with correct Markdown/code fences (`apps/assistant/docs/setup-jetson.md` formatting).
- Replace firmware stubs with real face animation and USB link behaviors described in docs (`firmware/cyd/firmware/friend-esp32-cyd-2_8/src/*.cpp`).

## P2 Nice To Have
- Populate `hardware/cyd-display/` with schematics, wiring, and BOMs as promised in the README.
- Document data/log retention and privacy expectations for `~/.sophia` profiles (`assistant.py`, `skills/new_person.py`).
- Provide sample configuration (e.g., `.env.example`) demonstrating necessary environment variables for LLM and TTS backends.
