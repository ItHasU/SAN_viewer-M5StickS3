#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <vector>
#include <cstring>
#include <fstream>
#include <sstream>

#include "san_audio.h"
#include "san_logic.h"
#include "san_protocol.h"
#include "sha256.h"

using namespace san;

static std::string fx(const char *name) {
  std::ifstream f(std::string("fixtures/") + name);
  if (!f) { TEST_FAIL_MESSAGE((std::string("fixture introuvable: ") + name).c_str()); }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void setUp() {}
void tearDown() {}

// ---------- sha256 / caps ----------
void test_sha256_vectors() {
  TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", sha256Hex("").c_str());
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha256Hex("abc").c_str());
  TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                           sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
  std::string million(1000000, 'a');
  TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", sha256Hex(million).c_str());
}
void test_caps_hash_trunc() {
  TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea", capsHash("abc").c_str());
  TEST_ASSERT_EQUAL(16, capsHash("x").size());
}
void test_caps_json_stable_and_valid() {
  std::string a = buildCapabilitiesJson("san-m5sticks3 0.1.0");
  TEST_ASSERT_EQUAL_STRING(a.c_str(), buildCapabilitiesJson("san-m5sticks3 0.1.0").c_str());
  TEST_ASSERT_NOT_NULL(strstr(a.c_str(), "\"protocol_version\":1"));
  TEST_ASSERT_NOT_NULL(strstr(a.c_str(), "\"pcm_s16le\""));
  TEST_ASSERT_NOT_NULL(strstr(a.c_str(), "\"max_duration_s\":10"));
  TEST_ASSERT_NOT_EQUAL(capsHash(a), capsHash(buildCapabilitiesJson("autre")));
}

// ---------- URL ----------
void test_normalize_base() {
  TEST_ASSERT_EQUAL_STRING("https://a.b", normalizeBase("https://a.b/").c_str());
  TEST_ASSERT_EQUAL_STRING("https://a.b", normalizeBase("https://a.b/api/v1/").c_str());
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.5:3000", normalizeBase("http://192.168.1.5:3000").c_str());
}
void test_choose_base() {
  TEST_ASSERT_EQUAL_STRING("http://nas:3000", chooseBase("https://pub.org", "http://nas:3000/", "Maison", "Maison").c_str());
  TEST_ASSERT_EQUAL_STRING("https://pub.org", chooseBase("https://pub.org", "http://nas:3000", "Cafe", "Maison").c_str());
  TEST_ASSERT_EQUAL_STRING("https://pub.org", chooseBase("https://pub.org", "", "Maison", "Maison").c_str());
  TEST_ASSERT_EQUAL_STRING("https://pub.org", chooseBase("https://pub.org", "http://nas", "Maison", "").c_str());
}
void test_sync_url() {
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/sync?ack=807&config_hash=3f9a1c&caps_hash=b71e02&battery=82",
                           syncUrl("https://h", 807, "3f9a1c", "b71e02", 82).c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/sync?ack=0", syncUrl("https://h", 0, "", "", -1).c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/sync", syncUrl("https://h", -1, "", "", -1).c_str());
}
void test_other_urls() {
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/device/register", registerUrl("https://h").c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/device/status", statusUrl("https://h").c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/voice/c_01J", voiceClipUrl("https://h", "c_01J").c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/voice?to=travail%2Fci%2Fx", voiceUploadUrl("https://h", "travail/ci/x").c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/config", configUrl("https://h").c_str());
  TEST_ASSERT_EQUAL_STRING("https://h/api/v1/viewer/capabilities", capabilitiesUrl("https://h").c_str());
}

