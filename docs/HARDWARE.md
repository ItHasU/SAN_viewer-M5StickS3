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

Recommandation M5Stack : volume HP < 75 % sur batterie (reboots sinon) -> le firmware utilise 230/255 (~90 %, volontairement plus fort ; a surveiller : reboot sur batterie ?).

## Coupure audio hors lecture (grésillement au repos)

Symptôme rapporté : souffle/crépitement quand rien n'est joué, carte éveillée.

Ce que fait réellement M5Unified (`M5Unified.inl`, `_speaker_enabled_cb_sticks3`, ~l.633-665) : `Speaker.begin()` allume
l'ampli (PMIC M5PM1, reg 0x11 bit 3 = GPIO3) puis configure l'ES8311 par I2C ; `Speaker.end()` (`Speaker_Class.inl`
`_end_locked`) rappelle le callback avec `false`, qui ne fait **que** couper la broche d'ampli, puis désinstalle l'I2S.
Le DAC de l'ES8311 n'est jamais mis hors tension (seul le callback micro, `_microphone_enabled_cb_sticks3`, écrit
0x0D=0xFC / 0x0E=0x6A / 0x00=0x00). Le codec peut en outre rester actif après un reveil du deep sleep (rail permanent).
`Speaker.setVolume()` / `stop()` n'appellent jamais `begin()` ; seuls `tone()` / `playRaw()` le font (paresseusement).

Causes probables : (1) ampli resté allumé entre la fin d'un son et le prochain `housekeeping()`, notamment pendant les
attentes bloquantes (connexion Wi-Fi, réseau) ou après l'aperçu sonore du volume ; (2) DAC/codec laissé actif (bruit
de fond, ampli allumé ou non).

Corrections (`src/audio_io.cpp`) :
- `speakerOff()` : `stop()`, volume maître 0 (pas de clic), `Speaker.end()` (ampli coupé), puis mise hors tension de
  l'ES8311 par I2C (0x12=0x02, 0x0D=0xFC, 0x0E=0x6A, 0x00=0x00, valeurs reprises de M5Unified).
- `quiesce()` : même séquence sans condition, appelée au boot (après `M5.begin`) et avant le deep sleep.
- Le haut-parleur n'est démarré qu'à la demande (`speakerBegin()` avant un son) ; `setVolume()` mémorise seulement.
- `sysTick()` (attente Wi-Fi) appelle `housekeeping()` pour couper l'ampli dès la fin d'un son.
- Le micro reste exclusif du haut-parleur : `recordWav` appelle `speakerOff()` puis `Mic.begin()` (qui refait le reset codec).

Limites / à valider à l'oreille : (a) disparition du grésillement carte éveillée au repos ; (b) absence de clic à
l'ouverture/fermeture de l'ampli ; (c) premier son après repos (ouverture codec) non tronqué ; (d) enregistrement micro
toujours correct après un son ; (e) un son déclenché pendant un appel HTTP bloquant garde l'ampli allumé jusqu'au retour
à la boucle (aucun `housekeeping()` dans `esp_http_client`). Non vérifié sur carte : l'effet réel du power-down I2C.

## Sifflement carte éveillée : PWM du rétroéclairage

Cause : M5GFX pilote le rétroéclairage du StickS3 en PWM LEDC sur GPIO38 (`M5GFX.cpp`, `_set_pwm_backlight(GPIO_NUM_38, 7, 256, false, 16)` :
canal 7 = timer 3, **256 Hz**, offset 16 ; `Light_PWM.inl` impose 9 bits). Un découpage à 256 Hz du driver de rétroéclairage
(bobine/condensateurs) est audible ; il disparaît en veille profonde car le PWM s'arrête. Le PMIC M5PM1 ne gère que
l'alimentation de l'écran (GPIO2), pas la luminosité.

