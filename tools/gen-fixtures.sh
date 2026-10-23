#!/usr/bin/env bash
# Génère les fixtures audio binaires avec la commande ffmpeg EXACTE du serveur SAN
# (SAN/server/src/san/audio/service.ts, ensureRendition, + formats.ts) :
#
#   ffmpeg -nostdin -hide_banner -loglevel error -y -protocol_whitelist file \
#     -f wav -i file:<pivot.wav> -map 0:a:0 -vn -ac 1 -ar <rate> <codecArgs> file:<sortie>
#
# où codecArgs = « -c:a adpcm_ima_wav -f wav » (ima_adpcm) ou « -c:a pcm_s16le -f wav » (pcm_s16le).
# Le pivot est une sinusoïde de 440 Hz, 1,0 s, 16 kHz mono (comme le pivot du serveur).
#
# Usage : tools/gen-fixtures.sh          (écrit fixtures/audio/*.wav)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
FFMPEG="${FFMPEG_PATH:-ffmpeg}"
OUT=fixtures/audio
mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Pivot : WAV PCM s16le mono 16 kHz (rendition « wav-16000 » du serveur)
"$FFMPEG" -nostdin -hide_banner -loglevel error -y -f lavfi -i "sine=frequency=440:sample_rate=16000:duration=1" \
  -ac 1 -ar 16000 -c:a pcm_s16le -f wav "$TMP/pivot.wav"

rendition() { # format rate codec
  "$FFMPEG" -nostdin -hide_banner -loglevel error -y -protocol_whitelist file \
    -f wav -i "file:$TMP/pivot.wav" -map 0:a:0 -vn -ac 1 -ar "$2" -c:a "$3" -f wav "file:$OUT/$1-$2.wav"
}
rendition ima_adpcm 8000 adpcm_ima_wav
rendition ima_adpcm 16000 adpcm_ima_wav
rendition pcm_s16le 8000 pcm_s16le
rendition pcm_s16le 16000 pcm_s16le
ls -l "$OUT"
