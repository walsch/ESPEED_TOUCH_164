# ESPEED Touch 1.64

Touchscreen-Oberflaeche fuer das ESP32-S3 Touch AMOLED 1.64.

## Hardware

- ESP32-S3 Touch AMOLED 1.64
- SH8601 AMOLED, 280 x 456 Pixel
- QSPI: CS 9, CLK 10, D0 11, D1 12, D2 13, D3 14
- Display Reset: GPIO 21
- FT3168 Touch: SDA 47, SCL 48, I2C-Adresse `0x38`
- TLE493D Trigger-Sensor: SDA 8, SCL 15 auf einem separaten I2C-Bus; A0/P3-Autodetektion
- BTN9990LV-Halbbruecke (Leistungsplatine): IN/PWM GPIO 5, INH GPIO 7; Startzustand ist Lowside-Bremse
- Strommessung: TLE4971 ueber U8 an GPIO 1 (1,25 V + 20 mV/A), Anzeige im Rennbildschirm unter `CURRENT`
- Bahnspannung: `VIN` ueber GPIO 2 und den Referenzteiler 15 kOhm / 2,2 kOhm; keinen direkten Bahnspannungsanschluss an den GPIO
- Darstellung: Landscape, beim Flush per Software gedreht

Alle Board-Details (Stromversorgung, Ladeschaltung, GPIO-Belegung, Stiftleisten, TF-Karte, IMU) stehen in [docs/HARDWARE.md](docs/HARDWARE.md); der Waveshare-Schaltplan liegt als Kopie unter `docs/`.

## Software

- Arduino IDE/CLI
- ESP32 Arduino Core 3.x
- GFX Library for Arduino 1.6.8
- LVGL 9.6.0
- Arduino_GFX SH8601 fuer QSPI-Panelzugriff (80 MHz)

