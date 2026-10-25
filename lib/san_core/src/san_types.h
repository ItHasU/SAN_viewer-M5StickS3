// Types de donnees du protocole SAN cote viewer (logique pure, sans HAL).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace san {

struct Clip {
  std::string id;
  std::string format;  // "ima_adpcm" | "pcm_s16le"
  int sampleRate = 0;
  int size = 0;
  int durationMs = 0;
};

struct AgentState {
  std::string agent, name, status, detail, updatedAt;
  int progress = -1;  // -1 = absent
  bool stale = false;
  int voicePending = 0;                 // vocaux non lus (voice_pending) ; 0 si absent (ancien serveur)
  std::vector<std::string> voiceClips;  // voice_pending_clips : du plus ancien au plus recent (20 max)
};

// Type d'un nudge (champ optionnel "type" ; absent ou inconnu -> Notification).
enum class NudgeType { Start, Notification, Question, Done, Error };

struct EventItem {
  int64_t id = 0;
  std::string agent, kind, priority, text, createdAt;  // kind: "nudge" | "voice"
  NudgeType type = NudgeType::Notification;
  bool hasClip = false;
  Clip clip;
  bool isHigh() const { return priority == "high"; }
};

struct AgentCfg {
  std::string agent, name;
  bool canSendVoice = false;
  int position = -1;  // -1 = null
};

struct Config {
  std::string mode;  // "manual" | "auto"
  std::vector<AgentCfg> agents;
  const AgentCfg *find(const std::string &slug) const {
    for (auto &a : agents) if (a.agent == slug) return &a;
    return nullptr;
  }
};

struct SyncResponse {
  std::string serverTime;
  bool capabilitiesRequired = false;
  std::string configHash;
  bool hasConfig = false;  // "config" non nul
  Config config;
  std::vector<AgentState> states;
  std::vector<EventItem> events;
  bool hasMore = false;
  int nextSyncS = 0;  // 0 = absent
};

struct RegisterResponse {
  std::string token, pairingCode, status;
};

enum class DeviceStatus { Unknown, Pending, Active };

struct ApiError {
  std::string error, message;
};

}  // namespace san
