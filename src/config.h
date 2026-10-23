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
constexpr int kScreenTimeoutDefaultS = 15;  // surchargeable en NVS ("scr")
constexpr int kAlertShowMs = 4000;
constexpr int kMenuIdleMs = 30000;
constexpr int kMaxSyncPages = 5;
constexpr int kPortalTimeoutS = 240;
constexpr int kMaxAgents = 16;           // taille du cache RTC
constexpr int kBootPortalHoldMs = 3000;  // BtnB maintenu au boot : portail de config
}  // namespace cfg