Kompilieren und Upload:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:PSRAM=opi .
arduino-cli upload -p /dev/cu.usbmodem1101 --fqbn esp32:esp32:esp32s3:PSRAM=opi .
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
- Statusleiste: `TRIG` (kalibrierter Wert 0 ... 100 %), `CURR` (gemittelter Motorstrom), `BEMF` (Gegen-EMK), `PMECH` (Wellenleistung ≈ BEMF × Strom, beim Bremsen negativ), `PEL` (elektrische Motorleistung ≈ VIN × Duty × Strom beim Fahren, sonst 0) und `VIN` (Bahnspannung).
- Motorstatus im Kopf: `NO VIN`, `NO SENSOR`, `CAL REQUIRED`, `RELEASE TRIGGER`, `BRAKE`, `DRIVE` oder `BTN ERROR` zeigt den aktuellen Freigabezustand.
- Motor: Trigger 0 % bremst mit `BRAKE` nur ueber Lowside; oberhalb des Deadbands wird nur die Highside mit `SENSI` als Startwert und linear bis 100 % getaktet.
- `EXPO` (-100 ... 100, 0 = linear) formt den Triggerweg vor der Abbildung auf `SENSI` ... 100 % wie die Referenz (Expo-Typ 0, exponentiell): positive Werte geben frueher mehr Gas, negative spaeter.
- `DRAGB` (0 ... 100 %): Schleppbremse; IN = Gas, INH = Gas + DRAGB, sodass die Lowside in der PWM-Pause bremst. Modus je Fahrzeug ueber die Taste im Kopf des DRAGB-Editors: `DECL` (Standard wie Referenz, nur waehrend der Trigger zurueckgenommen wird) oder `CONT` (immer beim Fahren). Bei `CONT` zeigt die DRAGB-Karte im Settings-Menue eine Markierung `CONT`.
- `WIRE` (0,0 ... 25,0 ft, 7 mOhm/ft wie Referenz) / `WLIM` (Standard 99 %): simuliert einen Widerstand in der Fahrleitung. Vom Tastgrad wird I x R / VIN abgezogen, mit I = gemessener Motorstrom (10 ms gefiltert); die Referenz verwendet stattdessen den Bahnstrom (Motorstrom x Tastgrad) und wirkt dadurch bei Teilgas schwaecher. Aktiv nur, solange der Trigger hoechstens WLIM betraegt. WLIM hat keine eigene Karte: Die WIRE-Karte zeigt `WLIM xx%`, und im WIRE-Editor schaltet die Kopfzeilen-Taste `WLIM xx%` die Regler zwischen WIRE und WLIM um (`CANCEL` setzt beide zurueck).
- `LIMIT` (OFF, 0,1 ... 20,0 V, Standard OFF): maximale Fahrspannung wie Referenz-Spannungsregelung. Der Tastgrad nach SENSI/EXPO wird jeden 1-ms-Takt mit LIMIT / VIN skaliert, sodass die mittlere Motorspannung bei Vollgas LIMIT betraegt, unabhaengig von der Bahnspannung. Liegt VIN unter LIMIT, wirkt LIMIT nicht; ohne gueltige VIN gibt es bei aktivem LIMIT kein Gas. WIRE wird danach abgezogen.
- `BYPAS` (`OFF` / `BTN` / `FULL` je Fahrzeug, Standard OFF): "alles was geht". Bei voll durchgezogenem Trigger werden Kennlinie und LIMIT umgangen (100 % ueber den BTN). Mit `FULL` wird zusaetzlich der Vollgaskontakt FULL der Leistungsplatine (GPIO 43, LOW = geschlossen, wie Referenz) geschlossen. Schliessen nur bei Ausgang 100 %, Motorspannung >= 75 % VIN (5 ms stabil) und Strom < 40 A (4 Takte); Oeffnen sofort beim Verlassen von 100 %, bei Ueberstrom (2 Takte > 50 A oder > 70 A, Sperre bis der Trigger 100 % verlaesst), Einbruch der Motorspannung oder Ausfall des Stromsensors. Ein Watchdog oeffnet den Kontakt, wenn die Regelung > 5 ms haengt; vor jedem Flash-Schreiben wird er geoeffnet. Nach dem Oeffnen bleibt die Lowside 15 ms gesperrt (Bremse und DRAGB; die Bruecke ist solange aus, der Motor rollt). Der Statuskopf zeigt `FULL`, sobald der Kontakt geschlossen ist (nach 15 ms).
- `BIPOL` (Karte am Ende des Settings-Rasters, eigener Dialog, je Fahrzeug): simuliert einen Transistorregler (idealer NPN-Emitterfolger, konstantes hFE, U_BE 0,7 V, keine Saettigung) mit Motorrueckfuehrung. Schaltung: V_LIM (LIMIT bzw. VIN) - Widerstandsband `BAND` (15 ... 220 Ohm, Schleifer am Trigger) - `R_B` (0 ... 10 Ohm) - Knoten; Knoten - `CHOKE` (0 ... 10 Ohm) - Basis; Knoten - `SENSI` (10 ... 35 Ohm) - Emitter = Motor; Kollektor an VIN. Daraus Spannungsquelle U = U0 - R_out x I, aufgeloest mit MOTOR R und BEMF (kein Stromregelkreis). Trigger 0 = BRAKE wie bisher; Vollgas mit `BYPAS` = Relais. Ersetzt SENSI/EXPO/LIMIT-Skalierung; WIRE und DRAGB wirken danach. Ohne gemessenes MOTOR R bleibt die normale Kennlinie aktiv (`NEED R` auf der Karte).
- `TORQ` (OFF, 0,1 ... 20,0 A, Standard OFF, je Fahrzeug): Momentenregelung. Der Trigger (nach EXPO/SENSI) gibt den Motorstrom vor, Vollgas = TORQ. Vorsteuerung U = BEMF + I x MOTOR R, ein PI-Regler (2 kHz, Verstaerkung mit VIN / MOTOR R normiert) korrigiert ueber den Stromsensor. LIMIT begrenzt die Spannung, WIRE nur die verfuegbare Spannung (I x R_Kabel); BYPAS bei Vollgas hat Vorrang, BIPOL schliesst TORQ aus. Benoetigt MOTOR R (`NEED R`). TORQ unter dem Leerlaufstrom des Motors laesst ihn nicht anlaufen.
- `BRKRL` (OFF, 0,1 ... 6,0 V, Standard OFF, je Fahrzeug, wie Referenz): Bremse loesen. Trigger 0 bremst voll, bis die BEMF auf BRKRL gefallen ist, dann rollt der Motor frei; bis der BEMF-Nullpunkt gelernt ist, wird voll gebremst. Waehrend der Motor dreht (> 0,2 V), wird die BEMF alle 2 ms statt 10 ms gemessen. Ersetzt BRAKE (Hinweis `BRKRL` auf der BRAKE-Karte).
- `PWM_F` / `BRK_F` (0,2 ... 50 kHz in 0,1-kHz-Schritten, Standard 30 kHz / 0,2 kHz wie Referenz): PWM-Frequenz fuer Fahren bzw. Bremsen. Eine niedrige Bremsfrequenz verringert die Rueckspeisung in Netzteile, die keinen Strom aufnehmen koennen. Die Periode wechselt jeweils am PWM-Periodenende.
- Die Motorsteuerung laeuft auf allen Seiten; geaenderte Werte wirken sofort. Nur waehrend der Triggerkalibrierung wird mit `BRAKE` gebremst (`CALIBRATING`).
- Freigabe: VIN muss mindestens 5,5 V betragen, Triggerkalibrierung muss gueltig sein und nach Start/VIN-Ausfall muss zuerst die Bremsposition erkannt werden. Bei VIN-Verlust fordert die Firmware Lowside-Bremse an.
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

