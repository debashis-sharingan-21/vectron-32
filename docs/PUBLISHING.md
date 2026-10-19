# Publishing to GitHub

Target details for publishing VECTRON-32:

## Repository Details

| Field | Value |
|---|---|
| Name | `vectron-32` |
| Remote URL | `https://github.com/debashis-sharingan-21/vectron-32.git` |
| Author | `debashis-sharingan-21` |
| Description | Industrial SCADA and real-time instrumentation console running entirely on an ESP32-WROOM-32 (FreeRTOS dual-core, WebSocket 10Hz telemetry, LittleFS). |
| Topics | `esp32` `freertos` `platformio` `scada` `instrumentation` `websocket` `littlefs` `cyberpunk` `embedded` `cpp17` |
| License | MIT |

## Push Instructions

```bash
git init
git config user.name "debashis-sharingan-21"
git config user.email "debashis-sharingan-21@users.noreply.github.com"
git remote add origin https://github.com/debashis-sharingan-21/vectron-32.git
git add .
git commit -m "feat: complete VECTRON-32 v2.0 refactor - FreeRTOS dual-core, WebSocket telemetry, SCADA UI"
git branch -M main
git push -u origin main --force
```
