#include "san_protocol.h"
#include "san_sound.h"

#include <ArduinoJson.h>

#include "sha256.h"

namespace san {

std::string normalizeBase(const std::string &in) {
  std::string u = in;
  while (!u.empty() && (u.back() == '/' || u.back() == ' ')) u.pop_back();
  size_t n = std::string("/api/v1").size();
  if (u.size() >= n && u.compare(u.size() - n, n, "/api/v1") == 0) u.resize(u.size() - n);
  while (!u.empty() && u.back() == '/') u.pop_back();
  return u;
}

std::string chooseBase(const std::string &serverUrl, const std::string &lanUrl,
                       const std::string &currentSsid, const std::string &homeSsid) {
  if (!lanUrl.empty() && !homeSsid.empty() && currentSsid == homeSsid) return normalizeBase(lanUrl);
  return normalizeBase(serverUrl);
}

std::string urlEncode(const std::string &s) {
  static const char *hx = "0123456789ABCDEF";
  std::string o;
  for (unsigned char c : s) {
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' ||
        c == '_' || c == '.' || c == '~') {
      o += (char)c;
    } else {
      o += '%'; o += hx[c >> 4]; o += hx[c & 15];
    }
  }
  return o;
}

static const char *API = "/api/v1";

std::string syncUrl(const std::string &base, int64_t ack, const std::string &configHash,
                    const std::string &capsHashV, int battery) {
  std::string u = base + API + "/viewer/sync";
  char sep = '?';
  auto add = [&](const std::string &k, const std::string &v) { u += sep; u += k + "=" + v; sep = '&'; };
  if (ack >= 0) add("ack", std::to_string((long long)ack));
  if (!configHash.empty()) add("config_hash", urlEncode(configHash));
  if (!capsHashV.empty()) add("caps_hash", urlEncode(capsHashV));
  if (battery >= 0) add("battery", std::to_string(battery));
  return u;
}

std::string registerUrl(const std::string &b) { return b + API + "/device/register"; }
std::string statusUrl(const std::string &b) { return b + API + "/device/status"; }
std::string capabilitiesUrl(const std::string &b) { return b + API + "/viewer/capabilities"; }
std::string configUrl(const std::string &b) { return b + API + "/viewer/config"; }
std::string voiceClipUrl(const std::string &b, const std::string &id) {
  return b + API + "/viewer/voice/" + urlEncode(id);
}
std::string voiceUploadUrl(const std::string &b, const std::string &agent) {
  return b + API + "/viewer/voice?to=" + urlEncode(agent);
}

std::string buildCapabilitiesJson(const std::string &firmware) {
  // Valeurs : DESIGN.md section 7 + ADPCM (decodeur implemente et teste, voir docs/HARDWARE.md).
  JsonDocument d;
  d["protocol_version"] = 1;
  d["firmware"] = firmware;
  JsonObject disp = d["display"].to<JsonObject>();
  disp["width"] = 135; disp["height"] = 240; disp["color"] = true; disp["max_detail_chars"] = 80;
  JsonObject ao = d["audio_out"].to<JsonObject>();
  ao["formats"].add("ima_adpcm"); ao["formats"].add("pcm_s16le");
  ao["sample_rates"].add(8000); ao["sample_rates"].add(16000);
  JsonObject ai = d["audio_in"].to<JsonObject>();
  ai["formats"].add("pcm_s16le"); ai["sample_rate"] = 16000; ai["max_duration_s"] = 10;
  d["nudge"].add("sound"); d["nudge"].add("screen");
  d["input"].add("buttons");
  d["battery"] = true;
  std::string s;
  serializeJson(d, s);
  return s;
}

std::string capsHash(const std::string &raw) { return sha256HexTrunc(raw, 16); }

std::string buildRegisterBody(const std::string &hw, const std::string &model, const std::string &caps) {
  JsonDocument d;
  d["hardware_id"] = hw;
  d["model"] = model;
  d["capabilities"] = serialized(caps);
  std::string s;
  serializeJson(d, s);
  return s;
}

std::string buildConfigPutBody(const std::string &baseHash, const Config &cfg) {
  JsonDocument d;
  d["base_hash"] = baseHash;
  JsonArray arr = d["subscriptions"].to<JsonArray>();
  int pos = 0;
  for (auto &a : cfg.agents) {
    JsonObject o = arr.add<JsonObject>();
    o["agent"] = a.agent;
    o["subscribed"] = a.subscribed;
    o["position"] = a.position >= 0 ? a.position : pos;
    o["muted"] = a.muted;
    pos++;
  }
  std::string s;
  serializeJson(d, s);
  return s;
}

