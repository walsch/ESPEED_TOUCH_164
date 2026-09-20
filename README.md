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

Der Startbildschirm wird uebersprungen. Alle Seiten haben eine Titelzeile; Aktionsbuttons (`BACK`, `SAVE`, `CANCEL`, ...) liegen rechts in der Titelzeile (Slots bei x=232 und x=342, je 100x34 px).

### Rennbildschirm (`ESPEED32`)

Vier Karten (196x64 px) plus Statusleiste:

- Karte 1 (oben links), Karte 2 (oben rechts), Karte 3 (unten links): frei belegbar mit einer beliebigen Einstellung (Standard: SENSI, BRAKE, EXPO). Anzeige von Name, Wert und Einheit.
- Karte 4 (unten rechts, orange): aktives Fahrzeug (`CAR`), fest.
- Statusleiste: `TRIGGER` (Platzhalter) und `VCC` (gemessene Betriebsspannung, siehe unten).
- Titelzeile: `SETTINGS` oeffnet das Settings-Menu.

Gesten (die Achse wird beim ersten Bewegen festgelegt):

- Horizontal in der oberen Haelfte: Wert von Karte 1 relativ aendern
- Horizontal in der unteren Haelfte: Wert von Karte 2 relativ aendern
- Vertikal in der linken Haelfte: Wert von Karte 3 (nach oben = erhoehen)
- Vertikal in der rechten Haelfte: Fahrzeug wechseln (nach unten = naechstes, ein Schritt je 60 px)
- Tap auf die `CAR`-Karte: Car Menu
- Doppeltipp auf Karte 1-3: Seite `SLOT n` zum Zuordnen einer Einstellung (Raster aller Einstellungen, aktuelle Zuordnung orange)

### Settings Menu

- Dreispaltiges, zweizeiliges Kartenraster ueber die volle Breite (148 px Karten, Label Groesse 3, Wert Groesse 4), horizontal dem Finger folgend scrollbar, rastet auf Spalten ein
- Tap auf eine Karte oeffnet den Edit-Screen
- Titelzeile: `CONFIG` (globale Optionen) und Badge mit dem aktiven Fahrzeug (oeffnet das Car Menu)
- Fussleiste `BACK` fuehrt zum Rennbildschirm

### Edit-Screen

- Titelzeile: `CANCEL` (verwirft, stellt den Ursprungswert wieder her) und `SAVE`
- Range-Zeile unter dem Titel (z. B. `0% - 100%`), Wert gross zentriert (Groesse 8, Einheit Groesse 4)
- Obere Haelfte: horizontales Wischen aendert den Wert relativ
- Untere Haelfte: horizontale Position wirkt als absoluter Slider (auch per Tap); es werden keine Slider gezeichnet
- EXPO: Graph mit Triggerposition auf X und DutyCycle auf Y
- CURVE: gemeinsamer Graph fuer CURVX/CURVE; horizontales Wischen verschiebt X (`20 ... 80 %`), vertikales Wischen Y (`10 ... 90 %`)

### Car Menu (`SELECT CAR`)

- 30 Fahrzeuge in einem zweizeiligen, dreispaltigen Raster (148x104 px, Namen bis 8 Zeichen in Groesse 3). Beim Oeffnen steht die Spalte des aktiven Fahrzeugs in der Mitte.
- Obere Reihe: horizontales Ziehen scrollt dem Finger folgend; untere Reihe: horizontale Position springt absolut durch die Liste. Beides rastet auf Spalten ein.
- Tap: Fahrzeug waehlen und (nach Ablauf des Doppeltipp-Fensters) zur aufrufenden Seite zurueck
- Doppeltipp oder `EDIT`: Fahrzeug-Optionen; `BACK`: zurueck ohne Aenderung

### Fahrzeug-Optionen (`CAR`)

Zeigt den Fahrzeugnamen und vier Buttons:

- `RENAME`: Namens-Editor. Der Name wird in 8 Zeichen-Slots mit invertiertem Cursor dargestellt; `<`/`>` bewegen den Cursor. Zwei horizontal scrollbare Zeichenreihen (Buchstaben A-Z/a-z; Ziffern, Leerzeichen, Sonderzeichen). Tap auf ein Zeichen ersetzt das Zeichen am Cursor, schneidet den Rest ab und rueckt den Cursor weiter. `SAVE` uebernimmt, `BACK` verwirft.
- `COPY`: Ziel im Fahrzeugraster waehlen (`COPY <Name> TO`), dann Bestaetigungsseite mit `FROM`/`TO`, Checkboxen `WITH NAME` (Name mitkopieren) und `GOTO CAR` (Ziel anschliessend aktivieren). `SAVE` kopiert alle Einstellungen inkl. CURVE-X.
- `RESTORE`: wie COPY in Gegenrichtung: Quelle im Raster waehlen (`RESTORE <Name>`), Bestaetigung, `SAVE` kopiert die Quelle in das aktive Fahrzeug (optional mit Name).
- `DEFAULTS`: Sicherheitsabfrage (`CANCEL`/`RESET`); setzt die Einstellungen des Fahrzeugs auf die Startwerte, der Name bleibt.

### Config

