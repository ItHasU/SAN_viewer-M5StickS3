# Matériel — M5StickS3

Recherche effectuée le 2026-10-23. Les informations proviennent de la doc officielle M5Stack et du code
source de M5Unified/M5GFX (version résolue par PlatformIO : M5Unified 0.2.27, M5GFX 0.2.34, qui
connaissent `board_M5StickS3`). **Aucune vérification sur carte physique n'a pu être faite.**

## Caractéristiques

| Élément | Valeur | Source |
|---|---|---|
| SoC | ESP32-S3-PICO-1-N8R8, Xtensa LX7 double coeur 240 MHz | [M5Stack docs](https://docs.m5stack.com/en/core/StickS3) |
| Mémoire | 8 Mo de flash, 8 Mo de PSRAM octale (`qio_opi`) | idem |
| Écran | 1,14", 135x240, ST7789P3 (SPI : MOSI G39, SCK G40, RS G45, CS G41, RST G21, BL G38) | idem |
| Codec audio | ES8311 (I2C 0x18 ; I2S : MCLK G18, BCLK G17, LRCK G15, DOUT G14, DIN G16) | idem + `M5Unified.inl` |
| Micro | MEMS (SNR 65 dB) relié à l'ES8311 | [fiche K150](https://docs.m5stack.com/en/products/sku/K150) |
| Haut-parleur | Ampli AW8737, HP 8 ohm / 1 W ; l'ampli est piloté par une GPIO du PMIC (PYG3) | idem |
| Boutons | KEY1 = G11 (BtnA, face avant), KEY2 = G12 (BtnB, côté) ; bouton latéral reset/power relié au PMIC (simple clic : reset/allumage, double clic : extinction, maintenu : mode téléchargement) | M5Stack docs, `M5Unified.inl` |
| PMIC | M5PM1 (I2C 0x6E, SDA G47 / SCL G48, partagé avec l'IMU) : charge batterie, rails, ampli HP, ... | M5Stack docs ; [Nordic NCS](https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/boards/m5stack/m5stack_sticks3/doc/index.html) (tiers) |
| Batterie | LiPo 250 mAh | M5Stack docs |
| IMU | BMI270 | idem |
| IR | émetteur G46, récepteur G42 | idem |
| LED | **Aucune LED utilisateur documentée** (seulement une LED interne verte en mode téléchargement, sans GPIO) | idem |
| Conso (4,2 V) | arrêt 14 µA ; L1 52 µA ; L2 102 µA ; actif 37 mA ; charge max 519 mA | idem |

Recommandation M5Stack : volume HP < 75 % sur batterie (reboots sinon) -> le firmware utilise 160/255.

## Configuration PlatformIO retenue

Reprise de la doc M5Stack : `platform = espressif32@6.12.0` (Arduino-ESP32 2.0.17), `board = esp32-s3-devkitc-1`,
`board_build.arduino.memory_type = qio_opi`, partition `default_8MB.csv`, `-DBOARD_HAS_PSRAM`,
USB-CDC au boot. Le flag `-mfix-esp32-psram-cache-issue` de la doc est inutile sur S3 et omis.
M5Unified détecte automatiquement la carte (`M5.getBoard() == board_M5StickS3`), initialise l'ES8311 et
l'ampli via le PMIC (`_speaker_enabled_cb_sticks3`).

## Impact sur les capacités déclarées

Document déclaré (`buildCapabilitiesJson`, `lib/san_core`) :

```json
{"protocol_version":1,"firmware":"san-m5sticks3 0.1.0",
 "display":{"width":135,"height":240,"color":true,"max_detail_chars":80},
 "audio_out":{"formats":["ima_adpcm","pcm_s16le"],"sample_rates":[8000,16000]},
 "audio_in":{"formats":["pcm_s16le"],"sample_rate":16000,"max_duration_s":10},
 "nudge":["sound","screen"],"input":["buttons"],"battery":true}
```

Écarts avec DESIGN.md section 7 :

- `audio_out.formats` ajoute `ima_adpcm` : le décodeur (`san_audio.cpp`, WAV 0x11 mono) est implémenté et testé
  sur hôte (aller-retour avec un encodeur de test). Le serveur préférera donc ADPCM (4x moins d'octets à
  télécharger, donc moins de temps Wi-Fi éveillé). **Non validé sur carte ni avec la sortie réelle de ffmpeg
  (`adpcm_ima_wav`)** : si problème, retirer `ima_adpcm` dans `buildCapabilitiesJson` (le hash change seul).
- Pas de `led` (aucune LED utilisateur) ; `screen` à la place, comme dans DESIGN.md.
- Enregistrement : PCM 16 kHz, 10 s max = 320 Ko en PSRAM, envoyé en WAV PCM s16le (pas d'encodeur ADPCM).

## Points non vérifiés / à mesurer

- Consommation réelle en deep sleep : le rail de l'écran/codec dépend du PMIC ; l'écran est mis en veille
  (`Display.sleep()`) mais la mesure en µA reste à faire. Si trop élevée, piloter les rails via M5PM1.
- Réveil par bouton : `ext1` sur G11/G12 (RTC IO, niveau bas). À valider sur carte (les deux GPIO sont des
  RTC IO sur S3, résistances de tirage externes supposées).
- Gain du micro : `M5.Mic` par défaut ; un gain logiciel peut être nécessaire.
