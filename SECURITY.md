# Security Policy

VECTRON-32 is an embedded instrumentation and SCADA console designed for local operational networks.

## Security Model

```text
Client ──Wi-Fi (WPA2, password configured at setup)──▶ ESP32 SoftAP ──HTTP/WS──▶ VECTRON-32
```

### Implemented Safeguards:

- **Hardware-Salted SHA-256 Storage:** Passwords are never stored in plaintext. They are salted with 16 random bytes from ESP32 hardware entropy (`esp_fill_random`) and hashed with `mbedtls_sha256` into NVS.
- **Constant-Time Verification:** Username and password comparisons execute in constant time to prevent timing attacks.
- **Session Tokens:** Cryptographically random session tokens generated from the hardware RNG, transported via `HttpOnly`, `SameSite=Strict` `VECTRON_SID` cookies.
- **RTC Brute-Force Defense:** Failed attempt counters and lockout expiration are stored in `RTC_DATA_ATTR` memory, resisting power cycling and brownout reset attacks.
- **Hardware Isolation Matrix:** Only pins registered in the safe-pin allow-list can be actuated. Strapping pins (0, 2, 12, 15), UART pins (1, 3), and internal flash SPI pins (6-11) are strictly protected.

## Reporting a Vulnerability

Please **do not** open a public issue with exploit details.

1. Use GitHub's private vulnerability reporting (repository **Security** tab → *Report a vulnerability*).
2. Contact the maintainer on GitHub: [@debashis-sharingan-21](https://github.com/debashis-sharingan-21).
3. Include reproduction steps, firmware version, and relevant logs. Never include real credentials or production network captures.
