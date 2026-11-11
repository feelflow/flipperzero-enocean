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
