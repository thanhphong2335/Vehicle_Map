# MotoMap — Codex Handoff

> Lưu ý: phần bên dưới mô tả kiến trúc web/GPS iPhone cũ tại ngày 2026-07-29. Firmware hiện đã chuyển sang GPS GY-NEO6MV2, hotspot iPhone, OSRM chạy trên ESP32, màn ngang 320×172 và pin màn mới. Dùng `README.md` cùng source hiện tại làm nguồn đúng; không khôi phục GPIO4/GPIO5 hoặc NimBLE từ handoff cũ.

Last updated: 2026-07-29

This document is the continuation point for another Codex instance. Read it before changing the project.

## 1. Overall objective

MotoMap is a motorcycle navigation/display device built around an ESP32 DevKit/WROOM-32 and a GMT147SPI 172×320 ST7789-compatible display.

The intended v1 workflow is:

1. The rider opens the MotoMap web app on an iPhone.
2. The user searches for an address with OpenStreetMap/Nominatim.
3. The user selects a result or manually moves a map pin to the exact location.
4. The web app gets a route from OSRM and tracks the iPhone GPS.
5. Navigation instructions are sent to the ESP32.
6. The ESP32 renders the next maneuver, distance, road name, remaining distance, and ETA.
7. When navigation is idle, the iPhone can stream JPEG frames from photos or video to the ESP32 display.

The project does not require a MacBook or Xcode for v1. A native SwiftUI iPhone app was drafted for a future phase, but the current usable path is the web app served by the ESP32 LittleFS filesystem.

The latest product discussion also considers a XiaoZhi-style AI assistant. The safe direction is for the iPhone or a backend to handle speech/AI and for the ESP32 to show short assistant cards. The ESP32 must not contain an OpenAI API key.

## 2. Current implementation status

### Web app: implemented

Location: `D:\Project ESP32 Moto_Map\moto_map\data`

- Dark responsive UI with Leaflet and OpenStreetMap tiles.
- Relative asset paths (`styles.css`, `app.js`) so the page works both from ESP32 LittleFS and from Live Server under `/moto_map/data/index.html`.
- Address search is submit-only through Nominatim; no autocomplete.
- Up to five Nominatim results are shown.
- Result selection places a destination marker.
- Destination marker is draggable.
- Direct map click sets a manual destination pin.
- Latitude/longitude can be entered manually.
- GPS current position can be used as a destination for testing.
- Location quality warns when OSM did not return a clear house number.
- OSRM route request includes full geometry, GeoJSON, and turn steps.
- GPS watch updates the active maneuver.
- Off-route detection triggers a reroute after a distance threshold and cooldown.
- Arrival state stops navigation and returns the UI from active navigation.
- Navigation has priority over media.
- Image/video selection is supported through a browser file input.
- Media is scaled to 172×320 with aspect-ratio preservation and sent as JPEG.
- Video frames are sent at approximately 10 fps initially.
- ESP32 status, navigation, media, and mode endpoints are called with relative URLs.
- Vehicle selector is implemented:
  - `motorcycle` / Xe máy is the default.
  - `car` / Ô tô is available.
  - Changing the vehicle clears the old route and asks the user to start a new route.
  - If navigation is already active, changing vehicle triggers a route request.
  - The selected vehicle is included in every navigation JSON packet.

Important limitation: both vehicle options currently map to OSRM's public `driving` profile. They are metadata/UI distinctions, not different routing graphs. Do not claim that the current OSRM endpoint provides motorcycle-specific restrictions.

### ESP32 firmware: implemented in source

Location: `D:\Project ESP32 Moto_Map\moto_map`

- PlatformIO environment: `esp32dev`, Arduino framework.
- LittleFS is enabled with `board_build.filesystem = littlefs`.
- LovyanGFX is used for the ST7789-compatible display.
- TJpg_Decoder is used to decode incoming JPEG frames directly.
- NimBLE-Arduino provides the BLE navigation service.
- ArduinoJson parses HTTP/BLE navigation packets.
- Firmware modes:
  - `IDLE`
  - `NAVIGATION`
  - `MEDIA`
  - `ERROR`
