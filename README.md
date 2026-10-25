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

## Écran (paysage 240x135)

`setRotation(1)` : USB-C en bas, bouton latéral (**dessus**) en haut, bouton **frontal** sur la face avant.

```
+--------------------------------------+
| [icone] Agent        x2   <- notifications en attente,
| [icone] Agent             <- 3 lignes max, la plus recente en haut
| [icone] Agent                 (icone du type : start / notification / question / done / error / vocal)
|--------------------------------------|   (sans notification : agent selectionne en grand, icone + nom)
| cloche N | engrenage w/f | pastille+agent |  <- nb de notifications | agents en activite / suivis | agent cible
| wifi  serveur  haut-parleur  veille  [batterie] 87% (eclair si charge)
+--------------------------------------+
```

- Plus aucun détail texte d'agent : icônes et grandes polices. `priority: high` = ligne sur fond vif.
- Activité : l'engrenage tourne et une petite barre glisse si au moins un agent est `working` ; `w/f` = agents en
  activité / agents suivis par ce viewer.
- Statut : Wi-Fi (barré = hors ligne), serveur (barré = dernière synchronisation en échec), haut-parleur (barré =
  volume 0), lune (barrée = veille automatique désactivée), batterie (jauge colorée + %, éclair en charge).
- File de notifications : nudges et vocaux reçus, bornée à 16 entrées, persistée en RTC memory entre deux sommeils
  (perdue à une coupure d'alimentation). Même agent + même type = fusion avec compteur (`x2`) ; le total du bas
  est la somme des compteurs. Acquitter est **local** (le serveur est déjà acquitté dès que la notification est en file).
- Un nouveau nudge allume l'écran et joue le son de son type (`lib/san_core/src/san_sound.*`, 1 son par lot : le
  plus important ; `high` = son répété 2 fois). Réveil timer sans nouveauté : écran éteint.
- **Connexion = notifications locales, pas d'écran plein** (`san_notify.*`, `Sys`) : pendant la connexion une ligne
  « Wi-Fi... » puis « Serveur... » (icône qui pulse) apparaît dans la zone du haut, seulement si l'écran est allumé.
  Échec : ligne rouge « Wi-Fi » (icône barrée) / « Serveur » (icône croisée), fusion avec compteur `xN`. Rétablissement
  après un échec : « Wi-Fi OK » / « Serveur OK » (coche verte) qui s'auto-acquitte après 4 s ; pas de « OK » sans échec
  précédent. Une entrée de connexion est **remplacée** (pas empilée) quand l'état change, s'acquitte avec le bouton du
  dessus comme les autres, n'est jamais envoyée au serveur ni comptée comme agent. Ces entrées sont **éphémères** (non
  persistées en RTC) ; seuls les compteurs d'échecs consécutifs (`wifiFails`, `serverFails`) le sont. Son d'erreur :
  écran déjà allumé, ou 2e échec consécutif (une seule fois, pas à chaque réveil hors couverture) ; un réveil timer
  reste écran éteint. Restent en plein écran : portail Wi-Fi, code d'appairage, enregistrement, vocal, agents.

## Boutons

Hypothèses (à valider sur la carte, constantes `SAN_BTN_TOP` / `SAN_BTN_FRONT` dans `src/config.h`, seuil d'appui long
`cfg::kHoldMs` = 500 ms) : bouton du **dessus** = BtnB (G12, latéral), bouton **frontal** = BtnA (G11). Le bouton
reset/power du PMIC n'est pas utilisable. Les deux boutons réveillent la carte (ext1).