Solution : `ui::begin()` (et `backlightOn()`) appelle `ledc_set_freq(LOW_SPEED, timer 3, 25 kHz)` après `M5.begin`. La résolution
9 bits est conservée (80 MHz / 25 kHz = 3200 >= 512), le rapport cyclique n'est pas touché : `setBrightness` (7 niveaux) et
`backlightOff/On` (duty 0 + sleep écran, deep sleep) restent inchangés. Le log série affiche `[san] backlight pwm: set_freq=0, freq=... Hz`.
Aucune modification de la lib. Cette fréquence réelle est lue par `ledc_get_freq` (le diviseur peut l'arrondir légèrement).

À valider sur carte : (a) sifflement disparu écran allumé à chaque niveau de luminosité ; (b) aucun scintillement ni
changement de luminosité perçu (le driver de rétroéclairage doit supporter 25 kHz) ; (c) consommation écran allumé
inchangée (pertes de commutation légèrement plus élevées possibles) ; (d) réveil depuis veille profonde correct.
Repli si le driver n'aime pas 25 kHz : essayer 20-30 kHz, sinon luminosité fixe 100 % (pas de PWM).

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

- Réveil ext1 : `rtc_gpio_init` + entrée seule + pull-up sont désormais explicites avant le deep sleep (à valider).
- État de charge : `M5.Power.isCharging()` (PMIC M5PM1) non vérifié sur carte ; `charge_unknown` n'affiche pas d'éclair.
- Appui long / double clic : seuil 500 ms (`kHoldMs`) ; le clic simple du bouton frontal est décidé après le délai de
  double clic de M5Unified (latence perceptible, à valider).
- Au premier réveil par bouton, l'appui qui réveille la carte est perdu (boot) : c'est normal.

## Résultats de la checklist sur carte réelle (24/10/2026)

Carte M5StickS3 réelle, firmware flashé via `pio run -e m5sticks3 -t upload` (mode téléchargement requis : le port
USB disparaît pendant le deep sleep), serveur SAN en Docker (compose standalone) joint par l'IP LAN en HTTP simple
(champ « URL LAN » du portail).

| # | Point | Résultat |
|---|---|---|
| 1 | Portail Wi-Fi au premier boot (AP `SAN-Viewer-XXXX`, 192.168.4.1), saisie SSID et URL LAN | OK |
| 2 | Register, code d'appairage, activation dans l'admin, passage en RUN | OK (carte `active`, sync toutes les 60 s) |
| 3 | `capabilities_required`, PUT caps, hash identique côté serveur | OK (hash enregistré côté serveur, identique à celui du simulateur) |
| 4 | Règle `*` + `can_send_voice`, état/nudge/say depuis `bin/san` : écran, son, clip ADPCM 16 kHz | OK : texte lisible, voix intelligible ; volume jugé trop bas à 160, relevé à 230 ; **nudge `question` déclenché par un vrai hook Claude Code : son et icône OK** |
| 5 | Enregistrement (appui long frontal) vers l'agent | Non testé |
| 6 | Menu abonnements (double clic frontal puis Agents), 409 | Non testé |
| 7 | Deep sleep : réveil timer et bouton, consommation | Réveil timer OK (cycle 60 s) ; réveil bouton, consommation : non testés |
| 8 | Révocation dans l'admin, 401, effacement du jeton | Non testé |
| 9 | Batterie remontée dans l'admin | OK (81 %, puis 99 % après charge) |

Évolution suivante (voir README, « Boutons ») : vue principale à icônes (notifications en haut, activité et statut en bas), file de notifications, menu de réglages (volume, lumière, pause, agents), batterie avec éclair de charge.

Retours d'usage intégrés : écran en paysage (rotation 1, bouton latéral B sur le dessus, bouton frontal A sur la face avant ; affectation modifiable dans `src/config.h`), police x2, pictogrammes plutôt
que texte, sons distincts par type de nudge (`start`, `notification`, `question`, `done`, `error`), animation de
calcul sans pourcentage. À valider à l'usage : les 4 autres types de nudge (rendu et sons), le volume 230 sur
batterie (risque de redémarrage), la fluidité de l'animation.

À valider sur la carte : lignes de connexion « Wi-Fi... » / « Serveur... » (pulsation, lisibilité), « Wi-Fi OK » après un échec, son d'erreur au 2e échec consécutif réveil timer hors couverture.