- Main firmware modules:
  - `DisplayManager`
  - `NavigationManager`
  - `MediaManager`
  - `ConnectionManager`
  - `ModeController`
- Wi-Fi AP defaults to:
  - SSID: `MotoMap-ESP32`
  - Password: `motomap123`
  - IP: normally `192.168.4.1`
- HTTP routes:
  - `GET /`
  - `GET /status`
  - `POST /navigation/update`
  - `POST /navigation/stop`
  - `POST /mode/media`
  - `POST /mode/idle`
  - `POST /media/frame`
- Navigation rejects media frames with HTTP 409 while navigation is active.
- Navigation JSON now accepts `vehicle`; the ESP32 displays `Motorbike` or `Car` in the navigation footer.
- BLE UUIDs:
  - Service: `4D4F544F-4D41-5000-0000-000000000001`
  - Navigation RX: `4D4F544F-4D41-5000-0000-000000000002`
  - Status TX: `4D4F544F-4D41-5000-0000-000000000003`

### Native iOS source: drafted, not the v1 path

Location: `D:\Project ESP32 Moto_Map\ios\MotoMap`

The SwiftUI files cover the intended native architecture: MapKit, Core Location, Core Bluetooth, PhotosUI, UIDocumentPicker, AVFoundation, and ImageIO. There is no Xcode project and it has not been compiled because the user does not have a MacBook. Keep this code separate from the current web v1 unless the user later obtains a Mac or chooses a cloud Mac build workflow.

### Verification status

- `node --check data\app.js` passed from `D:\Project ESP32 Moto_Map\moto_map`.
- The web page was opened through Live Server and the vehicle selector was verified:
  - selecting `car` changed the select value to `car`;
  - the vehicle note changed to the car text;
  - route detail changed to indicate the selected vehicle.
- A PlatformIO build attempt initially failed because the global `.platformio` directory could not be locked (`platforms.lock` permission error).
- A subsequent elevated PlatformIO attempt timed out after 120 seconds, but `.pio\build\esp32dev\firmware.bin`, `firmware.elf`, and source object files were created. Treat this as “build artifacts exist, final clean build result not yet confirmed”; do not claim hardware upload was completed.
- No firmware has been uploaded to the physical ESP32 in this session.

## 3. Files modified and why

### Project documentation and build helpers

- `D:\Project ESP32 Moto_Map\README.md`
  - Documents the v1 web flow, PlatformIO commands, Wi-Fi credentials, display wiring, HTTP API, OSM/OSRM limitations, and the `vehicle` payload field.
- `D:\Project ESP32 Moto_Map\moto_map\platformio.ini`
  - Defines `esp32dev`, Arduino, LittleFS, monitor speed, and required libraries.
- `D:\Project ESP32 Moto_Map\moto_map\build.cmd`
  - Windows wrapper for `build`, `upload`, `uploadfs`, and `monitor` using the PlatformIO Core installation under the user's profile.
- `.vscode/settings.json` may contain the local PlatformIO Core/Python configuration added during setup; preserve it if present.

### Web app

- `D:\Project ESP32 Moto_Map\moto_map\data\index.html`
  - Main web UI.
  - Uses relative CSS/JS paths.
  - Contains search, manual coordinates, map, navigation, media, and vehicle selector UI.
- `D:\Project ESP32 Moto_Map\moto_map\data\styles.css`
  - Dark MotoMap visual system.
  - Contains responsive cards, map layout, buttons, media preview, and vehicle `<select>` styling.
- `D:\Project ESP32 Moto_Map\moto_map\data\app.js`
  - Nominatim search, Leaflet map, destination marker, GPS watch, OSRM route/steps, navigation state machine, ESP32 HTTP calls, media frame streaming, status polling, and vehicle selection.
  - Vehicle routing is defined in `VEHICLES`; both `motorcycle` and `car` currently use profile `driving`.

### Firmware