- Karten fuer globale Optionen, editierbar ueber den normalen Edit-Screen:
  - `DBLTAP`: Doppeltipp-Fenster in ms (200 ... 1000, Standard 400)
- `HELP`: zweiseitige Kurzhilfe aller Gesten mit Symbolen (Wischen horizontal/vertikal, Tap, Doppeltipp, Button)
- `DEFAULTS`: setzt die Einstellungen aller 30 Fahrzeuge zurueck (Namen bleiben) - mit zweistufiger Sicherheitsabfrage (`WARNING` -> `NEXT`, dann `CONFIRM` -> `RESET`)

## Fahrzeugprofile und Speicherung

- Jedes der 30 Fahrzeuge hat eigene Werte fuer alle Einstellungen (`BRAKE`, `SENSI`, `EXPO`, `ANTIS`, `DRAGB`, `WIRE`, `WLIM`, `CURVE`, `PWM_F`, `LIMIT`, `BYPAS`) plus CURVE-X und einen Namen (max. 8 Zeichen).
- Die Startwerte stehen in der `settings[]`-Tabelle in `display_unit.cpp`; beim ersten Start werden alle Fahrzeuge damit initialisiert.
- Persistenz im NVS (`Preferences`, Namespace `espeed`): pro Fahrzeug ein Blob `car00` ... `car29`, dazu `sel` (aktives Fahrzeug), `DBLTAP`, `slot0` ... `slot2` (Kartenbelegung) und `ver` (Layout-Version, aktuell 1; beim Aendern der Blob-Struktur hochzaehlen, dann wird auf Startwerte zurueckgesetzt).
- Geschrieben wird entkoppelt 2 s nach dem letzten Loslassen und nur fuer geaenderte Fahrzeuge/Werte (schont den Flash).

## Betriebsspannung

`VCC` im Rennbildschirm wird ueber BAT_ADC (GPIO 4, Teiler 200K/100K, Faktor 3) mit `analogReadMilliVolts` gemessen (8 Samples, alle 500 ms). Am USB liegen ca. 4.9 V an, am Akku 3.3 ... 4.2 V.

Hinweise zur Hardware (aus dem Waveshare-Schaltplan): Der Akku wird ueber einen P-MOSFET (AO3401) automatisch zugeschaltet, sobald kein USB anliegt; der Buck-Regler MP1605 ist immer aktiv (EN fest an VIN), der Lader ETA6098 hat keinen Steuerpin. Ein Ein-/Ausschalten des Akkus per Software ist nicht moeglich, und es gibt keinen Tiefentladeschutz auf dem Board - der Akku sollte eine eigene Schutzschaltung haben; eine Low-Battery-Abschaltung per Software (Deep Sleep) ist noch nicht implementiert.

## Darstellung

Alle Farben sind als RGB565-Palette in `display_unit.cpp` gebuendelt (`COLOR_*`): schwarzer Hintergrund, deutlich abgesetzte Panels und Rahmen, gelbe Labels, weisse Werte, Cyan/Orange als Akzente, Rot fuer `CANCEL`.

Schriftgroessen (GFX `setTextSize`, nur ganzzahlig, 6x8 px pro Stufe): Ueberschriften 3, Titelzeilen-Buttons 2, Settings-Karten Label 3 / Wert 4, Fahrzeugnamen 3 (Raster) bzw. 4 (Editor), Edit-Wert 8. Nur vollstaendig sichtbare Karten werden gezeichnet, da Arduino_GFX negative X-Koordinaten nicht sauber clippt. Die C/C++-IntelliSense ist fuer diesen Workspace in `.vscode/settings.json` aktiviert, damit die Outline-Ansicht funktioniert.

## EXPO und CURVE

Die EXPO-Darstellung verwendet die Berechnungsfolge aus dem alten ESPEED32-Projekt. EXPO liegt im Bereich `-100 ... 100`, wobei `0` eine gerade Kennlinie ist.

Die CURVE-Darstellung nutzt die alte zweigeteilte Kennlinie mit Vertex:

- X/`CURVX`: `20 ... 80 %` Triggerposition
- Y/`CURVE`: `10 ... 90 %` Kurvenwert
- `SENSI` und `LIMIT` begrenzen die Kennlinie

## Aktueller Entwicklungsstand

Anzeige, Touch-Gesten, Menunavigation, Fahrzeugprofile (30 Stueck) und Persistenz im NVS sind funktionsfaehig. Offen:

- `TRIGGER` ist noch ein Platzhalter; die Bahnspannung (`BAHN V`) kann noch nicht gemessen werden und wurde durch `VCC` ersetzt
- Anbindung der Einstellwerte an die ESC-/Regler-Logik
- Low-Battery-Warnung/Abschaltung (Deep Sleep) auf Basis von `VCC`

Alle Konfigurationsdaten koennen ueber `Config -> DEFAULTS` (zweistufige Abfrage) zurueckgesetzt werden; alternativ `PROFILE_VERSION` in `display_unit.cpp` hochzaehlen.

## GitHub

Privates Repository: https://github.com/walsch/ESPEED_TOUCH_164

Der lokale Branch `main` verfolgt `origin/main`.
