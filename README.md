# Sophia Monorepo

**Sophia** is a modular open‑source robotics and AI assistant framework that brings together hardware expression, voice interaction, and language intelligence.  
This repository unifies multiple subsystems into one coherent project.

---

## Repository Overview

```
sophia/
├── apps/
│   └── assistant/        # Python assistant core (LLM, STT, TTS, skills)
├── firmware/
│   └── cyd/              # ESP32‑S3 CYD 2.8" display firmware (PlatformIO)
├── packages/
│   └── speech/           # Shared Python adapters (TTS/STT, audio utils)
├── hardware/
│   └── cyd-display/      # Schematics, wiring, 3D models, and BOMs
├── docs/                 # Setup and architecture documentation
└── scripts/
    └── dev/              # Development helper scripts
```

---

## Features

- **Conversational Assistant** – Speech recognition, text‑to‑speech, and language model integration.  
- **Expressive Display** – Firmware for the CYD ESP32‑S3 2.8" TFT used for animated facial expressions.  
- **Shared Speech Modules** – Reusable Python adapters for speech pipelines.  
- **Cross‑platform Development** – Built for Linux/WSL environments, deployable to embedded hardware.  
- **Open and Extensible** – Designed to be forked, remixed, and extended for personal or research use.

---

## Quickstart

### 1. Environment Setup (WSL / Ubuntu)
```bash
# Clone repository
git clone https://github.com/stvenmobile/sophia.git
cd sophia

# Python assistant setup
cd apps/assistant
pip install -r requirements.txt
pytest -q
```

### 2. Firmware Build (CYD Display)
```bash
cd firmware/cyd
pio run
pio run -t upload
pio device monitor -b 115200
```

### 3. Directory Conventions
- `apps/assistant` — All high‑level Python logic for the AI assistant.
- `firmware/cyd` — PlatformIO‑based firmware for ESP32‑S3 CYD displays.
- `packages/` — Shared modules for cross‑app functionality.
- `hardware/` — Documentation for wiring and 3D printed enclosures.
- `docs/` — Architecture, setup guides, and developer documentation.

---

## Development Notes

- Recommended Python ≥ 3.11  
- Use [PlatformIO](https://platformio.org/) for firmware builds  
- Use GitHub Actions CI to verify builds and tests  
- Keep shared logic modular (avoid circular imports between apps/packages)  
- All contributions must follow [Conventional Commits](https://www.conventionalcommits.org/) style

---

## License

This project is released under the **MIT License**.  
See [LICENSE](LICENSE) for details.

---

## Contributing

Pull requests are welcome!  
Before contributing:
1. Fork the repository and create a feature branch (`git checkout -b feature/xyz`).
2. Run linting/tests (`ruff check . && pytest`).
3. Submit your PR with a concise description.

---

## Acknowledgements

Sophia builds upon contributions from the open‑source community across embedded systems, speech processing, and AI research.  
Special thanks to early collaborators and testers who helped shape the architecture and direction of this project.