- `moto_map/include/MotoMapTypes.h`
  - Defines device modes, navigation states, maneuvers, and `NavigationData`.
  - `NavigationData.vehicle` defaults to `motorcycle`.
- `moto_map/include/ConnectionManager.h` and `src/ConnectionManager.cpp`
  - Wi-Fi AP, LittleFS serving, HTTP routes, status JSON, navigation update/stop, media frame handling, and mode endpoints.
- `moto_map/include/NavigationManager.h` and `src/NavigationManager.cpp`
  - NimBLE custom service, connection state, JSON parsing, sequence tracking, navigation callback.
  - Parses `vehicle` from navigation payload.
- `moto_map/include/ModeController.h` and `src/ModeController.cpp`
  - Enforces mode transitions and navigation priority.
- `moto_map/include/DisplayManager.h` and `src/DisplayManager.cpp`
  - LovyanGFX display initialization, rotation/offset configuration, navigation arrow rendering, idle/error/media screens, and JPEG drawing.
  - Navigation footer now shows the selected vehicle.
- `moto_map/include/MediaManager.h` and `src/MediaManager.cpp`
  - Receives and decodes transient JPEG frames without maintaining a media library.
- `moto_map/src/main.cpp`
  - Initializes managers and routes navigation callbacks to the mode/display system.

### Native iOS draft

- `ios/MotoMap/*.swift` and `ios/MotoMap/Info.plist`
  - Future native companion app draft. Do not assume this is buildable without creating an Xcode target on macOS.

Do not treat `.pio`, `.platformio-local`, downloaded PlatformIO packages, or build artifacts as hand-written source. They are generated/tooling state.

## 4. Important architectural decisions

### Web-first instead of native iOS

The user does not have a MacBook, so the current product path is a web app served by ESP32 LittleFS and opened in Safari. Native MapKit/SwiftUI remains a future option only.

### OSM/Nominatim/OSRM instead of Google Maps

- Nominatim is used for address lookup.
- The user must confirm a result or move the pin manually when the house number/alley is missing from OSM.
- Leaflet renders OSM tiles and includes attribution.
- OSRM provides route geometry and turn steps.
- Public Nominatim and OSRM are prototype services; throttle requests and do not turn Nominatim into autocomplete.

### Separate responsibilities

- iPhone/browser: internet calls, address search, routing, GPS tracking, route-step interpretation, media scaling, and future AI.
- ESP32: display rendering, local HTTP server, BLE receiver, media frame decode, and mode enforcement.
- ESP32 should not perform expensive routing or hold a cloud API secret.

### Navigation priority

Navigation must always win over media. The firmware rejects `/media/frame` while in `NAVIGATION`, and the web app stops media when navigation begins.

### Transport split

The original design includes BLE for compact navigation packets and Wi-Fi for media. The current web v1 primarily uses the ESP32 HTTP routes. Native iOS code is the planned BLE path. Be careful not to assume the web browser can use the same BLE behavior as the native app on every iPhone/Safari version.

### Future XiaoZhi-style assistant

XiaoZhi is an architectural reference, not a drop-in library for this exact board/display. A XiaoZhi-like product normally uses ESP32 audio hardware as the edge client and a server/cloud protocol for speech recognition, TTS, AI, and device control. For MotoMap:

- First implementation should be iPhone/web voice or text input → backend/OpenAI → short JSON assistant card → ESP32 display.
- Do not put an OpenAI API key in `app.js`, LittleFS, or ESP32 firmware.
- Do not let an LLM decide the navigation arrow. OSRM remains authoritative for routing.
- If true voice-on-device is desired, add an I2S microphone, amplifier, and speaker; an ESP32-S3 with PSRAM is a better future target than the current WROOM-32.
- AI should be event-driven or user-triggered, not called for every GPS update.

## 5. Bugs and problems already investigated

### White, unstyled web page

Cause: the page was loaded at `http://127.0.0.1:5500/moto_map/data/index.html`, but HTML used root-relative `/styles.css` and `/app.js`. Live Server therefore requested files from the wrong root and the browser showed default HTML styling.