// ---------- parsing ----------
void test_parse_sync_full() {
  SyncResponse r;
  TEST_ASSERT_TRUE(parseSync(fx("sync_full.json"), r));
  TEST_ASSERT_FALSE(r.capabilitiesRequired);
  TEST_ASSERT_FALSE(r.hasConfig);
  TEST_ASSERT_EQUAL_STRING("3f9a1c0000000000", r.configHash.c_str());
  TEST_ASSERT_EQUAL(2, r.states.size());
  TEST_ASSERT_EQUAL(40, r.states[0].progress);
  TEST_ASSERT_EQUAL(-1, r.states[1].progress);
  TEST_ASSERT_TRUE(r.states[1].stale);
  TEST_ASSERT_EQUAL_STRING("", r.states[1].detail.c_str());
  TEST_ASSERT_EQUAL(2, r.events.size());
  TEST_ASSERT_EQUAL(808, r.events[0].id);
  TEST_ASSERT_TRUE(r.events[0].isHigh());
  TEST_ASSERT_FALSE(r.events[0].hasClip);
  TEST_ASSERT_TRUE(r.events[1].hasClip);
  TEST_ASSERT_EQUAL_STRING("ima_adpcm", r.events[1].clip.format.c_str());
  TEST_ASSERT_EQUAL(8000, r.events[1].clip.sampleRate);
  TEST_ASSERT_EQUAL(4400, r.events[1].clip.durationMs);
  TEST_ASSERT_EQUAL(60, r.nextSyncS);
  TEST_ASSERT_EQUAL_STRING("Échec des tests", r.events[0].text.c_str());
}
void test_parse_sync_config() {
  SyncResponse r;
  TEST_ASSERT_TRUE(parseSync(fx("sync_with_config.json"), r));
  TEST_ASSERT_TRUE(r.capabilitiesRequired);
  TEST_ASSERT_TRUE(r.hasConfig);
  TEST_ASSERT_TRUE(r.hasMore);
  TEST_ASSERT_EQUAL(15, r.nextSyncS);
  TEST_ASSERT_EQUAL_STRING("manual", r.config.mode.c_str());
  TEST_ASSERT_EQUAL(3, r.config.agents.size());
  TEST_ASSERT_TRUE(r.config.agents[0].canSendVoice);
  TEST_ASSERT_TRUE(r.config.agents[1].muted);
  TEST_ASSERT_TRUE(r.config.agents[2].isNew);
  TEST_ASSERT_EQUAL(-1, r.config.agents[2].position);
}
void test_parse_sync_empty_and_bad() {
  SyncResponse r;
  TEST_ASSERT_TRUE(parseSync(fx("sync_empty.json"), r));
  TEST_ASSERT_EQUAL(0, r.events.size());
  TEST_ASSERT_FALSE(parseSync("not json", r));
  TEST_ASSERT_FALSE(parseSync("[1,2]", r));
}
void test_parse_register_status() {
  RegisterResponse r;
  TEST_ASSERT_TRUE(parseRegister(fx("register.json"), r));
  TEST_ASSERT_EQUAL_STRING("K7-42", r.pairingCode.c_str());
  TEST_ASSERT_EQUAL_STRING("pending", r.status.c_str());
  TEST_ASSERT_FALSE(parseRegister("{}", r));
  TEST_ASSERT_TRUE(parseStatus(fx("status_pending.json")) == DeviceStatus::Pending);
  TEST_ASSERT_TRUE(parseStatus(fx("status_active.json")) == DeviceStatus::Active);
  TEST_ASSERT_TRUE(parseStatus("garbage") == DeviceStatus::Unknown);
}
void test_parse_config_variants() {
  ConfigResponse c;
  TEST_ASSERT_TRUE(parseConfigResponse(fx("config_get.json"), c));
  TEST_ASSERT_EQUAL_STRING("1111222233334444", c.configHash.c_str());
  TEST_ASSERT_EQUAL(1, c.config.agents.size());
  TEST_ASSERT_TRUE(parseConfigResponse(fx("config_409.json"), c));
  TEST_ASSERT_EQUAL_STRING("9999888877776666", c.configHash.c_str());
  TEST_ASSERT_EQUAL(2, c.config.agents.size());
  TEST_ASSERT_TRUE(parseConfigResponse(
      "{\"config_hash\":\"h\",\"mode\":\"auto\",\"agents\":[{\"agent\":\"a\",\"subscribed\":true}]}", c));
  TEST_ASSERT_EQUAL_STRING("auto", c.config.mode.c_str());
  TEST_ASSERT_EQUAL_STRING("a", c.config.agents[0].name.c_str());  // nom par defaut = slug
  TEST_ASSERT_FALSE(parseConfigResponse("{\"x\":1}", c));
  ApiError e;
  TEST_ASSERT_TRUE(parseApiError(fx("error_401.json"), e));
  TEST_ASSERT_EQUAL_STRING("unauthorized", e.error.c_str());
}
void test_bodies() {
  std::string caps = buildCapabilitiesJson("fw");
  std::string reg = buildRegisterBody("m5sticks3-aabbcc", "M5StickS3", caps);
  TEST_ASSERT_NOT_NULL(strstr(reg.c_str(), "\"hardware_id\":\"m5sticks3-aabbcc\""));
  TEST_ASSERT_NOT_NULL(strstr(reg.c_str(), "\"capabilities\":{\"protocol_version\":1"));
  Config c;
  c.mode = "manual";
  AgentCfg a; a.agent = "build-1"; a.subscribed = true; a.position = 0;
  AgentCfg b; b.agent = "veille"; b.muted = true;
  c.agents = {a, b};
  std::string put = buildConfigPutBody("abc", c);
  TEST_ASSERT_EQUAL_STRING(
      "{\"base_hash\":\"abc\",\"subscriptions\":[{\"agent\":\"build-1\",\"subscribed\":true,\"position\":0,\"muted\":false},"
      "{\"agent\":\"veille\",\"subscribed\":false,\"position\":1,\"muted\":true}]}",
      put.c_str());
}

