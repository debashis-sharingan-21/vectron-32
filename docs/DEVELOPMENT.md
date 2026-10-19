# Development guide

Back to the [README](../README.md). Architecture background: [ARCHITECTURE.md](ARCHITECTURE.md).

## Repository layout

```text
platformio.ini     one environment: esp32dev (Arduino, LittleFS, huge_app.csv)
include/config.h   pins, limits, timings               (safe to edit)
include/secrets.*  credentials template / your private copy
src/               main.cpp · api.cpp · auth.cpp · hardware.cpp (+ headers)
data/              frontend → LittleFS image
tools/             mock-server.js
docs/              guides and screenshots
```

## Build commands

| Command | What it does |
|---|---|
| `pio run` | Compile the firmware |
| `pio run -t upload` | Flash the firmware |
| `pio run -t buildfs` | Pack `data/` into a LittleFS image |
| `pio run -t uploadfs` | Flash the filesystem image |
| `pio device monitor` | Serial monitor, 115200 baud |
| `pio run -t clean` | Clean build output |

Firmware and filesystem are independent uploads. Changed `src/` or `include/` → `upload`. Changed `data/` → `uploadfs`.

## Work on the UI without hardware

```bat
node tools/mock-server.js
```

Open <http://localhost:8099/> and log in with `demo` / `demo` (demo-only; the real device uses `include/secrets.h`).
The mock imitates the routes in `src/api.cpp` with **fake** data. It is useful for layout and JavaScript work but proves
nothing about the firmware. Set `MOCK_PORT`, `MOCK_USER`, `MOCK_PASS` to change the defaults.

## Extending the project

### Add a GPIO output or input

Add a row to `OUTPUTS[]` / `INPUTS[]` in `include/config.h`; upload the firmware. See [HARDWARE.md](HARDWARE.md#adding-an-output).
Outputs must also be on the allow-list in `Hardware::isSafeOutputPin` (`src/hardware.cpp`).

### Add an API route

1. Add the hardware function to `src/hardware.h` / `hardware.cpp`. If it is slow (more than a few milliseconds), expose a
   `requestX()` that sets a flag and do the work in `Hardware::service()`, which runs from `loop()`.
2. Register the route in `Api::begin` (`src/api.cpp`). Use `requireAuth` for reads and `requireAuthPost` for writes
   (the latter also checks `Origin`). Read form fields with `getParam`.
3. If the dashboard needs to show state, extend `buildStatus()` (hand-built JSON — remember to escape strings with `jsonEscape`).
4. Document the route in [ARCHITECTURE.md](ARCHITECTURE.md) and, if it can fail on hardware, in [FEATURES.md](FEATURES.md).

### Add a dashboard panel

1. Add a `<div class="mod">…</div>` block in the dashboard section of `data/index.html` (copy an existing one, use `data-k` attributes for values).
2. Fill it from the status JSON in `render()` in `data/script.js`; use `act()` for buttons so they get loading/error states.
3. Style with the existing CSS variables in `data/style.css`. Keep the green-terminal look and avoid external requests (no CDNs).
4. Run `pio run -t uploadfs` and hard-refresh the browser (Ctrl+F5).

The two empty `SLOT_A` / `SLOT_B` entries in the `modules --list` panel are placeholders for this.

## Conventions

- C++: 2-space indent, one namespace per module, no dynamic work in the web-server task.
- Never commit `include/secrets.h` or real network details. Redact serial logs and screenshots.
- Label anything you could not test on hardware as untested in the docs and PR description.
- No new external runtime dependencies without discussion (the project intentionally avoids ArduinoJson).

## Testing

There is no automated test suite. Before a pull request, at minimum:

1. `pio run` succeeds.
2. For UI changes: run the mock server and exercise login, wrong password, lockout and the panels you touched.
3. For firmware changes: say exactly what you tested on which board.