| Contexte | Dessus (BtnB) | Frontal (BtnA) |
|---|---|---|
| Sommeil | clic : réveil, affiche la vue principale | idem |
| Vue principale | **clic** : acquitte la notification du haut (la plus récente) ; sans notification : agent suivant. **Appui long** : agent suivant (même avec des notifications) | **clic** : répète le dernier vocal ; **appui long** : enregistre (relâcher = envoi à l'agent sélectionné) ; **double clic** : ouvre le menu |
| Menu (racine : Volume, Lumière, Pause, Agents, Sortir) | **clic** : modifie la ligne courante (valeur suivante ; Agents = ouvrir ; Sortir = quitter) | **clic** : ligne suivante ; **appui long** : quitter |
| Menu > Agents (liste cochable + ligne « Enregistrer ») | **clic** : coche/décoche (sur « Enregistrer » : sauvegarde) ; **appui long** : muet on/off de l'agent | **clic** : ligne suivante ; **appui long** : retour sans enregistrer |
| Enregistrement / lecture | un appui coupe la lecture ou le son en cours | relâcher termine l'enregistrement |
| Démarrage (reset) | **maintenu 3 s** : portail Wi-Fi (jamais sur réveil timer) | - |

Inactivité 30 s dans le menu : fermeture sans enregistrer les abonnements.

Réglages (NVS `vol`, `bri`, `pause`, appliqués et sauvegardés immédiatement ; le volume joue un bip d'aperçu) :

| Réglage | Valeurs | Défaut |
|---|---|---|
| Volume | Muet, 25 %, 50 %, 63 %, 75 %, 90 %, 100 % (0-255) | 230 (90 %) |
| Lumière | 8 %, 16 %, 27 %, 43 %, 63 %, 82 %, 100 % | 110 (43 %) |
| Pause (délai d'inactivité avant veille) | 10 s, 30 s, 1 min, 5 min, Jamais | 30 s |

« Jamais » : la veille profonde est désactivée, la carte reste éveillée (écran allumé, Wi-Fi connecté en économie
modem) et se synchronise toutes les `next_sync_s` secondes. Sinon : après le délai sans appui, rétroéclairage éteint
et veille profonde (réveil timer `next_sync_s`, défaut 60, borné 10-3600, backoff en échec, ou bouton). Pendant la
fenêtre d'éveil, la carte se resynchronise aussi à chaque `next_sync_s`.

Les agents visibles sur l'écran sont ceux suivis (abonnements) ; la sélection des agents se fait dans le menu
(mode `manual` ; en mode `auto`, seul le muet est modifiable). `PUT /viewer/config` avec gestion du 409.

## Configuration initiale

1. Au premier démarrage (ou bouton **du dessus (B) maintenu 3 s pendant le démarrage**), la carte ouvre un point d'accès
   `SAN-Viewer-XXXX` (portail WiFiManager, `192.168.4.1`).
2. Choisir le Wi-Fi et renseigner : **URL du serveur** (ex. `https://agents.example.org`), *URL LAN*
   (optionnelle, ex. `http://nas:3000`, utilisée si la carte est sur le SSID domestique) et *SSID domestique*.
3. La carte s'enregistre (`POST /device/register`) et affiche le **code d'appairage** (ex. `K7-42`).
   Un administrateur l'active dans l'interface web ; la carte passe alors en fonctionnement normal.

Stockage : NVS (jeton, URLs, Wi-Fi, SSID domestique, réglages volume/lumière/pause) ; RTC memory (dernier événement acquitté, file de notifications, hashes de
config/capacités, configuration et états en cache, canal/BSSID Wi-Fi, dernier clip).

## Architecture

- `lib/san_core/` : logique pure sans HAL (machine à états, URLs, JSON, ack, décision d'écran, troncature,
  file de notifications, réglages, menu, actions des boutons, batterie, SHA-256, WAV/ADPCM). Testée en natif.
- `src/` : HAL (`hw_state` NVS/RTC, `net` Wi-Fi + `esp_http_client` TLS avec bundle de CA, `audio_io`, `ui`,
  `portal`) et orchestration (`main.cpp`).
- `fixtures/` : réponses JSON conformes à SPEC §10.4 (dont `server_sync.json`, capturée sur le vrai serveur) et `fixtures/audio/*.wav`, produits par la commande ffmpeg exacte du serveur (`tools/gen-fixtures.sh`, ffmpeg requis pour les régénérer) ; utilisés par les tests natifs.

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
- Au maximum 16 agents mis en cache en RTC (le `detail` des états n'est plus affiché ni mis en cache).
- Portail Wi-Fi : point d'accès ouvert (sans mot de passe).
- L'état de charge (éclair) dépend de `M5.Power.isCharging()` : fiabilité à valider sur StickS3 (sinon pas d'éclair).
- Pas d'IP statique configurable (le canal/BSSID sont mémorisés).
- Le Wi-Fi est établi avant l'interaction (pas pendant l'enregistrement) ; l'affichage initial vient du cache RTC.
- Réécoute : instantanée tant que la carte reste éveillée (clip gardé en PSRAM), sinon re-téléchargement.
- `max_detail_chars` reste déclaré dans les capacités (le détail n'est plus affiché) : le retirer changerait le hash.
- Le menu ne peut pas lister d'agents hors ligne (Wi-Fi requis pour « Agents »).
- Code non testé sur carte physique (voir `docs/HARDWARE.md`).