// ---------- machine a etats ----------
void test_phase_machine() {
  TEST_ASSERT_TRUE(phaseAfterBoot(false, false) == Phase::Register);
  TEST_ASSERT_TRUE(phaseAfterBoot(false, true) == Phase::Register);
  TEST_ASSERT_TRUE(phaseAfterBoot(true, false) == Phase::Pairing);
  TEST_ASSERT_TRUE(phaseAfterBoot(true, true) == Phase::Run);
  TEST_ASSERT_TRUE(phaseOnRegistered() == Phase::Pairing);
  TEST_ASSERT_TRUE(phaseOnStatus(Phase::Pairing, DeviceStatus::Active) == Phase::Run);
  TEST_ASSERT_TRUE(phaseOnStatus(Phase::Pairing, DeviceStatus::Pending) == Phase::Pairing);
  TEST_ASSERT_TRUE(phaseOnStatus(Phase::Pairing, DeviceStatus::Unknown) == Phase::Pairing);
  for (Phase p : {Phase::Boot, Phase::Register, Phase::Pairing, Phase::Run})
    TEST_ASSERT_TRUE(phaseOnHttp(p, 401) == Phase::Register);
  TEST_ASSERT_TRUE(phaseOnHttp(Phase::Run, 403) == Phase::Pairing);
  TEST_ASSERT_TRUE(phaseOnHttp(Phase::Run, 200) == Phase::Run);
  TEST_ASSERT_TRUE(httpCodeClearsToken(401));
  TEST_ASSERT_FALSE(httpCodeClearsToken(403));
}

// ---------- ack ----------
void test_ack() {
  SyncResponse r;
  parseSync(fx("sync_full.json"), r);
  TEST_ASSERT_EQUAL(807, ackAfterProcessing(807, r.events, 0));
  TEST_ASSERT_EQUAL(808, ackAfterProcessing(807, r.events, 1));
  TEST_ASSERT_EQUAL(809, ackAfterProcessing(807, r.events, 2));
  TEST_ASSERT_EQUAL(809, ackAfterProcessing(807, r.events, 99));
  TEST_ASSERT_EQUAL(900, ackAfterProcessing(900, r.events, 2));  // ne recule jamais
  TEST_ASSERT_EQUAL(5, advanceAck(5, 3));
}