// ---- Parsing ----
static std::string str(JsonVariantConst v) { return v.is<const char *>() ? std::string(v.as<const char *>()) : std::string(); }

static void readConfig(JsonObjectConst o, Config &c) {
  c = Config();
  c.mode = str(o["mode"]);
  for (JsonObjectConst a : o["agents"].as<JsonArrayConst>()) {
    AgentCfg g;
    g.agent = str(a["agent"]);
    g.name = str(a["name"]);
    if (g.name.empty()) g.name = g.agent;
    g.subscribed = a["subscribed"] | false;
    g.isNew = a["new"] | false;
    g.muted = a["muted"] | false;
    g.canSendVoice = a["can_send_voice"] | false;
    g.position = a["position"].is<int>() ? a["position"].as<int>() : -1;
    if (!g.agent.empty()) c.agents.push_back(g);
  }
}

bool parseRegister(const std::string &json, RegisterResponse &out) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  out.token = str(d["token"]);
  out.pairingCode = str(d["pairing_code"]);
  out.status = str(d["status"]);
  return !out.token.empty();
}

DeviceStatus parseStatus(const std::string &json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return DeviceStatus::Unknown;
  std::string s = str(d["status"]);
  if (s == "active") return DeviceStatus::Active;
  if (s == "pending") return DeviceStatus::Pending;
  return DeviceStatus::Unknown;
}

bool parseSync(const std::string &json, SyncResponse &out) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  if (!d.is<JsonObject>()) return false;
  out = SyncResponse();
  out.serverTime = str(d["server_time"]);
  out.capabilitiesRequired = d["capabilities_required"] | false;
  out.configHash = str(d["config_hash"]);
  if (d["config"].is<JsonObject>()) {
    out.hasConfig = true;
    readConfig(d["config"].as<JsonObjectConst>(), out.config);
  }
  for (JsonObjectConst s : d["states"].as<JsonArrayConst>()) {
    AgentState st;
    st.agent = str(s["agent"]);
    st.name = str(s["name"]);
    if (st.name.empty()) st.name = st.agent;
    st.status = str(s["status"]);
    st.detail = str(s["detail"]);
    st.updatedAt = str(s["updated_at"]);
    st.progress = s["progress"].is<int>() ? s["progress"].as<int>() : -1;
    st.stale = s["stale"] | false;
    out.states.push_back(st);
  }
  for (JsonObjectConst e : d["events"].as<JsonArrayConst>()) {
    EventItem ev;
    ev.id = e["id"] | (int64_t)0;
    ev.agent = str(e["agent"]);
    ev.kind = str(e["kind"]);
    ev.priority = str(e["priority"]);
    ev.text = str(e["text"]);
    ev.type = parseNudgeType(str(e["type"]));
    ev.createdAt = str(e["created_at"]);
    if (e["clip"].is<JsonObjectConst>()) {
      JsonObjectConst c = e["clip"];
      ev.hasClip = true;
      ev.clip.id = str(c["id"]);
      ev.clip.format = str(c["format"]);
      ev.clip.sampleRate = c["sample_rate"] | 0;
      ev.clip.size = c["size"] | 0;
      ev.clip.durationMs = c["duration_ms"] | 0;
      if (ev.clip.id.empty()) ev.hasClip = false;
    }
    if (ev.id > 0) out.events.push_back(ev);
  }
  out.hasMore = d["has_more"] | false;
  out.nextSyncS = d["next_sync_s"] | 0;
  return true;
}

bool parseConfigResponse(const std::string &json, ConfigResponse &out) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  out = ConfigResponse();
  out.configHash = str(d["config_hash"]);
  if (d["config"].is<JsonObject>()) {
    readConfig(d["config"].as<JsonObjectConst>(), out.config);
    return true;
  }
  if (d["agents"].is<JsonArray>()) {
    readConfig(d.as<JsonObjectConst>(), out.config);
    return true;
  }
  return false;
}

bool parseApiError(const std::string &json, ApiError &out) {
  JsonDocument d;
  if (deserializeJson(d, json)) return false;
  out.error = str(d["error"]);
  out.message = str(d["message"]);
  return !out.error.empty();
}

}  // namespace san