Zeigt den Fahrzeugnamen und sechs Buttons (`MOTOR R` und `MOTOR KV` zeigen den gespeicherten Wert):

- `RENAME`: Namens-Editor. Der Name wird in 8 Zeichen-Slots mit invertiertem Cursor dargestellt; `<`/`>` bewegen den Cursor. Zwei horizontal scrollbare Zeichenreihen (Buchstaben A-Z/a-z; Ziffern, Leerzeichen, Sonderzeichen). Tap auf ein Zeichen ersetzt das Zeichen am Cursor, schneidet den Rest ab und rueckt den Cursor weiter. `SAVE` uebernimmt, `BACK` verwirft.
- `COPY`: Ziel im Fahrzeugraster waehlen (`COPY <Name> TO`), dann Bestaetigungsseite mit `FROM`/`TO`, Checkboxen `WITH NAME` (Name mitkopieren) und `GOTO CAR` (Ziel anschliessend aktivieren). `SAVE` kopiert alle Einstellungen inkl. CURVE-X, MOTOR R und MOTOR KV.
- `RESTORE`: wie COPY in Gegenrichtung: Quelle im Raster waehlen (`RESTORE <Name>`), Bestaetigung, `SAVE` kopiert die Quelle in das aktive Fahrzeug (optional mit Name).
- `MOTOR R`: Innenwiderstand des Motors bei festgehaltenem Anker. `MEASURE` gibt je 300 ms halben und vollen Test-Tastgrad aus (Standard 10/20 %, 10 ... 50 % in 5-%-Schritten) und rechnet R = Delta-Duty x (VIN + 0,7 V) / Delta-I; Schaltverzoegerung, Diodenspannung und Strom-Nullpunkt fallen dabei heraus. Dreht der Motor (BEMF > 0,5 V), wird abgebrochen. `SAVE` speichert den Wert fuer dieses Fahrzeug. Fahren ist auf dieser Seite gesperrt.
- `MOTOR KV`: Motorkonstante (U/min pro Volt). Motor frei mit konstanter Drehzahl laufen lassen, extern gemessene Drehzahl als `SOLL` einstellen (50/500/5000er-Schritte), `SAVE` rechnet KV = Drehzahl / BEMF (BEMF mindestens 1 V). Live: Drehzahl, BEMF, KV und Drehmoment M = 60 / (2 pi KV) x I. Mit gespeichertem KV zeigt die Statusleiste `RPM` statt `BEMF`.
- `DEFAULTS`: Sicherheitsabfrage (`CANCEL`/`RESET`); setzt die Einstellungen des Fahrzeugs auf die Startwerte und loescht MOTOR R und MOTOR KV, der Name bleibt.