// ---------- decision ecran ----------
static Config cfg2() {
  SyncResponse r;
  parseSync(fx("sync_with_config.json"), r);
  return r.config;  // build-1 actif, veille mute, nouveau non suivi
}
void test_screen_events() {
  Config c = cfg2();
  SyncResponse s;
  parseSync(fx("sync_full.json"), s);
  ScreenDecision d = decideScreen(c, {}, s.states, s.events);
  TEST_ASSERT_TRUE(d.screen);
  TEST_ASSERT_TRUE(d.sound);
  TEST_ASSERT_TRUE(d.highPriority);
  TEST_ASSERT_EQUAL(1, d.alertEvents);  // l'evenement de "veille" (mute) est ignore
}
void test_screen_muted_only() {
  Config c = cfg2();
  EventItem e; e.id = 1; e.agent = "veille"; e.kind = "nudge";
  ScreenDecision d = decideScreen(c, {}, {}, {e});
  TEST_ASSERT_FALSE(d.screen);
  TEST_ASSERT_FALSE(d.sound);
}
void test_screen_state_change() {
  Config c = cfg2();
  AgentState s; s.agent = "build-1"; s.status = "error";
  TEST_ASSERT_TRUE(decideScreen(c, {{"build-1", "working"}}, {s}, {}).screen);
  TEST_ASSERT_FALSE(decideScreen(c, {{"build-1", "error"}}, {s}, {}).screen);  // inchange
  TEST_ASSERT_FALSE(decideScreen(c, {{"build-1", "working"}}, {s}, {}).sound);
  s.status = "working";
  TEST_ASSERT_FALSE(decideScreen(c, {{"build-1", "idle"}}, {s}, {}).screen);   // pas notable
  s.status = "error"; s.stale = true;
  TEST_ASSERT_FALSE(decideScreen(c, {}, {s}, {}).screen);                     // perime
  s.stale = false; s.agent = "veille";
  TEST_ASSERT_FALSE(decideScreen(c, {}, {s}, {}).screen);                     // mute
  s.agent = "nouveau";
  TEST_ASSERT_FALSE(decideScreen(c, {}, {s}, {}).screen);                     // non suivi
}
void test_order_states() {
  Config c = cfg2();
  AgentState a; a.agent = "veille";
  AgentState b; b.agent = "build-1";
  AgentState z; z.agent = "nouveau";
  auto o = orderStates(c, {a, b, z});
  TEST_ASSERT_EQUAL(2, o.size());
  TEST_ASSERT_EQUAL_STRING("build-1", o[0].agent.c_str());
  TEST_ASSERT_EQUAL(2, orderStates(Config(), {a, b}).size());
}

// ---------- texte / veille ----------
void test_truncate() {
  TEST_ASSERT_EQUAL_STRING("abc", truncateChars("abc", 5).c_str());
  TEST_ASSERT_EQUAL_STRING("abcde", truncateChars("abcde", 5).c_str());
  TEST_ASSERT_EQUAL_STRING("ab...", truncateChars("abcdefgh", 5).c_str());
  TEST_ASSERT_EQUAL_STRING("Éc...", truncateChars("Échec des tests", 5).c_str());  // pas de coupure UTF-8
  TEST_ASSERT_EQUAL_STRING("abcdefgh", truncateChars("abcdefgh", 0).c_str());
  TEST_ASSERT_EQUAL_STRING("ab", truncateChars("abcdefgh", 2).c_str());
  TEST_ASSERT_EQUAL(6, utf8Length("é€abcd"));
  std::string s(200, 'x');
  TEST_ASSERT_EQUAL(80, utf8Length(truncateChars(s, 80)));
}
void test_sleep() {
  TEST_ASSERT_EQUAL(60, computeSleepSeconds(0, 0));
  TEST_ASSERT_EQUAL(30, computeSleepSeconds(30, 0));
  TEST_ASSERT_EQUAL(10, computeSleepSeconds(1, 0));
  TEST_ASSERT_EQUAL(3600, computeSleepSeconds(99999, 0));
  TEST_ASSERT_EQUAL(120, computeSleepSeconds(60, 1));
  TEST_ASSERT_EQUAL(900, computeSleepSeconds(60, 9));
}
void test_config_edit() {
  Config c = cfg2();
  toggleSubscribed(c, 2);
  TEST_ASSERT_TRUE(c.agents[2].subscribed);
  TEST_ASSERT_FALSE(c.agents[2].isNew);
  toggleSubscribed(c, 99);  // hors limites : sans effet
  toggleMuted(c, 0);
  TEST_ASSERT_TRUE(c.agents[0].muted);
  TEST_ASSERT_TRUE(configDiffers(c, cfg2()));
  TEST_ASSERT_FALSE(configDiffers(cfg2(), cfg2()));
  Config au = cfg2(); au.mode = "auto";
  toggleSubscribed(au, 2);
  TEST_ASSERT_FALSE(au.agents[2].subscribed);  // sans effet en mode auto
}

