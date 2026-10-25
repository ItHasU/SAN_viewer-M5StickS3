# Spec : sons des nudges (timbre marimba)

Statut : proposition, à valider. Timbre retenu après écoute des candidats (cloche, marimba, goutte). Référence d'écoute : `gen_sons.py`, jeu `marimba/`.

## 1. Objectif

Remplacer les sons carrés actuels (`lib/san_core/src/san_sound.cpp`) par des sons doux et discrets : une ou deux notes, sans répétition, enveloppe percussive. Le son informe sans stresser.

## 2. Synthèse d'une note

Une note = fréquence `f`, début `t0`, durée `d`. Pour `t` en secondes depuis `t0` :

```
s(t) = sin(2π·f·t)·e^(-t/0,22) + 0,35·sin(2π·4f·t)·e^(-t/0,05)
s(t) *= min(1, t/0,002)        # attaque 2 ms
```

- Le terme `4f` est le claquement de lame : il disparaît en environ 0,15 s.
- La note se coupe à `d`. Les notes d'un même son s'additionnent (elles se chevauchent).
- Pas de DC ni de clic en fin : la fin de chaque note est déjà proche de zéro, mais appliquer une rampe de 5 ms avant la coupure par sécurité.

### Cas de l'erreur

L'erreur n'utilise pas le marimba mais un grave arrondi (déjà validé à l'écoute) :

```
s(t) = sin(2πf t) + 0,6·sin(2π·2f·t) + 0,3·sin(2π·3f·t)
s(t) *= e^(-t/0,28) · min(1, t/0,012)
```

## 3. Séquences

Notes en Hz : do5 523,25 · ré5 587,33 · mi5 659,25 · sol5 783,99 · la5 880,00 · ré3 146,83 · la3 220,00.

| Type | Notes (f, début s, durée s) | Durée totale |
|---|---|---|
| start | do5 0,00 / 0,50 ; mi5 0,16 / 0,60 | 1,06 s |
| notification | sol5 0,00 / 0,90 | 1,20 s |
| question | ré5 0,00 / 0,45 ; la5 0,20 / 0,90 | 1,40 s |
| done | sol5 0,00 / 0,50 ; mi5 0,20 / 0,50 ; do5 0,40 / 1,00 | 1,70 s |
| error | la3 0,00 / 0,55 ; ré3 0,28 / 0,80 | 1,38 s |

La durée totale inclut 0,3 s de queue après la dernière note.

## 4. Niveau

- Chaque son est normalisé à un pic de 0,35 (environ -9 dBFS), comme dans les fichiers d'écoute.
- Le gain de normalisation est une constante précalculée par type (pas de recherche de pic à l'exécution). Un test natif vérifie que le pic réel reste dans 0,35 ± 2 %.
- Le volume utilisateur (réglage existant) s'applique par-dessus.
- Le grave de l'erreur sort faible sur le haut-parleur : prévoir un gain propre à l'erreur, à régler sur la carte (point ouvert, §8).

## 5. Implémentation

- Fréquence d'échantillonnage : 16 kHz, mono, 16 bits. Le partiel `4f` maximal (la5 → 3520 Hz) reste sous Nyquist.
- Synthèse **par blocs** (par exemple 512 échantillons) dans `playRaw`, sans tampon complet : le plus long son ferait 27 200 échantillons, soit 54 Ko.
- Logique pure dans `lib/san_core` (liste de notes, formules, gains) avec tests natifs. Le rendu et l'I/O restent dans `src/audio_io.cpp`.
- `san_sound` expose pour chaque type : la liste de notes et le gain. L'ancien format `{freq_hz, ms}` est supprimé.
- Conserver la coupure de l'ampli et du codec ES8311 hors lecture (`fd1cf80`) : lancer la lecture, puis couper à la fin du dernier bloc.

## 6. Règles anti-bruit

- Un seul son à la fois. Si un nouveau nudge arrive pendant un son, il attend la fin du son en cours (pas de superposition ni de coupure brutale).
- Délai minimal de 2 s entre deux sons. Les nudges trop proches ne jouent qu'un son, celui du type le plus prioritaire (error > question > done > notification > start).
- `start` est muet par défaut (déjà désactivé côté connecteur).
- Écran allumé et nudge `done` ou `notification` : son joué (l'utilisateur peut regarder ailleurs). Option à trancher, voir §8.
- Aucun son pour les nudges système Wi-Fi/Serveur, sauf `error` pour une perte de connexion prolongée (comportement actuel à conserver).

## 7. Réglage « Sons » (menu)

Trois modes, valeur persistée avec les autres réglages (RTC et NVS comme le volume) :

| Mode | Effet |
|---|---|
| normal | tous les types jouent, gain nominal |
| discret | gain -6 dB ; seuls `question` et `error` jouent |
| muet | aucun son (l'écran et le vocal restent actifs) |

Défaut : normal. Le TTS (`say`) n'est pas concerné par ce réglage, il suit le volume.

## 8. Points ouverts

1. Gain propre à `error` : à régler à l'écoute sur la carte.
2. Son pour `done` et `notification` quand l'écran est déjà allumé : jouer ou non.
3. Mode « discret » : liste des types qui jouent à confirmer.
4. Rendu sur le petit haut-parleur : les sons seront plus pointus qu'à l'écoute sur le Mac. Prévoir un essai sur la carte et un ajustement possible du claquement (`0,35` du partiel `4f`).

## 9. Critères d'acceptation

- Tests natifs : durées, nombre de notes, pic ≤ 0,35 ± 2 %, absence de dépassement 16 bits, pas de clic (premier et dernier échantillon proches de 0).
- Écoute sur la carte des 5 types, comparée aux fichiers `marimba/*.wav`.
- Pas de grésillement hors lecture (ampli et codec coupés).
- `pio test -e native` et `pio run -e m5sticks3` verts.
