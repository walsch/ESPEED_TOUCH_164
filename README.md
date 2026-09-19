# ESPEED Touch 1.64

Touchscreen-Oberflaeche fuer das ESP32-S3 Touch AMOLED 1.64.

## Hardware

- ESP32-S3 Touch AMOLED 1.64
- SH8601 AMOLED, 280 x 456 Pixel
- QSPI: CS 9, CLK 10, D0 11, D1 12, D2 13, D3 14
- Display Reset: GPIO 21
- FT3168 Touch: SDA 47, SCL 48, I2C-Adresse `0x38`
- Darstellung: Landscape ueber einen software-gedrehten Arduino_Canvas

## Software

- Arduino IDE/CLI
- ESP32 Arduino Core 3.x
- GFX Library for Arduino 1.6.8
- Arduino_GFX SH8601 und Arduino_Canvas

Kompilieren und Upload:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3 .
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:esp32s3 .
```

## Struktur

- `espeed_touch_164.ino`: Arduino-Setup und Hauptschleife
- `display_unit.h`: oeffentliche Display-API
- `display_unit.cpp`: Display, Touch, Menues und Gesten

## Bedienung

Der Startbildschirm wird uebersprungen. Der Rennbildschirm zeigt die wichtigsten Werte:

- SENS
- BRAKE
- EXPO
- CAR
- Statusleiste fuer TRIGGER und BAHN V

Im Rennbildschirm:

- Horizontales Wischen oben: BRAKE relativ veraendern
- Horizontales Wischen unten: SENS relativ veraendern
- Linke untere Karte: Settings Menu
- Rechte untere Karte: Car Menu

Im Settings Menu:

- Horizontales Raster mit kompakten Karten
- Horizontales Ziehen scrollt dem Finger folgend
- Tap auf einen Eintrag oeffnet den Edit-Screen
- `CANCEL` verwirft, `SAVE` uebernimmt

Spezielle Edit-Screens:

- EXPO: Graph mit Triggerposition auf X und DutyCycle auf Y; relative und absolute Gesten
- CURVE: gemeinsamer Graph fuer CURVX/CURVE; horizontales Wischen verschiebt X, vertikales Wischen verschiebt Y

Car Menu (`SELECT CAR`):

- 20 Fahrzeuge (`CAR 01` ... `CAR 20`) in einem zweizeiligen, horizontal scrollbaren Raster; Ziehen folgt dem Finger und rastet auf Spalten ein
- Das aktive Fahrzeug ist orange umrandet
- Tap auf eine Karte waehlt das Fahrzeug und kehrt zur aufrufenden Seite zurueck (Rennbildschirm oder Settings); `BACK` ebenso
- Nur vollstaendig sichtbare Karten werden gezeichnet, um Zeichenartefakte am Rand zu vermeiden

## Darstellung

Alle Farben sind als RGB565-Palette in `display_unit.cpp` gebuendelt (`COLOR_*`): schwarzer Hintergrund, deutlich abgesetzte Panels und Rahmen, gelbe Labels, weisse Werte, Cyan/Orange als Akzente, Rot fuer `CANCEL`.

Schriftgroessen (GFX `setTextSize`): Ueberschriften 3, Labels 2, Werte 3, Fussleisten 2. Die C/C++-IntelliSense ist fuer diesen Workspace in `.vscode/settings.json` aktiviert, damit die Outline-Ansicht funktioniert.

## EXPO und CURVE

Die EXPO-Darstellung verwendet die Berechnungsfolge aus dem alten ESPEED32-Projekt. EXPO liegt im Bereich `-100 ... 100`, wobei `0` eine gerade Kennlinie ist.

Die CURVE-Darstellung nutzt die alte zweigeteilte Kennlinie mit Vertex:

- X/`CURVX`: `20 ... 80 %` Triggerposition
- Y/`CURVE`: `10 ... 90 %` Kurvenwert
- `SENSI` und `LIMIT` begrenzen die Kennlinie

## Aktueller Entwicklungsstand

Die Anzeige, Touch-Gesten und Menunavigation sind funktionsfaehig. Die Einstellwerte sind derzeit lokale UI-Werte. `TRIGGER` und `BAHN V` werden noch als Platzhalter angezeigt und muessen spaeter an die ESC-/ADC-Daten angebunden werden.

## GitHub

Privates Repository: https://github.com/walsch/ESPEED_TOUCH_164

Der lokale Branch `main` verfolgt `origin/main`.