### Config

- Karten fuer globale Optionen, editierbar ueber den normalen Edit-Screen:
  - `DBLTAP`: Doppeltipp-Fenster in ms (200 ... 1000, Standard 400)
  - `DEADBAND`: Endbereich je Triggerseite (0 ... 20 %, Standard 5 %); innerhalb davon werden 0 % bzw. 100 % sicher ausgegeben.
- `VIN CORRECTION`: Dialog mit Livewert `IST`, einstellbarem Multimeterwert `SOLL` (10-mV-Schritte), `VINDIV` (15K/10K) und Speichern der Differenz als Korrektur (-500 ... +500 mV).
- `CURRENT CAL`: Livewert `IST`, Referenzstrom `SOLL` (0,01/0,1/1-A-Schritte), `ZERO` misst den Nullpunkt (nur mit losgelassenem Trigger), `SAVE` skaliert und speichert die Verstaerkung (mindestens 0,5 A). Rohspannung und Gain werden zur Diagnose angezeigt.
- `TRIGGER CALIBRATION`: Trigger loslassen und `SET 0%` waehlen, dann voll durchziehen und `SET 100%` waehlen. `SAVE` speichert den Winkelbereich; die Live-Anzeige ordnet ihn auf 0 ... 100 % ab. Gueltiger Stellweg: 15 ... 150 Grad.
- `HELP`: zweiseitige Kurzhilfe aller Gesten mit Symbolen (Wischen horizontal/vertikal, Tap, Doppeltipp, Button)
- `DEFAULTS`: setzt die Einstellungen aller 30 Fahrzeuge zurueck (Namen bleiben) - mit zweistufiger Sicherheitsabfrage (`WARNING` -> `NEXT`, dann `CONFIRM` -> `RESET`)

## Fahrzeugprofile und Speicherung

- Jedes der 30 Fahrzeuge hat eigene Werte fuer alle Einstellungen (`BRAKE`, `SENSI`, `EXPO`, `ANTIS`, `DRAGB`, `WIRE`, `WLIM`, `CURVE`, `PWM_F`, `LIMIT`, `BYPAS`, `BRK_F`, `TORQ`, `BRKRL`) plus CURVE-X und einen Namen (max. 8 Zeichen).
- Die Startwerte stehen in der `settings[]`-Tabelle in `display_unit.cpp`; beim ersten Start werden alle Fahrzeuge damit initialisiert.
- Persistenz im NVS (`Preferences`, Namespace `espeed`): pro Fahrzeug ein Blob `car00` ... `car29`, dazu `sel` (aktives Fahrzeug), `DBLTAP`, `slot0` ... `slot2` (Kartenbelegung) und `ver` (Layout-Version, aktuell 1; beim Aendern der Blob-Struktur hochzaehlen, dann wird auf Startwerte zurueckgesetzt).
- Geschrieben wird entkoppelt 2 s nach dem letzten Loslassen und nur fuer geaenderte Fahrzeuge/Werte (schont den Flash).

## Betriebsspannung

`VCC` wird intern ueber BAT_ADC (GPIO 4, Teiler 200K/100K, Faktor 3) gemessen (alle 500 ms), aber nicht mehr im Rennbildschirm angezeigt. Am USB liegen ca. 4.9 V an, am Akku 3.3 ... 4.2 V.

Alle Analogeingaenge (Strom GPIO 1, VIN GPIO 2, Motorspannung GPIO 3, VCC GPIO 4) werden per DMA kontinuierlich abgetastet (80 kS/s, Motorspannung alle 25 us, Strom alle 50 us); die Regelschleife liest nur die jeweils letzten Werte und laeuft mit 2 kHz, der Trigger wird jede Millisekunde in einem eigenen Task gelesen.

