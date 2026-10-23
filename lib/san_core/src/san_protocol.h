// URLs, construction des corps et parsing des reponses JSON (API /api/v1).
#pragma once
#include <string>

#include "san_types.h"

namespace san {

// ---- URL ----
// Retire les "/" finaux et un eventuel suffixe "/api/v1".
std::string normalizeBase(const std::string &url);
// URL LAN si le SSID courant est le SSID domestique configure, sinon URL publique.
std::string chooseBase(const std::string &serverUrl, const std::string &lanUrl,
                       const std::string &currentSsid, const std::string &homeSsid);
std::string urlEncode(const std::string &s);
// ack<0 / battery<0 / hash vide : parametre omis.
std::string syncUrl(const std::string &base, int64_t ack, const std::string &configHash,
                    const std::string &capsHash, int battery);
std::string registerUrl(const std::string &base);
std::string statusUrl(const std::string &base);
std::string capabilitiesUrl(const std::string &base);
std::string configUrl(const std::string &base);
std::string voiceClipUrl(const std::string &base, const std::string &clipId);
std::string voiceUploadUrl(const std::string &base, const std::string &agent);

// ---- Capacites ----
// Document de capacites (compact, ordre stable). Le hash est calcule sur CETTE chaine.
std::string buildCapabilitiesJson(const std::string &firmware);
// 16 premiers hex de sha256(corps brut) - DESIGN.md section 4.
std::string capsHash(const std::string &rawBody);

// ---- Corps de requete ----
std::string buildRegisterBody(const std::string &hardwareId, const std::string &model,
                              const std::string &capsJson);
std::string buildConfigPutBody(const std::string &baseHash, const Config &cfg);

// ---- Parsing ----
bool parseRegister(const std::string &json, RegisterResponse &out);
DeviceStatus parseStatus(const std::string &json);
bool parseSync(const std::string &json, SyncResponse &out);
// Accepte {config_hash, config:{mode,agents}} ou {config_hash, mode, agents}.
bool parseConfigResponse(const std::string &json, ConfigResponse &out);
bool parseApiError(const std::string &json, ApiError &out);

}  // namespace san
