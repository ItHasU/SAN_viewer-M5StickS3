# SAN - viewer M5StickS3

Firmware PlatformIO (Arduino-ESP32 + M5Unified) d'un *viewer* SAN sur **M5StickS3** : affiche l'état des
agents, signale les nudges, joue les messages vocaux et permet de répondre à un agent par la voix.
Spécification : `SAN/docs/SPEC.md` et `SAN/docs/DESIGN.md` (ce dernier prime).

## Matériel

M5StickS3 (ESP32-S3, 8 Mo flash / 8 Mo PSRAM, écran 135x240, ES8311 + micro + HP, 2 boutons, PMIC M5PM1).
Détails et sources : [docs/HARDWARE.md](docs/HARDWARE.md).

## Build et flash

```sh
python3 -m venv ~/.venvs/pio && ~/.venvs/pio/bin/pip install platformio
pio test -e native          # tests de la logique pure (hôte)
pio run                     # compilation
pio run -t upload           # flash (USB-C ; maintenir le bouton latéral pour le mode téléchargement si besoin)
pio device monitor
```

## Configuration initiale

1. Au premier démarrage (ou bouton **B maintenu 3 s pendant le boot**), la carte ouvre un point d'accès
   `SAN-Viewer-XXXX` (portail WiFiManager, `192.168.4.1`).
2. Choisir le Wi-Fi et renseigner : **URL du serveur** (ex. `https://agents.example.org`), *URL LAN*
   (optionnelle, ex. `http://nas:3000`, utilisée si la carte est sur le SSID domestique) et *SSID domestique*.
3. La carte s'enregistre (`POST /device/register`) et affiche le **code d'appairage** (ex. `K7-42`).
   Un administrateur l'active dans l'interface web ; la carte passe alors en fonctionnement normal.

Stockage : NVS (jeton, URLs, Wi-Fi, SSID domestique) ; RTC memory (dernier événement acquitté, hashes de
config/capacités, configuration et états en cache, canal/BSSID Wi-Fi, dernier clip).

## Ergonomie des boutons (proposition)

Deux boutons : **A** = face avant (grand), **B** = côté. Le bouton latéral reset/power du PMIC n'est pas utilisable.

| Contexte | A (face avant) | B (côté) |
|---|---|---|
| Sommeil | clic : réveil, affiche les états | clic : réveil, affiche les états |
| Vue états | clic : agent suivant ; **maintenu : enregistre** (relâcher = envoi à l'agent sélectionné) | clic : réécoute du dernier message ; **maintenu : menu abonnements** |
| Menu abonnements | clic : suivant ; maintenu : muet on/off | clic : cocher/décocher ; **maintenu : enregistrer** (inactivité 30 s : annule) |
| Alerte (nudge/vocal) | tout clic : passe | tout clic : passe |

Écran : éteint pendant le sommeil. Sur réveil timer, il ne s'allume que pour un nudge/vocal d'un agent non muet
ou un passage notable (`waiting`, `error`, `done`) d'un agent suivi. Extinction après 15 s d'inactivité
(NVS `scr`). Réveil timer toutes les `next_sync_s` secondes (défaut 60, borné 10-3600, backoff en cas d'échec).

## Architecture

- `lib/san_core/` : logique pure sans HAL (machine à états, URLs, JSON, ack, décision d'écran, troncature,
  SHA-256, WAV/ADPCM). Testée en natif.
- `src/` : HAL (`hw_state` NVS/RTC, `net` Wi-Fi + `esp_http_client` TLS avec bundle de CA, `audio_io`, `ui`,
  `portal`) et orchestration (`main.cpp`).
- `fixtures/` : réponses JSON conformes à SPEC §10.4 utilisées par les tests.

## Hypothèses sur l'API (à vérifier côté serveur)

- Base = origine du serveur ; toutes les routes sont préfixées `/api/v1`.
- `caps_hash` = 16 hex de `sha256` du corps exact envoyé à `PUT /viewer/capabilities` (DESIGN §4).
- `GET`/`PUT /viewer/config` et corps `409` : `{config_hash, config:{mode,agents[]}}` (la forme à plat
  `{config_hash, mode, agents}` est aussi acceptée). Sans `config_hash` dans la réponse du `PUT`, la carte
  relit `GET /viewer/config`.
- `POST /viewer/voice?to=<agent>` : `Content-Type: audio/wav`, réponse 2xx.
- Un clip `404`/`410` est considéré comme purgé (ignoré et acquitté).

## Limites connues / TODO

- Pas de reprise de session TLS entre deux réveils (non exposée par `esp_http_client`) : TODO, ticket en RTC.
- NVS non chiffrée (jeton en clair en flash) : TODO flash encryption / NVS encryption.
- Au maximum 16 agents mis en cache en RTC ; détail tronqué à 55 caractères dans le cache (complet en direct).
- Pas d'IP statique configurable (le canal/BSSID sont mémorisés).
- Le Wi-Fi est établi avant l'interaction (pas pendant l'enregistrement) ; l'affichage initial vient du cache RTC.
- Réécoute : re-télécharge le dernier clip (pas de stockage local).
- Code non testé sur carte physique (voir `docs/HARDWARE.md`).
