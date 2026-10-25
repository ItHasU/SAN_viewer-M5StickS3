// Stockage : NVS (Preferences) pour la config durable, RTC memory pour l'etat entre deux sommeils.
#pragma once
#include <Arduino.h>

#include <string>
#include <vector>

#include "config.h"
#include "san_logic.h"
#include "san_board.h"
#include "san_ui.h"
#include "san_types.h"

struct Settings {
  String token, serverUrl, lanUrl, wifiSsid, wifiPass, homeSsid;
  int volNotif = san::kDefaultVolume;        // 0-255 : sons de nudge
  int volVoice = san::kDefaultVolume;        // 0-255 : voix (vocaux, TTS)
  bool autoPlay = san::kDefaultAutoPlay;     // lecture automatique des vocaux a l'arrivee
  int brightness = san::kDefaultBrightness;  // 5-255
  int pauseS = san::kDefaultPauseS;          // delai d'inactivite avant veille ; 0 = jamais
  bool configured() const { return serverUrl.length() && wifiSsid.length(); }
};

void settingsLoad(Settings &s);
void settingsSave(const Settings &s);      // reseau/serveur (portail)
void settingsSaveUi(const Settings &s);    // volumes, luminosite, pause, lecture auto
void settingsEraseAll(Settings &s);        // efface tout le namespace NVS (reinitialisation d'usine) ; `s` remis par defaut
void tokenSave(Settings &s, const String &token);
void tokenClear(Settings &s);

// ---- RTC memory (survit au deep sleep, pas a une coupure d'alimentation) ----
struct RtcAgent {
  char slug[32];
  char name[20];
  uint8_t flags;  // 8 can_send_voice (bits 1/2/4 : anciens abonnement/nouveau/muet, ignores)
  uint8_t status; // dernier statut serveur : 0 idle 1 working 2 waiting 3 error 4 done 255 inconnu
  uint8_t stale;
  int8_t progress;
  uint8_t voices;                    // vocaux en attente (voice_pending)
  uint8_t nSeen;                     // empreintes de clips vues (san::clipHash)
  uint16_t seen[san::kSeenMax];
};

struct RtcState {
  uint32_t magic;
  int64_t lastAck;
  char configHash[24];
  char capsHash[24];
  char mode[8];
  char pairingCode[12];
  char lastClipId[40];
  char lastClipFmt[12];
  char lastClipAgent[32];
  int32_t lastClipRate;
  uint8_t active;       // viewer deja active (saute PAIRING au prochain boot)
  uint8_t failures;     // echecs reseau consecutifs
  uint8_t wifiFails;    // echecs Wi-Fi consecutifs (son d'erreur au 2e)
  uint8_t serverFails;  // echecs serveur consecutifs (Wi-Fi OK)
  uint8_t channel;
  uint8_t bssid[6];
  uint8_t nAgents;
  RtcAgent agents[cfg::kMaxAgents];
  char selAgent[32];    // slug de l'agent selectionne (cible du vocal), conserve entre deux sommeils
};

extern RtcState rtc;
void rtcInit();  // reinitialise si magic invalide
void rtcReset();

// Conversions Config/etats <-> cache RTC.
void rtcStoreConfig(const san::Config &c);
san::Config rtcLoadConfig();
void rtcStoreStates(const std::vector<san::AgentState> &states, const san::Config &c);
std::vector<san::AgentState> rtcLoadStates();
// Etat precedent (statut, clips vus) pour detecter les changements ; puis memorisation des clips vus.
std::vector<san::PrevAgent> rtcPrevAgents();
void rtcUpdateSeen(const std::vector<san::AgentState> &states);