// ---------- audio ----------
// Encodeur IMA ADPCM de test (meme algorithme que le decodeur, cote encodeur).
static const int stepTab[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int idxTab[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static uint8_t encNib(int16_t s, int &pred, int &idx) {
  int step = stepTab[idx], diff = s - pred;
  uint8_t nib = 0;
  if (diff < 0) { nib = 8; diff = -diff; }
  int d = step >> 3;
  if (diff >= step) { nib |= 4; diff -= step; d += step; }
  if (diff >= step / 2) { nib |= 2; diff -= step / 2; d += step / 2; }
  if (diff >= step / 4) { nib |= 1; d += step / 4; }
  pred += (nib & 8) ? -d : d;
  if (pred > 32767) pred = 32767;
  if (pred < -32768) pred = -32768;
  idx += idxTab[nib];
  if (idx < 0) idx = 0;
  if (idx > 88) idx = 88;
  return nib;
}
static std::vector<uint8_t> makeAdpcmWav(const std::vector<int16_t> &pcm, int rate, int blockAlign) {
  int spb = (blockAlign - 4) * 2 + 1;
  std::vector<uint8_t> data;
  size_t pos = 0;
  int idx = 0;  // l'index de pas est reporte d'un bloc a l'autre (comme un vrai encodeur)
  while (pos < pcm.size()) {
    int pred = pcm[pos];
    data.push_back(pred & 0xFF); data.push_back((pred >> 8) & 0xFF); data.push_back(idx); data.push_back(0);
    pos++;
    for (int i = 0; i < (blockAlign - 4); i++) {
      uint8_t lo = pos < pcm.size() ? encNib(pcm[pos], pred, idx) : 0; pos++;
      uint8_t hi = pos < pcm.size() ? encNib(pcm[pos], pred, idx) : 0; pos++;
      data.push_back(lo | (hi << 4));
    }
    (void)spb;
  }
  std::vector<uint8_t> w(12 + 8 + 20 + 8, 0);
  auto p32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) w[o + i] = v >> (8 * i); };
  auto p16 = [&](size_t o, uint16_t v) { w[o] = v; w[o + 1] = v >> 8; };
  memcpy(&w[0], "RIFF", 4); p32(4, 36 + data.size()); memcpy(&w[8], "WAVE", 4);
  memcpy(&w[12], "fmt ", 4); p32(16, 20); p16(20, 0x11); p16(22, 1); p32(24, rate); p32(28, rate * blockAlign / spb);
  p16(32, blockAlign); p16(34, 4); p16(36, 2); p16(38, spb);
  memcpy(&w[40], "data", 4); p32(44, data.size());
  w.insert(w.end(), data.begin(), data.end());
  return w;
}