Fix: changed asset references to `styles.css` and `app.js`.

### PlatformIO project not found in VS Code

The user had PlatformIO Home open, but the terminal/project shown was `D:\TTNT\cm4_gateway\gateway_c++`. The correct PlatformIO project is `D:\Project ESP32 Moto_Map\moto_map`, which contains `platformio.ini`. Open that folder, then use Project Tasks → `esp32dev` → Build/Upload/Upload Filesystem Image.

### `pio is not recognized`

The user's PlatformIO Core is installed under:

```text
C:\Users\Thanh Phong\.platformio\penv\Scripts\platformio.exe
```

The wrapper `moto_map\build.cmd` avoids relying on the PowerShell PATH. Alternatively, use the full path or the PlatformIO IDE UI.

### PlatformIO permission/timeout

The first direct build failed trying to lock:

```text
C:\Users\Thanh Phong\.platformio\platforms.lock
```

It also reported that `.platformio` or `.platformio\.cache` was not owned by the current user. An elevated retry ran long enough to create build artifacts but timed out after 120 seconds. The next Codex should verify the actual PlatformIO configuration and run the build from the correct project/VS Code task; do not delete broad directories or reset user PlatformIO state without explicit approval.

### Address precision

Nominatim cannot guarantee a house number inside an alley when OSM has no corresponding data. Manual pin dragging and coordinate input are intentional fallbacks. The route can only follow mapped roads and may end at the nearest mapped road.

### Browser/GPS limitation

Safari needs location permission and generally needs the page kept open during active navigation. Internet may be unavailable when the iPhone is connected only to the ESP32 AP; a future STA-to-iPhone-hotspot mode or a better network topology is needed.

### Encoding observation

Some PowerShell `Get-Content` output appeared as mojibake, while the browser rendered Vietnamese correctly. Treat this as a console encoding/display issue unless a browser or file-byte check proves the source file itself is corrupted.

## 6. Remaining tasks in priority order

### P0 — Verify hardware build and upload

1. Open `D:\Project ESP32 Moto_Map\moto_map` as the VS Code workspace, not the unrelated gateway project.
2. Run `node --check data\app.js`.
3. Run PlatformIO Build for `esp32dev`.
4. Upload firmware and LittleFS separately.
5. Open serial monitor at 115200.
6. Verify boot, Wi-Fi AP, `GET /status`, and serving `/`.
7. Send a manual navigation JSON packet and verify the arrow, distance, road, vehicle, and mode.

### P0 — Verify the actual GMT147SPI panel

1. Confirm the panel controller and physical orientation.
2. Test red/green/blue/white/black, text, arrow, and JPEG rendering.
3. Adjust LovyanGFX rotation and panel offsets in `DisplayManager.cpp` if the image is shifted or clipped.
4. The user now wants the physical screen horizontal. Implement and test a 320×172 horizontal layout without displaying a clock.

### P1 — Stabilize web navigation

- Test real Nominatim search with house-number and alley examples.
- Test manual pin drag and direct map click.
- Test OSRM route, turn-step progression, arrival, and off-route reroute.
- Test loss/recovery of GPS, Wi-Fi, and device connection.
- Test iPhone screen lock/background behavior; document what Safari cannot reliably do.
- Add request throttling/error backoff where needed.

### P1 — Make the horizontal UI useful

Recommended 320×172 layout:

- Main left area: very large arrow, maneuver label, distance to turn, road name.
- Right area: speed from iPhone GPS, remaining distance, ETA, connection indicators, and one short AI/alert line.
- No clock because the motorcycle already has one.
- Add day/night theme, brightness control, and possibly physical buttons before adding decorative media.

### P2 — Add phone telemetry

Extend the navigation packet with fields such as:

- `speed_kmh`
- `heading_deg`
- `phone_battery_percent`
- `gps_accuracy_m`
- `route_status`

Render them in the horizontal display. Keep packets small and rate-limit updates.

### P2 — Add a safe AI assistant bridge

