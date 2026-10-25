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
| 6 | Menu abonnements (double clic frontal puis Agents), 409 | Supprimé : plus de filtre sur la carte, tous les agents sont affichés |
| 7 | Deep sleep : réveil timer et bouton, consommation | Réveil timer OK (cycle 60 s) ; réveil bouton, consommation : non testés |
| 8 | Révocation dans l'admin, 401, effacement du jeton | Non testé |
| 9 | Batterie remontée dans l'admin | OK (81 %, puis 99 % après charge) |

Évolution suivante (voir README, « Boutons ») : vue principale à icônes (notifications en haut, activité et statut en bas), file de notifications, menu de réglages (volume, lumière, pause), batterie avec éclair de charge.

Ajouts non testés sur carte (à valider) : nouvelle mise en page de la vue principale (sans notification : rangée d'icônes agrandies, rayon 24 px max, + nom de l'agent sélectionné en police 40, seule la ligne de statut en bas ; avec notifications : 3 lignes de 28 px, résumé de 24 px (cloche + total, engrenage animé si agents en activité), statut de 26 px ; plus de scène animée en bas ni de « Pas de message »), sélection au bouton du dessus conservée en RTC (magic RTC 0x53414E05), animation de la rangée (un pictogramme par agent, `working` = engrenage
cyan, autres atténués, « +N » au-delà de 6), menu à 6 lignes (Volume, Lumière, Pause, Config Wi-Fi, Réinitialiser, Sortir ; 5 visibles) avec défilement, **Config Wi-Fi (AP)** (portail WiFiManager
non bloquant lancé depuis le menu, annulable au clic, timeout 240 s, sans redémarrage) et **Réinitialiser** (maintien 3 s
du bouton du dessus, annulable au bouton frontal : efface NVS + RTC puis redémarre en portail ; journal série
`[san] REINITIALISATION`). Écrans de pairing (code seul) et de portail (icône Wi-Fi + nom de l'AP) simplifiés. À vérifier :
fluidité de l'animation, état du Wi-Fi/STA après un portail annulé, aucune consommation excessive en fenêtre d'éveil.

Retours d'usage intégrés : écran en paysage (rotation 1, bouton latéral B sur le dessus, bouton frontal A sur la face avant ; affectation modifiable dans `src/config.h`), police x2, pictogrammes plutôt
que texte, sons distincts par type de nudge (`start`, `notification`, `question`, `done`, `error`), animation de
calcul sans pourcentage. À valider à l'usage : les 4 autres types de nudge (rendu et sons), le volume 230 sur
batterie (risque de redémarrage), la fluidité de l'animation.

À valider sur la carte : lignes de connexion « Wi-Fi... » / « Serveur... » (pulsation, lisibilité), « Wi-Fi OK » après un échec, son d'erreur au 2e échec consécutif réveil timer hors couverture.

## Sifflement écran allumé (non résolu, toléré)

Un léger sifflement est audible carte éveillée (écran allumé) ; il disparaît en deep sleep. Essais faits le 25/10/2026 :
- coupure de l'ampli et du codec ES8311 hors lecture : sans effet sur le sifflement ;
- PWM du rétroéclairage (M5GFX : GPIO38, LEDC canal 7, 256 Hz par défaut) monté à 25 kHz : toujours audible ;
- rétroéclairage fixe à 100 % (rapport cyclique 512/512, aucune commutation) : toujours audible.
Ce n'est donc pas la commutation PWM du rétroéclairage. Pistes non explorées : bruit d'alimentation de l'écran ou du
PMIC (M5PM1), activité du Wi-Fi/CPU couplée à l'ampli. Décision de l'utilisateur : on laisse tomber, le réglage de
luminosité reste inchangé.

## Refonte de l'interface (spec « M5 Stick - Fonctionnement »), non testée sur carte

Remplace la file de notifications, les lignes de notification, le résumé cloche/engrenage et les entrées Wi-Fi/Serveur
éphémères (voir README « Écran » et « Boutons »). Tout est à valider sur la carte.

Décisions sur les points ambigus ou ouverts :

- **Icône** : `idle` et `done` = coche verte ; `waiting` = point d'interrogation ; `error` = point d'exclamation ; `working` =
  engrenage animé ; périmé/inconnu = tiret gris. Seul `states[]` fixe l'icône (le serveur met l'état à jour avec le nudge) ;
  le type du nudge ne sert qu'au son. Un nudge `notification` ne change pas l'état.