void test_wav_pcm_header_roundtrip() {
  uint8_t h[44];
  buildWavHeader(h, 16000, 32000);
  std::vector<uint8_t> f(h, h + 44);
  f.resize(44 + 32000, 0);
  WavInfo w = parseWav(f.data(), f.size());
  TEST_ASSERT_TRUE(w.valid);
  TEST_ASSERT_EQUAL(1, w.format);
  TEST_ASSERT_EQUAL(1, w.channels);
  TEST_ASSERT_EQUAL(16000, w.sampleRate);
  TEST_ASSERT_EQUAL(16, w.bits);
  TEST_ASSERT_EQUAL(44, w.dataOffset);
  TEST_ASSERT_EQUAL(32000, w.dataLen);
}
void test_wav_streaming_size_and_list_chunk() {
  uint8_t h[44];
  buildWavHeader(h, 8000, 0xFFFFFFFFu);
  std::vector<uint8_t> f(h, h + 44);
  f.resize(44 + 100);
  WavInfo w = parseWav(f.data(), f.size());
  TEST_ASSERT_TRUE(w.valid);
  TEST_ASSERT_EQUAL(100, w.dataLen);  // borne a ce qui est disponible
  // chunk LIST insere entre fmt et data
  std::vector<uint8_t> g(h, h + 36);
  const uint8_t list[] = {'L','I','S','T', 5,0,0,0, 'a','b','c','d','e', 0};  // 5 + padding
  g.insert(g.end(), list, list + sizeof list);
  g.insert(g.end(), h + 36, h + 44);
  g.resize(g.size() + 10);
  WavInfo w2 = parseWav(g.data(), g.size());
  TEST_ASSERT_TRUE(w2.valid);
  TEST_ASSERT_EQUAL(36 + 14 + 8, w2.dataOffset);
}
void test_wav_invalid() {
  uint8_t junk[20] = {0};
  TEST_ASSERT_FALSE(parseWav(junk, sizeof junk).valid);
  TEST_ASSERT_FALSE(parseWav(junk, 4).valid);
}
void test_ima_roundtrip() {
  std::vector<int16_t> pcm;
  for (int i = 0; i < 2000; i++) pcm.push_back((int16_t)(8000.0 * sin(i * 0.05)));
  auto wav = makeAdpcmWav(pcm, 8000, 256);
  WavInfo w = parseWav(wav.data(), wav.size());
  TEST_ASSERT_TRUE(w.valid);
  TEST_ASSERT_EQUAL(0x11, w.format);
  TEST_ASSERT_EQUAL(505, w.samplesPerBlock);
  size_t n = imaSampleCount(w);
  TEST_ASSERT_TRUE(n >= pcm.size());
  std::vector<int16_t> out(n);
  size_t got = imaDecode(w, wav.data(), out.data(), out.size());
  TEST_ASSERT_EQUAL(n, got);
  // erreur de quantification bornee (signal lisse)
  long maxErr = 0;
  TEST_ASSERT_EQUAL(pcm[0], out[0]);
  for (size_t i = 64; i < pcm.size(); i++) {  // laisse l'adaptation de pas s'etablir
    long e = labs((long)out[i] - pcm[i]);
    if (e > maxErr) maxErr = e;
  }
  { char m[64]; snprintf(m, sizeof m, "maxErr=%ld", maxErr); TEST_ASSERT_TRUE_MESSAGE(maxErr < 800, m); }
  // sortie plafonnee
  std::vector<int16_t> small(10);
  TEST_ASSERT_EQUAL(10, imaDecode(w, wav.data(), small.data(), small.size()));
}