Implement a separate backend/proxy, or a carefully designed iPhone app bridge, with:

- user-triggered text/voice input;
- OpenAI call on the server side;
- tools/functions for Nominatim, OSRM, nearby POI search, and possibly weather;
- strict short JSON output such as `type`, `title`, `text`, `priority`, `ttl_ms`;
- display card overlaid in a low-priority region of the horizontal screen;
- fallback to normal navigation if AI/network fails;
- explicit rate limits and API-key protection.

Useful commands include “find a petrol station”, “find food near the route”, “change destination”, and “repeat the next turn”. Do not use AI for the authoritative route arrow.

### P3 — XiaoZhi-like voice hardware

Only after the display/navigation core is stable:

- add I2S microphone;
- add MAX98357A or equivalent audio amplifier and speaker;
- add push-to-talk first, wake word later;
- choose ESP32-S3/PSRAM if real-time audio buffering becomes difficult;
- decide whether the ESP32 connects through the iPhone hotspot or whether the iPhone remains the audio/AI bridge.

### P4 — Native iOS app

Create a real Xcode target on macOS, compile the SwiftUI draft, validate MapKit/Core Location/Core Bluetooth, and decide whether native BLE is worth replacing the web v1.

## 7. Known constraints

- No MacBook is available, so native iOS cannot be built/tested locally.
- Current target board is `esp32dev` / ESP32 WROOM-32; RAM and flash are limited.
- Current physical screen is assumed ST7789-compatible GMT147SPI, but exact controller offsets/rotation still need hardware validation.
- Current screen code is built around 172×320 portrait coordinates; horizontal 320×172 redesign remains.
- The iPhone AP connection and Internet access problem is unresolved: an iPhone connected to ESP32 AP may not route public Internet in the way the web app needs.
- Nominatim public service has usage policies and is not suitable for aggressive autocomplete or high-volume production use.
- OSRM public demo is a prototype dependency and currently uses `driving` for both vehicle options.
- Browser geolocation needs user permission and may stop/restrict when Safari is backgrounded or locked.
- The ESP32 does not currently have a microphone, speaker, camera, or dedicated GPS module.
- Media is transient JPEG streaming; there is no durable media library on ESP32.
- OpenAI API usage requires credentials, network access, cost control, and a secure server-side key boundary.
- The current HTTP web app is not equivalent to native iOS BLE support; do not assume Safari will expose every BLE API needed by the native design.

## 8. Assumptions made

- `D:\Project ESP32 Moto_Map` is the project root.
- `D:\Project ESP32 Moto_Map\moto_map` is the PlatformIO project and firmware root.
- The display wiring documented in `README.md` is intended: SCK GPIO18, MOSI GPIO23, RST GPIO17, DC GPIO16, CS GPIO5, BL GPIO4, plus 3V3/GND.
- The user accepts manual pin correction when OSM has incomplete house/alley data.
- The iPhone is the authoritative GPS source for navigation.
- Navigation should remain usable without AI.
- Vehicle selection currently means display/metadata selection plus the same OSRM driving route, not true motorcycle-vs-car restrictions.
- The user prefers a horizontal display and does not want a clock because the motorcycle already has one.
- A XiaoZhi-style assistant is a future feature idea, not yet an implementation requirement.

## 9. Suggested first action for the next Codex

Start by opening this handoff and then run these read-only/validation commands from the correct firmware directory:

```powershell
cd D:\Project ESP32 Moto_Map\moto_map
node --check data\app.js
Get-Content .\platformio.ini
Get-Content .\src\DisplayManager.cpp
```

Then run a PlatformIO build using the VS Code Project Tasks for `esp32dev` or the full PlatformIO executable. Confirm whether the existing generated firmware really corresponds to the latest `vehicle` changes. After build verification, prioritize the physical horizontal display test and redesign `DisplayManager::showNavigation()` for 320×172. Do not begin the AI/XiaoZhi layer until basic navigation, screen rotation, Wi-Fi, and media behavior are confirmed on hardware.
