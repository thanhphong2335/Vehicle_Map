# MotoMap iPhone app

This directory contains the native SwiftUI source for the MotoMap iPhone companion app.

## Create the Xcode target

On macOS, create an iOS App target named `MotoMap` with SwiftUI and iOS 17 or newer, then add all `.swift` files in this directory to the target. Add `Info.plist` as the target's custom Info.plist file.

The app uses:

- MapKit for address search, Apple Maps rendering, and route directions.
- Core Location for navigation location updates.
- Core Bluetooth to write navigation JSON to the ESP32.
- PhotosUI and Files for media selection.
- AVFoundation and ImageIO for media frame conversion.

## ESP32 connection

Navigation uses BLE with these UUIDs:

```text
Service:       4D4F544F-4D41-5000-0000-000000000001
Navigation RX: 4D4F544F-4D41-5000-0000-000000000002
Status TX:     4D4F544F-4D41-5000-0000-000000000003
```

Media uses the ESP32 access point:

```text
SSID:     MotoMap-ESP32
Password: motomap123
Endpoint: http://192.168.4.1/media/frame
```

The app sends one normalized JPEG frame per HTTP POST. The ESP32 keeps no media library; it decodes each frame directly to the ST7789 display.