// ---------- fichiers produits par la commande ffmpeg exacte du serveur (tools/gen-fixtures.sh) ----------
static std::vector<uint8_t> fxBin(const char *name) {
  std::ifstream f(std::string("fixtures/audio/") + name, std::ios::binary);
  if (!f) { TEST_FAIL_MESSAGE((std::string("fixture audio introuvable (tools/gen-fixtures.sh): ") + name).c_str()); }
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Decode comme playWav() : renvoie les echantillons mono et la frequence.
static std::vector<int16_t> decodeLikePlayer(const std::vector<uint8_t> &wav, uint32_t &rate, uint16_t &fmt) {
  WavInfo w = parseWav(wav.data(), wav.size());
  TEST_ASSERT_TRUE(w.valid);
  TEST_ASSERT_EQUAL(1, w.channels);
  rate = w.sampleRate;
  fmt = w.format;
  std::vector<int16_t> out;
  if (w.format == 1) {
    TEST_ASSERT_EQUAL(16, w.bits);
    out.resize(w.dataLen / 2);
    memcpy(out.data(), wav.data() + w.dataOffset, out.size() * 2);
  } else {
    TEST_ASSERT_EQUAL(0x11, w.format);
    out.resize(imaSampleCount(w));
    TEST_ASSERT_EQUAL(out.size(), imaDecode(w, wav.data(), out.data(), out.size()));
  }
  return out;
}

// Sinusoide ffmpeg `sine=frequency=440` : amplitude 1/8 de la pleine echelle (4096), 1,0 s.
static void checkSine(const std::vector<int16_t> &pcm, uint32_t rate, double tolRms) {
  TEST_ASSERT_EQUAL_UINT32(rate, pcm.size());  // duree exacte : 1,0 s (le remplissage du dernier bloc ADPCM est ignore)
  double sum = 0; int peak = 0, crossings = 0;
  for (size_t i = 0; i < pcm.size(); i++) {
    sum += (double)pcm[i] * pcm[i];
    if (abs(pcm[i]) > peak) peak = abs(pcm[i]);
    if (i && pcm[i - 1] < 0 && pcm[i] >= 0) crossings++;
  }
  double rms = sqrt(sum / pcm.size());
  char m[96];
  snprintf(m, sizeof m, "rms=%.0f (attendu ~2896)", rms);
  TEST_ASSERT_TRUE_MESSAGE(fabs(rms - 2896.0) < 2896.0 * tolRms, m);
  snprintf(m, sizeof m, "peak=%d (attendu ~4096)", peak);
  TEST_ASSERT_TRUE_MESSAGE(abs(peak - 4096) < 4096 * tolRms * 1.5, m);
  snprintf(m, sizeof m, "passages par zero=%d (attendu 440)", crossings);
  TEST_ASSERT_TRUE_MESSAGE(abs(crossings - 440) <= 2, m);
}

void test_server_pcm_fixtures() {
  for (uint32_t rate : {8000u, 16000u}) {
    auto wav = fxBin(rate == 8000 ? "pcm_s16le-8000.wav" : "pcm_s16le-16000.wav");
    uint32_t r; uint16_t f;
    auto pcm = decodeLikePlayer(wav, r, f);
    TEST_ASSERT_EQUAL_UINT32(rate, r);
    TEST_ASSERT_EQUAL(1, f);
    checkSine(pcm, rate, 0.01);
  }
}

void test_server_ima_adpcm_fixtures() {
  for (uint32_t rate : {8000u, 16000u}) {
    auto wav = fxBin(rate == 8000 ? "ima_adpcm-8000.wav" : "ima_adpcm-16000.wav");
    auto ref = fxBin(rate == 8000 ? "pcm_s16le-8000.wav" : "pcm_s16le-16000.wav");
    WavInfo w = parseWav(wav.data(), wav.size());
    TEST_ASSERT_TRUE(w.valid);
    TEST_ASSERT_EQUAL(0x11, w.format);
    TEST_ASSERT_EQUAL_UINT32(rate, w.sampleRate);
    TEST_ASSERT_EQUAL(1024, w.blockAlign);        // ffmpeg : blocs de 1024 octets
    TEST_ASSERT_EQUAL(2041, w.samplesPerBlock);   // (1024-4)*2+1
    TEST_ASSERT_EQUAL_UINT32(rate, w.factSamples);  // chunk 'fact' = 1,0 s exactes
    TEST_ASSERT_TRUE(imaSampleCount(w) == rate);   // sans 'fact', le dernier bloc ajouterait du silence
    uint32_t r; uint16_t f;
    auto pcm = decodeLikePlayer(wav, r, f);
    TEST_ASSERT_EQUAL_UINT32(rate, r);
    checkSine(pcm, rate, 0.05);
    // proche de la version PCM du meme signal (quantification 4 bits)
    uint32_t r2; uint16_t f2;
    auto exact = decodeLikePlayer(ref, r2, f2);
    long maxErr = 0;
    for (size_t i = 64; i < pcm.size(); i++) maxErr = std::max(maxErr, labs((long)pcm[i] - exact[i]));
    char m[64]; snprintf(m, sizeof m, "ecart max ADPCM/PCM = %ld", maxErr);
    TEST_ASSERT_TRUE_MESSAGE(maxErr < 400, m);
  }
}

void test_server_ima_without_fact_pads_last_block() {
  // Meme fichier sans chunk 'fact' : le decodeur voit le remplissage du dernier bloc (comportement documente).
  auto wav = fxBin("ima_adpcm-16000.wav");
  WavInfo w = parseWav(wav.data(), wav.size());
  w.factSamples = 0;
  size_t n = imaSampleCount(w);
  TEST_ASSERT_TRUE(n > 16000 && n < 16000 + 2041);
}

// Reponse reelle du serveur SAN (capturee sur le vrai serveur, sync avec nudge + clip TTS ima_adpcm)
void test_parse_real_server_sync() {
  SyncResponse r;
  TEST_ASSERT_TRUE(parseSync(fx("server_sync.json"), r));
  TEST_ASSERT_TRUE(r.capabilitiesRequired);
  TEST_ASSERT_TRUE(r.hasConfig);
  TEST_ASSERT_EQUAL_STRING("60a6ebb52e6e7aa6", r.configHash.c_str());
  TEST_ASSERT_EQUAL(1, r.config.agents.size());
  TEST_ASSERT_TRUE(r.config.agents[0].subscribed);
  TEST_ASSERT_EQUAL(0, r.config.agents[0].position);
  TEST_ASSERT_EQUAL(1, r.states.size());
  TEST_ASSERT_EQUAL_STRING("working", r.states[0].status.c_str());
  TEST_ASSERT_EQUAL(40, r.states[0].progress);
  TEST_ASSERT_EQUAL_STRING("Compilation", r.states[0].detail.c_str());
  TEST_ASSERT_EQUAL(2, r.events.size());
  TEST_ASSERT_TRUE(r.events[0].isHigh());
  TEST_ASSERT_FALSE(r.events[0].hasClip);
  TEST_ASSERT_TRUE(r.events[1].hasClip);
  TEST_ASSERT_EQUAL_STRING("ima_adpcm", r.events[1].clip.format.c_str());
  TEST_ASSERT_EQUAL(16000, r.events[1].clip.sampleRate);
  TEST_ASSERT_EQUAL(4190, r.events[1].clip.size);
  TEST_ASSERT_EQUAL(500, r.events[1].clip.durationMs);
  TEST_ASSERT_EQUAL(60, r.nextSyncS);
}

// Document de capacites exact (octet pour octet) : tools/device-sim.py du serveur envoie la meme chaine.
void test_caps_json_exact() {
  const char *expected = R"JSON({"protocol_version":1,"firmware":"san-m5sticks3 0.1.0","display":{"width":135,"height":240,"color":true,"max_detail_chars":80},"audio_out":{"formats":["ima_adpcm","pcm_s16le"],"sample_rates":[8000,16000]},"audio_in":{"formats":["pcm_s16le"],"sample_rate":16000,"max_duration_s":10},"nudge":["sound","screen"],"input":["buttons"],"battery":true})JSON";
  TEST_ASSERT_EQUAL_STRING(expected, buildCapabilitiesJson("san-m5sticks3 0.1.0").c_str());
  TEST_ASSERT_EQUAL_STRING("065921302cd8ee02", capsHash(expected).c_str());  // hash renvoye par le vrai serveur
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_sha256_vectors);
  RUN_TEST(test_caps_hash_trunc);
  RUN_TEST(test_caps_json_stable_and_valid);
  RUN_TEST(test_normalize_base);
  RUN_TEST(test_choose_base);
  RUN_TEST(test_sync_url);
  RUN_TEST(test_other_urls);
  RUN_TEST(test_parse_sync_full);
  RUN_TEST(test_parse_sync_config);
  RUN_TEST(test_parse_sync_empty_and_bad);
  RUN_TEST(test_parse_register_status);
  RUN_TEST(test_parse_config_variants);
  RUN_TEST(test_bodies);
  RUN_TEST(test_phase_machine);
  RUN_TEST(test_ack);
  RUN_TEST(test_screen_events);
  RUN_TEST(test_screen_muted_only);
  RUN_TEST(test_screen_state_change);
  RUN_TEST(test_order_states);
  RUN_TEST(test_truncate);
  RUN_TEST(test_sleep);
  RUN_TEST(test_config_edit);
  RUN_TEST(test_wav_pcm_header_roundtrip);
  RUN_TEST(test_wav_streaming_size_and_list_chunk);
  RUN_TEST(test_wav_invalid);
  RUN_TEST(test_ima_roundtrip);
  RUN_TEST(test_parse_real_server_sync);
  RUN_TEST(test_caps_json_exact);
  RUN_TEST(test_server_pcm_fixtures);
  RUN_TEST(test_server_ima_adpcm_fixtures);
  RUN_TEST(test_server_ima_without_fact_pads_last_block);
  return UNITY_END();
}
