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
Quatre écrans : principal, menu des paramètres, lecture d'un vocal, enregistrement vocal (+ écrans de service :
code d'appairage, portail Wi-Fi, confirmation de réinitialisation, messages).

```
+--------------------------------------+ y 0
|                                      |
|  (cog)  (coche) (!)  (?)  (coche)    |   zone d'agents : une icône par agent (dernier statut), rayon 24 px max
|           ‾‾‾‾‾              (3)     |   (17 px à 6 emplacements) ; barre sous l'agent SÉLECTIONNÉ ; « +N » au-delà
|        Nom de l'agent                |   pastille bleue en bas à droite = vocaux en attente ; nom : police 24, y ~79..105
|--------------------------------------| y 108 (séparateur)
| wifi  serveur  haut-parleur  veille  [batterie] 87% (éclair si charge)   barre de statut, y 109..134
+--------------------------------------+ y 135
```

- **Icône d'un agent** (dernier statut serveur, `states[].status`) : `idle` / `done` = coche verte ; `error` = point
  d'exclamation rouge ; `waiting` = point d'interrogation jaune ; `working` = engrenage cyan animé ; périmé ou inconnu =
  tiret gris. Personne ne réfléchit : respiration lente (±2 px). 6 emplacements max, au-delà « +N » (la fenêtre glisse
  pour garder l'agent sélectionné visible).
- **Nudges** (`start|notification|question|done|error`) : ils déclenchent le son marimba de leur type. Le serveur fixe
  l'état de l'agent avec le nudge (question→waiting, error→error, done→done, start→working, notification→inchangé) : l'icône
  vient donc uniquement de `states[]`.
- **Pastille** : nombre de vocaux non lus (`voice_pending`, « 9+ » au-delà). La liste des clips (`voice_pending_clips`,
  du plus ancien au plus récent) dit lequel lire, indépendamment du curseur d'acquittement. Un vocal lu est signalé au
  serveur (`POST /viewer/voice/:clip/played`, idempotent), ce qui décrémente la pastille.
- **Auto-sélection** : quand le statut d'un agent change ou qu'un vocal arrive, la sélection va sur cet agent. Plusieurs
  changements simultanés : priorité vocal > question > problème > prêt (à égalité : le dernier de la liste). Passer à
  « réflexion » ne change pas la sélection ; aucun changement pendant un enregistrement. La sélection est conservée entre
  deux sommeils (RTC, par slug).
- **Lecture automatique** (réglage, défaut oui) : un vocal qui arrive est joué tout de suite (écran de lecture : haut-parleur,
  agent, progression) ; sinon il reste en pastille jusqu'à l'appui frontal. Volume vocaux à zéro : pas de lecture auto.
- **Barre de statut** : Wi-Fi (barré = hors ligne), serveur (barré = dernière synchronisation en échec), volume (barré si les
  deux volumes sont à zéro), lune (barrée = veille automatique désactivée), batterie (jauge colorée + %, éclair en charge).
  Plus de lignes de notification ni de résumé : l'état réseau n'apparaît que dans la barre.
- Un nouveau nudge allume l'écran. Réveil timer sans nouveauté : écran éteint.
- Restent en plein écran : portail Wi-Fi (icône + nom de l'AP + temps restant), code d'appairage (seul, en grand),
  enregistrement, lecture d'un vocal, menu, confirmation de reset.

## Boutons

Constantes `SAN_BTN_TOP` / `SAN_BTN_FRONT` dans `src/config.h`, seuil d'appui long `cfg::kHoldMs` = 500 ms : bouton du
**dessus** = BtnB (G12, latéral), bouton **frontal** = BtnA (G11). Le bouton reset/power du PMIC n'est pas utilisable.
Les deux boutons réveillent la carte (ext1). Plus d'acquittement : le bouton du dessus ne sert plus à acquitter.

| Contexte | Dessus (BtnB) | Frontal (BtnA) |
|---|---|---|
| Sommeil | appui : réveil | idem |
| Écran principal | **clic** : agent suivant ; **appui long** : ouvrir les paramètres | **clic** : lire le vocal de l'agent sélectionné (le plus ancien en attente ; sans vocal en attente, réécoute du dernier) ; **appui long** : enregistrer (relâcher = envoi à l'agent sélectionné) |
| Menu | **clic** : élément suivant ; **appui long** : quitter | **clic** : agir sur l'élément (valeur suivante, bascule, ouvrir) |
| Portail Wi-Fi lancé depuis le menu | clic : annuler | clic : annuler (sinon timeout 240 s) |
| Confirmation de reset | **clic** : annuler (15 s d'inactivité = annulation) | **maintenir 3 s** : réinitialise (barre + compte à rebours ; relâcher = remise à zéro) |
| Lecture d'un vocal | un appui coupe la lecture | idem |
| Enregistrement | - | relâcher termine l'enregistrement |
| Démarrage (reset) | **maintenu 3 s** : portail Wi-Fi (jamais sur réveil timer) | - |

Le vocal (appui long frontal) vise l'agent **sélectionné** ; s'il n'a pas `can_send_voice` : « Envoi non autorisé vers cet agent ».
Inactivité 30 s dans le menu : fermeture.

### Menu des paramètres (7 lignes, 5 visibles, défilement + ascenseur)

Valeurs appliquées et sauvegardées immédiatement (NVS) :

| Réglage | Valeurs | Défaut |
|---|---|---|
| Vol. notifs (sons de nudge ; aperçu = son marimba) | Muet, 25 %, 50 %, 63 %, 75 %, 90 %, 100 % | 90 % (`voln`) |
| Vol. vocaux (voix : vocaux et TTS ; aperçu = bip) | idem | 90 % (`volv`) |
| Lumière | 8 %, 16 %, 27 %, 43 %, 63 %, 82 %, 100 % | 43 % (`bri`) |
| Pause (inactivité avant veille) | 10 s, 30 s, 1 min, 5 min, Jamais | 30 s (`pause`) |
| Lecture auto des vocaux | Oui / Non | Oui (`auto`) |
| Mode AP (Wi-Fi) | ouvre le portail WiFiManager | - |
| Réinitialiser | écran de confirmation (maintien frontal 3 s) | - |

Il n'y a plus de sélection des agents visibles. Un ancien réglage `vol` est repris comme valeur initiale des deux volumes.
« Jamais » : la veille profonde est désactivée, la carte reste éveillée (écran allumé, Wi-Fi en économie modem) et se
synchronise toutes les `next_sync_s` secondes. Sinon : après le délai sans appui, rétroéclairage éteint et veille profonde
(réveil timer `next_sync_s`, défaut 60, borné 10-3600, backoff en échec, ou bouton).

**Mode AP** : coupe la connexion et ouvre le portail WiFiManager (AP `SAN-Viewer-XXXX`) sans redémarrer ; enregistré, annulé
(clic) ou timeout (240 s), la carte reprend aussitôt son fonctionnement normal et se resynchronise.

**Réinitialiser** : efface la NVS (Wi-Fi, URL serveur, jeton, réglages) et l'état RTC, journalise `[san] REINITIALISATION`
sur le port série, puis redémarre en mode portail (premier démarrage). Le serveur n'est pas modifié : l'ancien enregistrement
reste à révoquer par l'administrateur.

### Sons (docs/SONS.md)

Cinq sons « marimba » synthétisés en 16 kHz par blocs de 1024 échantillons (`san_sound.*`, joués par `playRaw`, gains de
normalisation précalculés, pic 0,35). Règles anti-bruit : un seul son à la fois, 2 s minimum entre deux sons, les nudges
proches ne jouent que le type le plus prioritaire (error > question > done > notification > start), `start` muet. Le réglage
« Sons » du §7 n'est pas implémenté : il est remplacé par les deux volumes.

**Agents affichés** : pas de filtre local. Tous les agents de la configuration reçue du serveur (`config.agents`, dans
l'ordre de la config, ceux qui ont un état) sont affichés. Les champs `subscribed`, `muted` et `new` sont ignorés ; la carte
n'envoie rien à `/viewer/config`.

## Configuration initiale

1. Au premier démarrage (ou bouton **du dessus (B) maintenu 3 s pendant le démarrage**), la carte ouvre un point d'accès
   `SAN-Viewer-XXXX` (portail WiFiManager, `192.168.4.1`).
2. Choisir le Wi-Fi et renseigner : **URL du serveur** (ex. `https://agents.example.org`), *URL LAN*
   (optionnelle, ex. `http://nas:3000`, utilisée si la carte est sur le SSID domestique) et *SSID domestique*.
3. La carte s'enregistre (`POST /device/register`) et affiche le **code d'appairage** (ex. `K7-42`, seul, en grand,
   tant que la carte n'est pas activée).
   Un administrateur l'active dans l'interface web ; la carte passe alors en fonctionnement normal.

Stockage : NVS (jeton, URLs, Wi-Fi, SSID domestique, réglages volumes/lumière/pause/lecture auto) ; RTC memory (dernier événement acquitté, hashes de
config/capacités, configuration et états en cache avec pastilles et clips vus, agent sélectionné, canal/BSSID Wi-Fi, dernier clip).

## Architecture

- `lib/san_core/` : logique pure sans HAL (machine à états, URLs, JSON, ack, décision d'écran, troncature,
  état des agents et auto-sélection, sons marimba et règles anti-bruit, réglages, menu, actions des boutons, batterie, SHA-256, WAV/ADPCM). Testée en natif.
- `src/` : HAL (`hw_state` NVS/RTC, `net` Wi-Fi + `esp_http_client` TLS avec bundle de CA, `audio_io`, `ui`,
  `portal`) et orchestration (`main.cpp`).
- `fixtures/` : réponses JSON conformes à SPEC §10.4 (dont `server_sync.json`, capturée sur le vrai serveur) et `fixtures/audio/*.wav`, produits par la commande ffmpeg exacte du serveur (`tools/gen-fixtures.sh`, ffmpeg requis pour les régénérer) ; utilisés par les tests natifs.

## Hypothèses sur l'API (à vérifier côté serveur)

- Base = origine du serveur ; toutes les routes sont préfixées `/api/v1`.
- `caps_hash` = 16 hex de `sha256` du corps exact envoyé à `PUT /viewer/capabilities` (DESIGN §4).
- La config arrive dans `GET /viewer/sync` : `{config_hash, config:{mode,agents[]}}` (seuls `agent`, `name`,
  `position`, `can_send_voice` sont lus). La carte n'appelle plus `/viewer/config`.
- `POST /viewer/voice?to=<agent>` : `Content-Type: audio/wav`, réponse 2xx.
- `states[]` porte `voice_pending` et `voice_pending_clips` ; `POST /viewer/voice/:clip/played` (sans corps, idempotent) marque lu. Les événements SSE ne sont pas utilisés (synchronisation par `GET /viewer/sync`).
- Un clip `404`/`410` est considéré comme purgé (ignoré et acquitté).

## Limites connues / TODO

- Pas de reprise de session TLS entre deux réveils (non exposée par `esp_http_client`) : TODO, ticket en RTC.
- NVS non chiffrée (jeton en clair en flash) : TODO flash encryption / NVS encryption.
- Au maximum 16 agents mis en cache en RTC (le `detail` des états n'est plus affiché ni mis en cache).
- Portail Wi-Fi : point d'accès ouvert (sans mot de passe).
- L'état de charge (éclair) dépend de `M5.Power.isCharging()` : fiabilité à valider sur StickS3 (sinon pas d'éclair).
- Pas d'IP statique configurable (le canal/BSSID sont mémorisés).
- Le Wi-Fi est établi avant l'interaction (pas pendant l'enregistrement) ; l'affichage initial vient du cache RTC.
- Lecture : le clip est téléchargé à la demande (le dernier est gardé en PSRAM pendant l'éveil).
- `max_detail_chars` reste déclaré dans les capacités (le détail n'est plus affiché) : le retirer changerait le hash.
- Code non testé sur carte physique (voir `docs/HARDWARE.md`).
