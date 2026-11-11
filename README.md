# flipperzero-enocean

Experimentelle Flipper-Zero-App zum Empfangen (und später Senden) von **EnOcean ERP1**-Telegrammen
(868,3 MHz, ASK, 125 kbit/s) mit dem eingebauten CC1101.

> Nur für eigene Geräte und eigene Netze. Dieses Projekt ist ein Lern- und Smart-Home-Projekt
> (Home Assistant), kein Werkzeug für fremde Anlagen.

## Warum eine eigene App?

Die Standard-Sub-GHz-App nutzt Presets mit ca. 3,8 kBaud Datenrate. EnOcean sendet mit 125 kbit/s
(ca. 8 µs pro Bit). Die Bits werden dadurch zu 400–800 µs langen "Klumpen" verschmiert. Eine
Aufnahme mit Read RAW zeigt zwar, **dass** ein Taster gesendet hat (drei Wiederholungen pro
Tastendruck), aber nicht **was**. Details: [docs/capture-analysis.md](docs/capture-analysis.md).

## Plan

| Phase | Inhalt | Status |
| --- | --- | --- |
| 1 | CC1101 mit eigenem Preset (125 kBaud, 650 kHz, OOK) konfigurieren, rohe Flanken direkt aus der HAL lesen, Pulsstatistik anzeigen und Bursts auf SD loggen | Code geschrieben, **ungetestet** |
| 2 | Bitstrom rekonstruieren: Preamble/SOF finden, 8/12-Dekodierung, CRC8, Sender-ID, RORG und Daten anzeigen | offen |
| 3 | Telegramm mit eigener Sender-ID erzeugen und senden (Teach-in am Aktor) | offen |
| 4 | Optional: Weitergabe an Home Assistant (WiFi-Devboard, MQTT/Webhook) | offen |

Phase 1 beantwortet die Kernfrage: Löst der CC1101 im Flipper das 125-kbit/s-ASK-Signal überhaupt
auf? Wenn in `bursts.sub.txt` Pulse in einem Raster von etwa 8 µs auftauchen, lohnt sich Phase 2.
Wenn nicht, ist ein EnOcean-USB-Stick (USB300) bzw. EnOcean Pi der zuverlässigere Weg.

## Hardware

- Flipper Zero (Firmware: Momentum)
- EnOcean-Taster/-Tracker (z. B. Eltako FTTB) als Testquelle
- Optional: externes CC1101-Modul mit 868-MHz-Antenne (bessere Reichweite)

## Bauen

Voraussetzung: [`ufbt`](https://pypi.org/project/ufbt/). Da das Gerät Momentum nutzt, sollte gegen
das passende SDK gebaut werden (siehe Momentum-Dokumentation für `ufbt update` mit deren Index).

```sh
pip install ufbt
ufbt update        # SDK passend zur Firmware auf dem Flipper wählen
ufbt               # baut dist/enocean_rx.fap
ufbt launch        # baut, lädt hoch und startet auf dem angeschlossenen Flipper
```

## Benutzung (Phase 1)

1. App starten: *Apps → Sub-GHz → EnOcean RX Test*.
2. EnOcean-Taster in wenigen Zentimetern Abstand drücken.
3. Anzeige: Anzahl Bursts, Flanken pro Burst, Länge, kürzester/längster Puls, Pulse in drei
   Längenklassen (≤12 µs, 13–28 µs, >28 µs), RSSI.
4. `OK` schaltet das Logging ein/aus. Log: `/ext/apps_data/enocean_rx/bursts.sub.txt` im
   RAW_Data-Format (`+` = high, `-` = low, Mikrosekunden).
5. Die Datei bitte hochladen bzw. in `captures/` committen, dann kann sie ausgewertet werden.

## Bekannte Risiken

- Der CC1101 hat bei schnellem OOK eine begrenzte Empfangsleistung (ein TI-Entwickler nennt für
  einen verwandten Chip 10–20 kbit/s im Empfang, Senden mit 125 kbit/s ist möglich). Ob es hier
  reicht, ist offen.
- Bei Rauschen kann der Flankenstrom die CPU stark belasten. Der Zähler `ovr` zeigt verlorene
  Flanken.
- Der Antennenpfad des Flipper ist auf 433 MHz optimiert, bei 868 MHz ist die Reichweite kleiner.
- Die CC1101-Registerwerte sind aus den Standard-OOK-Presets abgeleitet (nur MDMCFG4/3 geändert)
  und nicht auf Hardware geprüft.

## EnOcean ERP1: Eckdaten

- Trägerfrequenz 868,3 MHz, Modulation ASK, Datenrate 125 kbit/s, Bitdauer ca. 8 µs
- Kodierung 8/12, Preamble, 4-Bit-SOF
- Telegramm: RORG, Daten, 4-Byte-Sender-ID, Status, Hash/CRC
- Jedes Telegramm wird als drei Subtelegramme mit zufälligem Abstand gesendet

Quelle: [EnOcean Radio Protocol 1](https://www.enocean.com/wp-content/uploads/Knowledge-Base/EnOceanRadioProtocol1.pdf)

## Verzeichnis

```
application.fam      App-Manifest
enocean_rx.c         Phase-1-App
captures/            Beispielaufnahmen
docs/                Analysen und Notizen
```