Die Bahnspannung `VIN` wird separat ueber GPIO 2 und einen 15-kOhm/2,2-kOhm-Teiler gemessen (gemittelt, alle 500 ms). Der Teiler muss vorhanden sein; niemals die Bahnspannung direkt an GPIO2 anschliessen.

Hinweise zur Hardware (aus dem Waveshare-Schaltplan): Der Akku wird ueber einen P-MOSFET (AO3401) automatisch zugeschaltet, sobald kein USB anliegt; der Buck-Regler MP1605 ist immer aktiv (EN fest an VIN), der Lader ETA6098 hat keinen Steuerpin und laedt fest mit 2 A. Der `5V`-Pin der Stiftleiste liegt auf demselben Netz wie USB-VBUS, d. h. auch bei Speisung ueber den Header wird der Akku geladen (USB und externe 5 V nicht gleichzeitig anschliessen). Ein Ein-/Ausschalten des Akkus per Software ist nicht moeglich, und es gibt keinen Tiefentladeschutz auf dem Board - der Akku sollte eine eigene Schutzschaltung haben; eine Low-Battery-Abschaltung per Software (Deep Sleep) ist noch nicht implementiert. Details in [docs/HARDWARE.md](docs/HARDWARE.md).

## Darstellung

Alle Menueelemente werden als LVGL-Objekte gerendert. LVGL arbeitet mit RGB565 und einer Landscape-Aufloesung von 456 x 280 im Partial-Modus: Die Zeichenfunktionen verwenden vorhandene LVGL-Objekte in gleicher Reihenfolge wieder und aendern nur abweichende Eigenschaften, sodass nur geaenderte Bereiche gerendert, fuer das physische 280 x 456 Panel gedreht und uebertragen werden. Anzeige und Touch laufen alle 10 ms. Die Touch-Ereignisse werden weiterhin vom FT3168 gepollt und zugleich als LVGL-Pointer bereitgestellt.

LVGL-Konfiguration und aktivierte Montserrat-Schriften liegen in `lv_conf.h`. Die C/C++-IntelliSense ist fuer diesen Workspace in `.vscode/settings.json` aktiviert, damit die Outline-Ansicht funktioniert.

## EXPO und CURVE

Die EXPO-Darstellung verwendet die Berechnungsfolge aus dem alten ESPEED32-Projekt. EXPO liegt im Bereich `-100 ... 100`, wobei `0` eine gerade Kennlinie ist.

Die CURVE-Darstellung nutzt die alte zweigeteilte Kennlinie mit Vertex:

- X/`CURVX`: `20 ... 80 %` Triggerposition
- Y/`CURVE`: `10 ... 90 %` Kurvenwert
- `SENSI` und `LIMIT` begrenzen die Kennlinie

## Aktueller Entwicklungsstand

Anzeige, Touch-Gesten, Menunavigation, Fahrzeugprofile, VIN-Referenzmessung und grundlegende SENSI/BRAKE-Motorsteuerung sind integriert. Offen:

- Triggerwinkel und Bahnspannung werden angezeigt; die VIN-Messung setzt den passenden externen Spannungsteiler voraus
- Lowside-Bremswirkung bei VIN unterhalb der BTN-Unterspannung am Motoraufbau pruefen
- Weitere Fahrzeugparameter (EXPO, CURVE, DRAGB, LIMIT usw.) in die Reglerlogik einbinden
- Low-Battery-Warnung/Abschaltung (Deep Sleep) auf Basis von `VCC`

Alle Konfigurationsdaten koennen ueber `Config -> DEFAULTS` (zweistufige Abfrage) zurueckgesetzt werden; alternativ `PROFILE_VERSION` in `display_unit.cpp` hochzaehlen.

## GitHub

Privates Repository: https://github.com/walsch/ESPEED_TOUCH_164

Der lokale Branch `main` verfolgt `origin/main`.