- **Changement de statut** : un changement entre deux icônes différentes (idle -> done n'en est pas un). Le premier état
  jamais vu d'un agent (cache vide) ne déclenche ni sélection ni écran.
- **Auto-sélection** : vocal > question > problème > prêt ; à rang égal le dernier agent de la liste ; « réflexion » ne
  change rien ; rien pendant un enregistrement (le drapeau existe, la synchronisation étant de toute façon bloquée pendant
  l'enregistrement).
- **Vocaux** : pastille = `voice_pending` ; clip à lire = premier de `voice_pending_clips` (le plus ancien). Arrivée = clip
  dont l'empreinte 16 bits n'est pas dans la mémoire des 8 derniers clips vus par agent (RTC) ; ainsi un vocal lu ailleurs
  ou plus ancien ne rejoue ni ne resélectionne. Les événements `voice` sont acquittés sans traitement. Le marquage lu
  (`POST .../played`) se fait après chaque lecture (auto ou manuelle), pas pour une simple réécoute. Échec réseau : la
  pastille reviendra à la synchronisation suivante, sans relecture automatique. Après un reset RTC (pastille connue mais
  liste de clips absente), l'appui frontal déclenche une synchronisation.
- **Lecture automatique** : désactivée de fait si le volume vocaux est à zéro. Les vocaux arrivés sont lus pendant la
  synchronisation (bloquant), dans l'ordre d'arrivée.
- **Priorité haute** (`priority: high`) : sans effet (plus de son répété ni de fond vif).
- **Appui sur le frontal sans vocal en attente** : réécoute du dernier vocal de l'agent s'il existe, sinon « Aucun vocal ».
- **Menu** : plus d'entrée « Sortir » (appui long dessus). Appui long frontal dans le menu : ignoré. Clics détectés au
  relâchement (`wasClicked`, plus de double clic : latence réduite). Confirmation de reset : maintien du frontal, annulation
  au dessus (la logique « agir = frontal » s'applique aussi là).
- **Barre de statut** : l'icône volume est barrée seulement si les deux volumes sont à zéro.
- **Sons** (SONS.md) : §7 non fait (réglage « Sons » remplacé par les deux volumes). §8 : (1) gain propre à `error` =
  constante `kErrorBoost` = 1,0 (à régler à l'oreille) ; (2) `done`/`notification` jouent aussi écran allumé ; (3) sans objet ;
  (4) rendu sur le petit haut-parleur à écouter, coefficient du claquement (0,35) dans `noteSample`. Synthèse en float32
  (1024 échantillons, 3 tampons, 2 en file dans `playRaw`) : un appel réseau bloquant pendant un son peut créer un trou.
  Le son de perte de connexion prolongée (2e échec consécutif, ou écran allumé) est conservé, via les mêmes règles anti-bruit.
- **Deux volumes** : notifications (sons marimba, aperçu = `notification`) et vocaux (aperçu = bip 1 kHz). NVS `voln` /
  `volv`, repris de l'ancien `vol`.
- **Micro** : démarré à l'appui frontal (si l'agent peut recevoir un vocal), annulé si l'appui est bref ; l'enregistrement
  ne commence que 500 ms après le démarrage (`kMicSettleMs`, déjà écoulées avec le seuil d'appui long) ; gain numérique
  +12 dB (x4) si le pic reste sous 32000, sinon le plus grand gain sans saturation (jamais en dessous de x1). À valider :
  disparition du transitoire saturé, absence de clic à l'ouverture du micro à chaque appui frontal.
- **RTC** : magic 0x53414E06 (format changé : plus de file de notifications, ajout des vocaux vus, du nombre de vocaux et
  de l'agent sélectionné).
- Les événements SSE ne sont pas utilisés par le firmware (synchronisation périodique uniquement).
