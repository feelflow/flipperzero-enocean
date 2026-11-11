# Analyse der ersten Aufnahme

Datei: [`captures/tracker_press_x3_ook270.sub`](../captures/tracker_press_x3_ook270.sub)
Aufgenommen mit der Standard-Sub-GHz-App (Read RAW), 868,35 MHz, Preset `Ook270Async`, Momentum.

## Ergebnis

- Die Aufnahme enthält **drei Gruppen zu je drei kurzen Bursts**. Das passt zu drei Tastendrücken
  mit je drei EnOcean-Subtelegrammen.
- Innerhalb einer Gruppe variieren die Abstände zufällig, zwischen den Gruppen liegen etwa 80–90 ms.
- Die Bursts sind nur 0,4–0,8 ms lang und bestehen aus Pulsen von 66 bis ca. 440 µs. Ein echtes
  ERP1-Telegramm hat Pulse im Raster von 8 µs. Die Bit-Information ist in dieser Aufnahme **nicht**
  mehr vorhanden.
- Zusätzlich zeigt die Datei ein periodisches Signal mit Pulsen um 650–700 µs und Pausen von
  ca. 3,3 ms, 30 ms und 110–120 ms. Das ist ein anderer Sender im Band und kein EnOcean.

## Burst-Zeitleiste (µs, Vorzeichen: + high, - low)

| Gruppe | Burst 1 | Pause | Burst 2 | Pause | Burst 3 | Pause danach |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 134 -98 198 -66 136 | 2298 | 170 -100 414 | 31282 | 162 -130 344 | 92240 |
| 2 | 162 -96 404 | 6274 | 196 -96 400 | 23246 | 202 -132 442 | 81768 |
| 3 | 128 -128 314 | 1454 | 66 -132 200 -68 102 | 29328 | 100 -132 202 -68 | (Ende der Datei) |

## Ursache

Die Standard-Presets des Flipper stellen den CC1101 auf ca. 3,8 kBaud Datenrate. Der Daten-Filter im
Demodulator verschmiert 8-µs-Bits zu einer Hüllkurve. Zusätzlich liegt die kleinste
vorkommende Pulslänge in der Datei bei etwa 66 µs, es gibt also ein grobes Zeitraster. Damit lassen
sich weder ID noch Daten rekonstruieren, und ein Replay der Rohdaten erzeugt kein gültiges Telegramm.

## Konsequenz

Phase 1 (`enocean_rx.c`) lädt ein eigenes Preset mit 125 kBaud und 650 kHz Bandbreite und liest die
Flanken direkt aus der HAL. Ob die Auflösung dann reicht, zeigt der erste Test auf dem Gerät.

---

# Erste Messung mit der Phase-1-App (11.11.2026)

Datei: [`captures/phase1_bursts_3_presses.sub.txt`](../captures/phase1_bursts_3_presses.sub.txt),
Preset "650k" (650 kHz, ca. 125 kBaud), 3 Tastendrücke am Tracker.

| Burst | Flanken | Gesamtdauer | Pulse in 8-µs-Einheiten (Vorzeichen = Pegel) |
| --- | --- | --- | --- |
| 42 | 11 | 746 µs | +2 -10 +14 -17 +19 -2 +2 -4 +16 -4 +4 |
| 43 | 11 | 816 µs | +11 -8 +6 -4 +6 -17 +19 -2 +22 -4 +4 |

## Befund

- **Das 8-µs-Raster ist aufgelöst.** Alle 22 Pulse liegen innerhalb von ±3 µs auf einem Vielfachen
  von 8 µs. Das ist die Bitdauer von ERP1 (125 kbit/s). Damit ist die Kernfrage von Phase 1
  beantwortet: Der CC1101 im Flipper liefert die Bit-Zeitstruktur.
- **Die Burstlänge passt.** 746 und 816 µs entsprechen etwa 93 bis 102 Bitzeiten, das ist die
  Größenordnung eines kurzen ERP1-Telegramms (z. B. Taster).
- **Es fehlen kurze Pulse.** Der kleinste Puls ist 15 µs (2 Bitzeiten), Einzel-Bit-Pulse (8 µs)
  kommen nicht vor. Gleichzeitig gibt es lange Pegel von 14 bis 22 Bitzeiten. Das spricht dafür,
  dass schnelle Wechsel vom Empfänger verschluckt werden (Datenfilter, DC-Filter,
  Schwellenwert), die Bits also noch nicht zuverlässig rekonstruierbar sind.
- Von den erwarteten 9 Bursts (3 Tastendrücke mal 3 Subtelegramme) enthält die Datei nur zwei.
  Der Zähler stand bei 43, die Datei zeigt nur die Bursts 42 und 43.

## Nächster Schritt

Version 0.2 der App hat fünf Varianten der CC1101-Einstellungen, die man am Gerät mit Links/Rechts
durchschaltet (Bandbreite 650/812 kHz, DC-Filter an/aus, 125/250 kBaud). Der Log-Header nennt die
verwendete Variante. Ziel: eine Variante finden, bei der Einzel-Bit-Pulse (8 µs) auftauchen und
alle drei Subtelegramme pro Tastendruck ankommen.
