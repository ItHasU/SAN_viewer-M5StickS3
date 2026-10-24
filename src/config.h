// Constantes du firmware (valeurs par defaut ; certaines sont surchargeables en NVS).
#pragma once
#include <cstdint>

namespace cfg {
constexpr const char *kModel = "M5StickS3";
constexpr const char *kFirmware = "san-m5sticks3 " SAN_FIRMWARE_VERSION;

constexpr int kRecRate = 16000;          // Hz, WAV PCM s16le mono (capacite audio_in)
constexpr int kRecMaxS = 10;             // audio_in.max_duration_s
constexpr int kRecMinMs = 600;           // en dessous : message ignore
constexpr uint32_t kWifiTimeoutMs = 12000;
constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr size_t kMaxJsonResp = 96 * 1024;
constexpr size_t kMaxClipBytes = 2 * 1024 * 1024;
// Reglages utilisateur (menu) : valeurs par defaut et series dans san_core/san_ui.*, persistes en NVS.
constexpr int kAlertShowMs = 4000;
constexpr int kMenuIdleMs = 30000;
constexpr int kMaxSyncPages = 5;
constexpr int kPortalTimeoutS = 240;
constexpr int kMaxAgents = 16;           // taille du cache RTC
constexpr int kBootPortalHoldMs = 3000;  // bouton du dessus maintenu au boot : portail de config
constexpr int kHoldMs = 500;             // appui long (enregistrement, menu, bascule d'agent)
constexpr int kNotifLines = 3;           // lignes de notification affichees en haut
constexpr int kBatteryRefreshMs = 30000;
}  // namespace cfg

// Affectation des boutons (hypothese a valider sur la carte : avec setRotation(1), le bouton lateral est en haut).
// Pour inverser, echanger ces deux lignes. Les deux boutons reveillent la carte (ext1 G11 / G12).
#define SAN_BTN_TOP M5.BtnB    // dessus : acquitter, agent suivant, agir dans le menu
#define SAN_BTN_FRONT M5.BtnA  // frontal : repeter, enregistrer, menu (double clic), defiler
