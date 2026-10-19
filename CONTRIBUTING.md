# Contributing to VECTRON-32 Command Center

Thanks for helping! This is an embedded systems and SCADA instrumentation project. By taking part you agree to the [Code of Conduct](CODE_OF_CONDUCT.md). Read the [README](README.md) first.

## Privacy Rules

This is a public repository. In code, commits, issues, logs and screenshots **never include**:
- real Wi-Fi names or passphrases, dashboard logins, tokens or session cookies
- your private network details, device MAC addresses or personal local file paths
- `include/secrets.h` (it is git-ignored — verify with `git status` before committing)

## Reporting Issues

Open an issue with:
1. Expected vs actual behavior.
2. ESP32 board model, framework version, and `platformio.ini` settings.
3. Serial monitor logs from boot.

For security vulnerabilities, see [SECURITY.md](SECURITY.md).

## Submitting Pull Requests

1. Fork the repository and create a branch (`feature/your-feature` or `fix/your-fix`).
2. Verify `pio run` builds cleanly without warnings.
3. Verify the phosphor-green terminal SCADA aesthetics and mobile responsiveness are preserved.
4. Document any new hardware peripherals or pin assignments in `include/config.h` and `docs/HARDWARE_GUIDE.md`.

## Attribution

Developed by **debashis-sharingan-21**  
GitHub: [@debashis-sharingan-21](https://github.com/debashis-sharingan-21)
