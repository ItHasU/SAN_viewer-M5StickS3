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
#include "san_notify.h"
#include "san_ui.h"
#include "san_protocol.h"
#include "san_sound.h"
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
void test_parse_api_error() {
  ApiError e;
  TEST_ASSERT_TRUE(parseApiError(fx("error_401.json"), e));
  TEST_ASSERT_EQUAL_STRING("unauthorized", e.error.c_str());
}
void test_bodies() {
  std::string caps = buildCapabilitiesJson("fw");
  std::string reg = buildRegisterBody("m5sticks3-aabbcc", "M5StickS3", caps);
  TEST_ASSERT_NOT_NULL(strstr(reg.c_str(), "\"hardware_id\":\"m5sticks3-aabbcc\""));
  TEST_ASSERT_NOT_NULL(strstr(reg.c_str(), "\"capabilities\":{\"protocol_version\":1"));
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
  return r.config;  // build-1, veille, nouveau (champs subscribed/muted du fixture ignores)
}
void test_screen_events() {
  Config c = cfg2();
  SyncResponse s;
  parseSync(fx("sync_full.json"), s);
  ScreenDecision d = decideScreen(c, {}, s.states, s.events);
  TEST_ASSERT_TRUE(d.screen);
  TEST_ASSERT_TRUE(d.sound);
  TEST_ASSERT_TRUE(d.highPriority);
  TEST_ASSERT_EQUAL(2, d.alertEvents);  // tous les evenements comptent
}
void test_screen_ignores_filters() {
  // Plus de filtre local : un evenement d'un agent "muet" ou "non suivi" cote serveur alerte comme les autres.
  Config c = cfg2();
  EventItem e; e.id = 1; e.agent = "veille"; e.kind = "nudge";
  ScreenDecision d = decideScreen(c, {}, {}, {e});
  TEST_ASSERT_TRUE(d.screen);
  TEST_ASSERT_TRUE(d.sound);
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
  TEST_ASSERT_TRUE(decideScreen(c, {}, {s}, {}).screen);                      // "muet" cote serveur : ignore
  s.agent = "nouveau";
  TEST_ASSERT_TRUE(decideScreen(c, {}, {s}, {}).screen);                      // "non suivi" : ignore
}
void test_order_states() {
  Config c = cfg2();
  AgentState a; a.agent = "veille";
  AgentState b; b.agent = "build-1";
  AgentState z; z.agent = "inconnu";
  auto o = orderStates(c, {a, b, z});
  TEST_ASSERT_EQUAL(2, o.size());  // ordre de la config ; etat hors config ignore
  TEST_ASSERT_EQUAL_STRING("build-1", o[0].agent.c_str());
  TEST_ASSERT_EQUAL(2, orderStates(Config(), {a, b}).size());
}
void test_all_config_agents_displayed() {
  // Le fixture marque "veille" comme muet et "nouveau" comme non suivi : la carte les affiche quand meme.
  Config c = cfg2();
  TEST_ASSERT_EQUAL(3, c.agents.size());
  AgentState a; a.agent = "build-1"; a.status = "working";
  AgentState b; b.agent = "veille"; b.status = "idle";
  AgentState n; n.agent = "nouveau"; n.status = "done";
  auto o = orderStates(c, {n, b, a});
  TEST_ASSERT_EQUAL(3, o.size());
  TEST_ASSERT_EQUAL_STRING("build-1", o[0].agent.c_str());
  TEST_ASSERT_EQUAL_STRING("veille", o[1].agent.c_str());
  TEST_ASSERT_EQUAL_STRING("nouveau", o[2].agent.c_str());
  Activity act = summarizeActivity(o);
  TEST_ASSERT_EQUAL(3, act.followed);
  TEST_ASSERT_EQUAL(1, act.working);
  TEST_ASSERT_EQUAL(3, (int)buildScene(o, 0).agents.size());
  // Un changement vers "done" d'un agent "non suivi" allume l'ecran.
  TEST_ASSERT_TRUE(decideScreen(c, {{"nouveau", "working"}}, {n}, {}).screen);
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

static void test_status_icon() {
  TEST_ASSERT_TRUE(statusIcon("working", false).icon == Icon::Working);
  TEST_ASSERT_TRUE(statusIcon("waiting", false).icon == Icon::Waiting);
  TEST_ASSERT_TRUE(statusIcon("error", false).icon == Icon::Error);
  TEST_ASSERT_TRUE(statusIcon("done", false).icon == Icon::Done);
  TEST_ASSERT_TRUE(statusIcon("idle", false).icon == Icon::Idle);
  TEST_ASSERT_TRUE(statusIcon("???", false).icon == Icon::Idle);
  TEST_ASSERT_TRUE(statusIcon("error", true).icon == Icon::Stale);
  TEST_ASSERT_NOT_EQUAL(statusIcon("done", false).color, statusIcon("error", false).color);
}

// ---------- types de nudge / sons ----------
static const NudgeType kAllTypes[] = {NudgeType::Start, NudgeType::Notification, NudgeType::Question,
                                      NudgeType::Done, NudgeType::Error};

void test_nudge_type_parse() {
  TEST_ASSERT_TRUE(parseNudgeType("start") == NudgeType::Start);
  TEST_ASSERT_TRUE(parseNudgeType("notification") == NudgeType::Notification);
  TEST_ASSERT_TRUE(parseNudgeType("question") == NudgeType::Question);
  TEST_ASSERT_TRUE(parseNudgeType("done") == NudgeType::Done);
  TEST_ASSERT_TRUE(parseNudgeType("error") == NudgeType::Error);
  TEST_ASSERT_TRUE(parseNudgeType("") == NudgeType::Notification);
  TEST_ASSERT_TRUE(parseNudgeType("banana") == NudgeType::Notification);
  TEST_ASSERT_TRUE(parseNudgeType("ERROR") == NudgeType::Notification);
}

void test_nudge_type_in_sync() {
  const char *json =
      "{\"server_time\":\"t\",\"events\":["
      "{\"id\":1,\"agent\":\"a\",\"kind\":\"nudge\",\"priority\":\"normal\",\"text\":\"x\"},"
      "{\"id\":2,\"agent\":\"a\",\"kind\":\"nudge\",\"priority\":\"high\",\"type\":\"question\",\"text\":\"x\"},"
      "{\"id\":3,\"agent\":\"a\",\"kind\":\"nudge\",\"type\":\"zzz\"},"
      "{\"id\":4,\"agent\":\"a\",\"kind\":\"nudge\",\"type\":7},"
      "{\"id\":5,\"agent\":\"a\",\"kind\":\"nudge\",\"type\":\"error\"}]}";
  SyncResponse sr;
  TEST_ASSERT_TRUE(parseSync(json, sr));
  TEST_ASSERT_EQUAL(5, (int)sr.events.size());
  TEST_ASSERT_TRUE(sr.events[0].type == NudgeType::Notification);
  TEST_ASSERT_TRUE(sr.events[1].type == NudgeType::Question);
  TEST_ASSERT_TRUE(sr.events[1].isHigh());
  TEST_ASSERT_TRUE(sr.events[2].type == NudgeType::Notification);
  TEST_ASSERT_TRUE(sr.events[3].type == NudgeType::Notification);
  TEST_ASSERT_TRUE(sr.events[4].type == NudgeType::Error);
}

static std::vector<int16_t> renderAll(NudgeType t, size_t block) {
  size_t total = nudgeSound(t).samples;
  std::vector<int16_t> pcm(total + block);
  for (size_t off = 0; off < total; off += block) soundRender(t, off, &pcm[off], block);  // synthese par blocs
  pcm.resize(total);
  return pcm;
}

void test_sound_durations_and_notes() {
  const uint32_t expectMs[] = {1060, 1200, 1400, 1700, 1380};  // SONS.md section 3, queue de 0,3 s comprise
  const size_t expectNotes[] = {2, 1, 2, 3, 2};
  for (size_t i = 0; i < 5; i++) {
    const SoundSpec &s = nudgeSound(kAllTypes[i]);
    TEST_ASSERT_EQUAL(expectNotes[i], s.count);
    TEST_ASSERT_EQUAL(expectMs[i], nudgeSoundMs(kAllTypes[i]));
    float end = 0;
    for (size_t k = 0; k < s.count; k++) {
      TEST_ASSERT_TRUE(s.notes[k].freq >= 100 && s.notes[k].freq * (s.rounded ? 3 : 4) < kSoundRate / 2.0f);  // sous Nyquist
      end = std::max(end, s.notes[k].start + s.notes[k].dur);
    }
    TEST_ASSERT_EQUAL((long)lroundf((end + 0.3f) * kSoundRate), (long)s.samples);  // derniere note + 0,3 s
  }
  TEST_ASSERT_EQUAL(27200, (int)nudgeSound(NudgeType::Done).samples);  // le plus long
}

void test_sound_peak_normalised() {
  for (NudgeType t : kAllTypes) {
    auto pcm = renderAll(t, 512);
    int pk = 0;
    for (int16_t v : pcm) pk = std::max(pk, std::abs((int)v));
    float want = kSoundPeak * (t == NudgeType::Error ? kErrorBoost : 1.0f);
    float got = pk / 32767.0f;
    TEST_ASSERT_FLOAT_WITHIN(want * 0.02f, want, got);  // 0,35 +/- 2 %
    TEST_ASSERT_TRUE(pk < 32767);                       // pas de depassement 16 bits
  }
}

void test_sound_no_click_and_block_independent() {
  for (NudgeType t : kAllTypes) {
    auto a = renderAll(t, 512);
    TEST_ASSERT_TRUE(std::abs((int)a.front()) <= 60);   // attaque progressive : demarre pres de 0
    TEST_ASSERT_EQUAL(0, a.back());                     // queue silencieuse
    // la taille de bloc ne change pas le resultat
    auto b = renderAll(t, 100);
    TEST_ASSERT_TRUE(a == b);
    // rampe de coupure : aucun saut brutal entre echantillons voisins (pas de clic)
    int maxStep = 0;
    for (size_t i = 1; i < a.size(); i++) maxStep = std::max(maxStep, std::abs((int)a[i] - (int)a[i - 1]));
    TEST_ASSERT_TRUE_MESSAGE(maxStep < 8000, "saut brutal (clic)");  // pente max d'un sinus 3,5 kHz ~ 6000 ; un clic ferait ~ le pic (11469)
  }
  // au-dela de la fin : silence
  int16_t out[8];
  soundRender(NudgeType::Notification, 19200 + 5, out, 8);
  for (int16_t v : out) TEST_ASSERT_EQUAL(0, v);
}

void test_sound_error_is_low_and_distinct() {
  const SoundSpec &e = nudgeSound(NudgeType::Error);
  TEST_ASSERT_TRUE(e.rounded);
  TEST_ASSERT_TRUE(e.notes[0].freq < 300 && e.notes[1].freq < e.notes[0].freq);
  for (size_t i = 0; i < 5; i++)
    for (size_t j = i + 1; j < 5; j++) {
      TEST_ASSERT_TRUE(renderAll(kAllTypes[i], 512) != renderAll(kAllTypes[j], 512));
      NudgeStyle a = nudgeStyle(kAllTypes[i]), b = nudgeStyle(kAllTypes[j]);
      TEST_ASSERT_TRUE(a.icon != b.icon);
      TEST_ASSERT_TRUE(a.color != b.color);
    }
}

void test_sound_priority_order() {
  TEST_ASSERT_TRUE(soundPriority(NudgeType::Error) > soundPriority(NudgeType::Question));
  TEST_ASSERT_TRUE(soundPriority(NudgeType::Question) > soundPriority(NudgeType::Done));
  TEST_ASSERT_TRUE(soundPriority(NudgeType::Done) > soundPriority(NudgeType::Notification));
  TEST_ASSERT_TRUE(soundPriority(NudgeType::Notification) > soundPriority(NudgeType::Start));
}

void test_sound_scheduler_rules() {
  SoundScheduler s;
  NudgeType t;
  TEST_ASSERT_FALSE(s.poll(0, false, t));  // rien a jouer
  // start muet par defaut
  s.request(NudgeType::Start);
  TEST_ASSERT_FALSE(s.pending());
  // un seul son a la fois : une demande pendant la lecture attend la fin
  s.request(NudgeType::Done);
  TEST_ASSERT_TRUE(s.poll(1000, false, t));
  TEST_ASSERT_TRUE(t == NudgeType::Done);
  s.request(NudgeType::Question);
  TEST_ASSERT_FALSE(s.poll(1500, true, t));  // en cours
  TEST_ASSERT_FALSE(s.poll(2700, false, t)); // fini a 2700 : delai de 2 s
  TEST_ASSERT_FALSE(s.poll(4699, false, t));
  TEST_ASSERT_TRUE(s.poll(4700, false, t));
  TEST_ASSERT_TRUE(t == NudgeType::Question);
  // les demandes proches fusionnent : seul le type le plus prioritaire joue
  s.poll(5000, true, t);
  s.poll(6000, false, t);  // fin du son a 6000
  s.request(NudgeType::Notification);
  s.request(NudgeType::Error);
  s.request(NudgeType::Done);
  TEST_ASSERT_FALSE(s.poll(7000, false, t));
  TEST_ASSERT_TRUE(s.poll(8000, false, t));
  TEST_ASSERT_TRUE(t == NudgeType::Error);
  TEST_ASSERT_FALSE(s.pending());
  TEST_ASSERT_FALSE(s.poll(20000, true, t));  // pas de second son
  // premier son d'un reveil : pas de delai
  SoundScheduler f;
  f.request(NudgeType::Notification);
  TEST_ASSERT_TRUE(f.poll(5, false, t));
}

// ---------- file de notifications ----------
void test_notif_push_merge_order() {
  NotifQueue q;
  q.push("a", NudgeType::Question, false, false, 1);
  q.push("b", NudgeType::Done, false, false, 2);
  q.push("a", NudgeType::Question, false, true, 3);  // fusion : redevient la plus recente
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_EQUAL(3, q.total());
  TEST_ASSERT_EQUAL_STRING("a", q.recent(0)->agent.c_str());
  TEST_ASSERT_EQUAL(2, q.recent(0)->count);
  TEST_ASSERT_TRUE(q.recent(0)->high);
  TEST_ASSERT_EQUAL(3, (int)q.recent(0)->id);
  TEST_ASSERT_EQUAL_STRING("b", q.recent(1)->agent.c_str());
  TEST_ASSERT_NULL(q.recent(2));
  // type ou voix differents : pas de fusion
  q.push("a", NudgeType::Done, false, false, 4);
  q.push("a", NudgeType::Done, true, false, 5);
  TEST_ASSERT_EQUAL(4, (int)q.size());
}

void test_notif_ack_and_capacity() {
  NotifQueue q;
  for (int i = 0; i < 20; i++) q.push("ag" + std::to_string(i), NudgeType::Notification, false, false, i);
  TEST_ASSERT_EQUAL(16, (int)q.size());
  TEST_ASSERT_EQUAL_STRING("ag19", q.recent(0)->agent.c_str());
  TEST_ASSERT_EQUAL_STRING("ag4", q.recent(15)->agent.c_str());  // les plus anciens sont perdus
  auto l = q.latest(3);
  TEST_ASSERT_EQUAL(3, (int)l.size());
  TEST_ASSERT_EQUAL_STRING("ag18", l[1].agent.c_str());
  TEST_ASSERT_TRUE(q.ackLatest());
  TEST_ASSERT_EQUAL_STRING("ag18", q.recent(0)->agent.c_str());
  TEST_ASSERT_TRUE(q.ackRecent(1));
  TEST_ASSERT_EQUAL_STRING("ag16", q.recent(1)->agent.c_str());
  TEST_ASSERT_FALSE(q.ackRecent(99));
  TEST_ASSERT_EQUAL(1, (int)q.ackAgent("ag5"));
  q.ackAll();
  TEST_ASSERT_TRUE(q.empty());
  TEST_ASSERT_FALSE(q.ackLatest());
  TEST_ASSERT_EQUAL(0, q.total());
}

void test_notif_restore_roundtrip() {
  NotifQueue q;
  q.push("x", NudgeType::Error, false, true, 7);
  q.push("y", NudgeType::Start, true, false, 8);
  NotifQueue r;
  r.restore(q.items());
  TEST_ASSERT_EQUAL(2, (int)r.size());
  TEST_ASSERT_EQUAL_STRING("y", r.recent(0)->agent.c_str());
  TEST_ASSERT_TRUE(r.recent(1)->high);
  std::vector<Notif> big(40);
  r.restore(big);
  TEST_ASSERT_EQUAL(16, (int)r.size());
}

// ---------- notifications systeme (connexion) ----------
void test_sys_replace_and_merge() {
  NotifQueue q;
  q.push("a", NudgeType::Done, false, false, 1);
  q.setSystem(Sys::Wifi, SysState::Connecting, 100);
  TEST_ASSERT_TRUE(q.hasBusy());
  TEST_ASSERT_EQUAL(1, q.total());  // la connexion en cours n'est pas comptee
  q.setSystem(Sys::Wifi, SysState::Failed, 200);  // remplace (pas empile)
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_FALSE(q.hasBusy());
  TEST_ASSERT_TRUE(q.recent(0)->type == NudgeType::Error);
  TEST_ASSERT_EQUAL(2, q.total());
  q.setSystem(Sys::Wifi, SysState::Failed, 300);  // meme etat : fusion avec compteur
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_EQUAL(2, q.recent(0)->count);
  q.setSystem(Sys::Server, SysState::Failed, 310);
  TEST_ASSERT_EQUAL(3, (int)q.size());
  q.setSystem(Sys::Wifi, SysState::Recovered, 400);  // etat change : compteur remis a 1, redevient la plus recente
  TEST_ASSERT_EQUAL(3, (int)q.size());
  TEST_ASSERT_TRUE(q.recent(0)->sys == Sys::Wifi);
  TEST_ASSERT_EQUAL(1, q.recent(0)->count);
  TEST_ASSERT_TRUE(q.recent(0)->type == NudgeType::Done);
  TEST_ASSERT_EQUAL_STRING("Wi-Fi", sysName(Sys::Wifi));
  TEST_ASSERT_EQUAL_STRING("Serveur", sysName(Sys::Server));
}

void test_sys_isolated_from_agents() {
  NotifQueue q;
  q.setSystem(Sys::Server, SysState::Failed, 0);
  q.push("", NudgeType::Error, false, false, 1);  // un agent sans slug ne fusionne pas avec une entree systeme
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_EQUAL(0, (int)q.ackAgent("Serveur"));
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_TRUE(q.clearSystem(Sys::Server));
  TEST_ASSERT_FALSE(q.clearSystem(Sys::Server));
  TEST_ASSERT_EQUAL(1, (int)q.size());
  // acquittement par le bouton du dessus : retire l'entree systeme la plus recente
  q.setSystem(Sys::Wifi, SysState::Failed, 0);
  TEST_ASSERT_TRUE(q.ackLatest());
  TEST_ASSERT_EQUAL(1, (int)q.size());
}

void test_sys_expire() {
  NotifQueue q;
  q.push("a", NudgeType::Done, false, false, 1);
  q.setSystem(Sys::Wifi, SysState::Failed, 1000);
  q.setSystem(Sys::Server, SysState::Recovered, 1000);
  TEST_ASSERT_EQUAL(0, (int)q.expire(1000 + kRecoveredMs - 1));
  TEST_ASSERT_EQUAL(3, (int)q.size());
  TEST_ASSERT_EQUAL(1, (int)q.expire(1000 + kRecoveredMs));  // seul "OK" expire ; l'erreur et l'agent restent
  TEST_ASSERT_EQUAL(2, (int)q.size());
  TEST_ASSERT_TRUE(q.recent(0)->sys == Sys::Wifi);
  // robuste au retour a zero de millis()
  NotifQueue w;
  w.setSystem(Sys::Wifi, SysState::Recovered, 0xFFFFFFFFu - 1000);
  TEST_ASSERT_EQUAL(0, (int)w.expire(0xFFFFFFFFu - 500));
  TEST_ASSERT_EQUAL(1, (int)w.expire(5000));
}

void test_conn_report() {
  uint8_t f = 0;
  ConnOutcome o = connReport(f, true, false);
  TEST_ASSERT_FALSE(o.show);  // pas d'entree OK sans echec precedent
  o = connReport(f, false, false);  // 1er echec, ecran eteint : silencieux
  TEST_ASSERT_TRUE(o.show);
  TEST_ASSERT_TRUE(o.state == SysState::Failed);
  TEST_ASSERT_FALSE(o.sound);
  TEST_ASSERT_EQUAL(1, f);
  o = connReport(f, false, false);  // 2e echec consecutif : son meme ecran eteint
  TEST_ASSERT_TRUE(o.sound);
  o = connReport(f, false, false);  // 3e : plus de sonnerie a chaque reveil
  TEST_ASSERT_FALSE(o.sound);
  o = connReport(f, true, false);
  TEST_ASSERT_TRUE(o.show);
  TEST_ASSERT_TRUE(o.state == SysState::Recovered);
  TEST_ASSERT_FALSE(o.sound);
  TEST_ASSERT_EQUAL(0, f);
  o = connReport(f, false, true);  // ecran allume : son des le 1er echec
  TEST_ASSERT_TRUE(o.sound);
  f = 250;
  connReport(f, false, false);
  TEST_ASSERT_EQUAL(250, f);
}

void test_sys_not_persisted_semantics() {
  // restore() garde ce qu'on lui donne : c'est rtcStoreNotifs (firmware) qui exclut les entrees systeme.
  NotifQueue q;
  q.setSystem(Sys::Wifi, SysState::Failed, 0);
  int persisted = 0;
  for (auto &n : q.items()) if (n.sys == Sys::None) persisted++;
  TEST_ASSERT_EQUAL(0, persisted);
}

void test_pick_alert() {
  std::vector<EventItem> ev;
  NudgeType t; bool h;
  TEST_ASSERT_FALSE(pickAlert(ev, t, h));
  EventItem a; a.kind = "nudge"; a.type = NudgeType::Done;
  EventItem b; b.kind = "nudge"; b.type = NudgeType::Error;
  EventItem c; c.kind = "voice";
  EventItem d; d.kind = "nudge"; d.type = NudgeType::Question; d.priority = "high";
  ev = {a, c};
  TEST_ASSERT_TRUE(pickAlert(ev, t, h));
  TEST_ASSERT_TRUE(t == NudgeType::Done); TEST_ASSERT_FALSE(h);
  ev = {a, b};
  pickAlert(ev, t, h);
  TEST_ASSERT_TRUE(t == NudgeType::Error);
  ev = {b, a, d};
  pickAlert(ev, t, h);
  TEST_ASSERT_TRUE(t == NudgeType::Question); TEST_ASSERT_TRUE(h);
  ev = {c};
  TEST_ASSERT_FALSE(pickAlert(ev, t, h));
}

// ---------- reglages ----------
void test_settings_cycles() {
  // le volume par defaut est atteignable et le cycle boucle
  int v = 0, seen230 = 0;
  for (int i = 0; i < 7; i++) { v = nextVolume(v); if (v == 230) seen230++; }
  TEST_ASSERT_EQUAL(0, v);
  TEST_ASSERT_EQUAL(1, seen230);
  TEST_ASSERT_EQUAL(255, nextVolume(230));
  TEST_ASSERT_EQUAL(0, nextVolume(255));
  TEST_ASSERT_EQUAL(192, nextVolume(150));  // valeur hors serie : plus proche (160), puis suivante
  TEST_ASSERT_EQUAL(210, nextBrightness(160));
  TEST_ASSERT_EQUAL(20, nextBrightness(255));
  TEST_ASSERT_EQUAL(30, nextPause(10));
  TEST_ASSERT_EQUAL(0, nextPause(300));
  TEST_ASSERT_EQUAL(10, nextPause(0));
  TEST_ASSERT_EQUAL(30, nextPause(12345));
  TEST_ASSERT_EQUAL(30, sanitizePause(7));
  TEST_ASSERT_EQUAL(0, sanitizePause(0));
  TEST_ASSERT_EQUAL(255, sanitizeVolume(999));
  TEST_ASSERT_EQUAL(0, sanitizeVolume(-4));
  TEST_ASSERT_EQUAL(5, sanitizeBrightness(0));
}

void test_formats() {
  TEST_ASSERT_EQUAL_STRING("Muet", formatVolume(0).c_str());
  TEST_ASSERT_EQUAL_STRING("100%", formatVolume(255).c_str());
  TEST_ASSERT_EQUAL_STRING("90%", formatVolume(230).c_str());
  TEST_ASSERT_EQUAL_STRING("43%", formatBrightness(110).c_str());
  TEST_ASSERT_EQUAL_STRING("10 s", formatPause(10).c_str());
  TEST_ASSERT_EQUAL_STRING("1 min", formatPause(60).c_str());
  TEST_ASSERT_EQUAL_STRING("5 min", formatPause(300).c_str());
  TEST_ASSERT_EQUAL_STRING("Jamais", formatPause(0).c_str());
  TEST_ASSERT_EQUAL_STRING("0", formatCount(0).c_str());
  TEST_ASSERT_EQUAL_STRING("99", formatCount(99).c_str());
  TEST_ASSERT_EQUAL_STRING("99+", formatCount(100).c_str());
  TEST_ASSERT_EQUAL_STRING("0", formatCount(-3).c_str());
  TEST_ASSERT_EQUAL_STRING("87%", formatBattery(87).c_str());
  TEST_ASSERT_EQUAL_STRING("--", formatBattery(-1).c_str());
}

void test_activity_and_agent_index() {
  AgentState a, b, c;
  a.status = "working"; b.status = "working"; b.stale = true; c.status = "idle";
  Activity act = summarizeActivity({a, b, c});
  TEST_ASSERT_EQUAL(1, act.working);
  TEST_ASSERT_EQUAL(3, act.followed);
  TEST_ASSERT_EQUAL(0, summarizeActivity({}).followed);
  TEST_ASSERT_EQUAL(1, (int)nextAgentIndex(0, 3));
  TEST_ASSERT_EQUAL(0, (int)nextAgentIndex(2, 3));
  TEST_ASSERT_EQUAL(1, (int)nextAgentIndex(9, 3));  // index invalide -> 0 puis suivant
  TEST_ASSERT_EQUAL(0, (int)nextAgentIndex(0, 0));
  TEST_ASSERT_EQUAL(0, (int)clampAgentIndex(5, 2));
}

void test_agent_label() {
  Config cfg;
  cfg.agents = {{"claude-code", "Claude Code", true, 0}, {"long", "Un nom d'agent vraiment tres long", true, 1}};
  AgentState a; a.agent = "claude-code";
  TEST_ASSERT_EQUAL_STRING("Claude Code", agentLabel(cfg, a, 18).c_str());
  AgentState l; l.agent = "long";
  TEST_ASSERT_EQUAL_STRING("Un nom d'agent ...", agentLabel(cfg, l, 18).c_str());
  TEST_ASSERT_EQUAL(18, (int)utf8Length(agentLabel(cfg, l, 18)));
  AgentState u; u.agent = "inconnu";
  TEST_ASSERT_EQUAL_STRING("inconnu", agentLabel(cfg, u, 18).c_str());  // hors config : slug
}

void test_battery_style() {
  BatteryStyle s = batteryStyle(80, Charge::Discharging);
  TEST_ASSERT_EQUAL_HEX16(0x07E0, s.color); TEST_ASSERT_FALSE(s.bolt); TEST_ASSERT_FALSE(s.low);
  s = batteryStyle(30, Charge::Unknown);
  TEST_ASSERT_EQUAL_HEX16(0xFD20, s.color); TEST_ASSERT_FALSE(s.bolt);
  s = batteryStyle(10, Charge::Discharging);
  TEST_ASSERT_EQUAL_HEX16(0xF800, s.color); TEST_ASSERT_TRUE(s.low);
  s = batteryStyle(10, Charge::Charging);  // en charge : pas critique, eclair
  TEST_ASSERT_TRUE(s.bolt); TEST_ASSERT_EQUAL_HEX16(0x07E0, s.color); TEST_ASSERT_FALSE(s.low);
  s = batteryStyle(-1, Charge::Charging);
  TEST_ASSERT_EQUAL(-1, s.level); TEST_ASSERT_TRUE(s.bolt);
  s = batteryStyle(250, Charge::Discharging);
  TEST_ASSERT_EQUAL(-1, s.level);
}

void test_main_actions() {
  TEST_ASSERT_TRUE(mainAction(Button::Top, Press::Click, true) == MainAction::AckLatest);
  TEST_ASSERT_TRUE(mainAction(Button::Top, Press::Click, false) == MainAction::NextAgent);
  TEST_ASSERT_TRUE(mainAction(Button::Top, Press::Hold, true) == MainAction::NextAgent);
  TEST_ASSERT_TRUE(mainAction(Button::Top, Press::DoubleClick, true) == MainAction::None);
  TEST_ASSERT_TRUE(mainAction(Button::Front, Press::Click, true) == MainAction::Replay);
  TEST_ASSERT_TRUE(mainAction(Button::Front, Press::Hold, false) == MainAction::Record);
  TEST_ASSERT_TRUE(mainAction(Button::Front, Press::DoubleClick, false) == MainAction::OpenMenu);
}

void test_menu_root() {
  MenuState m;
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::VolumeNext);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::FrontClick) == MenuAct::Redraw);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::BrightnessNext);
  menuStep(m, MenuEvent::FrontClick);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::PauseNext);
  menuStep(m, MenuEvent::FrontClick);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::WifiConfig);
  menuStep(m, MenuEvent::FrontClick);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::AskReset);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopHold) == MenuAct::None);  // jamais de reset direct
  menuStep(m, MenuEvent::FrontClick);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopClick) == MenuAct::Close);
  menuStep(m, MenuEvent::FrontClick);  // boucle
  TEST_ASSERT_EQUAL(0, (int)m.cursor);
  TEST_ASSERT_EQUAL(6, (int)kMenuRootRows);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::FrontHold) == MenuAct::Close);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::Idle) == MenuAct::Close);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::TopHold) == MenuAct::None);
}

static AgentState mkAgent(const char *slug, const char *status, bool stale = false) {
  AgentState a; a.agent = slug; a.name = slug; a.status = status; a.stale = stale;
  return a;
}

void test_scene_styles() {
  std::vector<AgentState> st = {mkAgent("a", "working"), mkAgent("b", "idle"), mkAgent("c", "done"),
                                mkAgent("d", "waiting"), mkAgent("e", "working", true)};
  Scene s = buildScene(st, 2);
  TEST_ASSERT_EQUAL(5, (int)s.agents.size());
  TEST_ASSERT_EQUAL(0, s.more);
  TEST_ASSERT_FALSE(s.calm);
  TEST_ASSERT_TRUE(s.agents[0].animated && s.agents[0].icon == Icon::Working && s.agents[0].color == 0x07FF);
  TEST_ASSERT_TRUE(!s.agents[1].animated && s.agents[1].icon == Icon::Idle);
  TEST_ASSERT_TRUE(!s.agents[2].animated && s.agents[2].icon == Icon::Done);
  TEST_ASSERT_TRUE(!s.agents[3].animated && s.agents[3].icon == Icon::Waiting);
  TEST_ASSERT_TRUE(!s.agents[4].animated && s.agents[4].icon == Icon::Stale);  // perime : pas anime
  TEST_ASSERT_TRUE(s.agents[2].selected && !s.agents[0].selected);
}

void test_scene_calm_and_empty() {
  Scene e = buildScene({}, 0);
  TEST_ASSERT_TRUE(e.agents.empty() && e.calm && e.more == 0);
  Scene c = buildScene({mkAgent("a", "idle"), mkAgent("b", "done")}, 0);
  TEST_ASSERT_TRUE(c.calm);
}

void test_scene_grouping() {
  std::vector<AgentState> st;
  for (int i = 0; i < 9; i++) st.push_back(mkAgent(("a" + std::to_string(i)).c_str(), "idle"));
  Scene s = buildScene(st, 0);
  TEST_ASSERT_EQUAL(5, (int)s.agents.size());  // 5 emplacements + "+4"
  TEST_ASSERT_EQUAL(4, s.more);
  TEST_ASSERT_TRUE(s.agents[0].selected);
  s = buildScene(st, 8);  // selection hors fenetre : la fenetre glisse
  TEST_ASSERT_EQUAL(5, (int)s.agents.size());
  TEST_ASSERT_EQUAL(4, s.more);
  TEST_ASSERT_TRUE(s.agents[4].selected);
  std::vector<AgentState> six(st.begin(), st.begin() + 6);
  s = buildScene(six, 0);
  TEST_ASSERT_EQUAL(6, (int)s.agents.size());  // 6 tiennent : pas de "+N"
  TEST_ASSERT_EQUAL(0, s.more);
  // un agent qui travaille cache derriere "+N" empeche quand meme la scene d'etre calme
  st[8].status = "working";
  s = buildScene(st, 0);
  TEST_ASSERT_FALSE(s.calm);
}

void test_scene_breath_range() {
  int mn = 9, mx = -9;
  for (uint32_t f = 0; f < 200; f++) { int b = sceneBreath(f); if (b < mn) mn = b; if (b > mx) mx = b; }
  TEST_ASSERT_EQUAL(-2, mn);
  TEST_ASSERT_EQUAL(2, mx);
  TEST_ASSERT_EQUAL(sceneBreath(5), sceneBreath(53));
}

void test_notif_layout_budget() {
  TEST_ASSERT_EQUAL(0, notifLayout(0).shown);
  for (int n = 1; n <= 6; n++) {
    NotifLayout l = notifLayout(n);
    TEST_ASSERT_EQUAL(n > 3 ? 3 : n, l.shown);
    TEST_ASSERT_TRUE(l.lineH >= 25 && l.lineH <= 40);
    TEST_ASSERT_EQUAL(l.shown * l.lineH, l.summaryY);
    // lignes + resume tiennent au-dessus du separateur (kStatusY - 1) : ni debordement ni chevauchement
    TEST_ASSERT_TRUE(l.summaryY + kSummaryH <= kStatusY - 1);
  }
  TEST_ASSERT_EQUAL(28, notifLayout(3).lineH);
  TEST_ASSERT_EQUAL(40, notifLayout(1).lineH);
  TEST_ASSERT_EQUAL(kScreenH, kStatusY + kStatusH);
}

void test_icon_row_layout() {
  TEST_ASSERT_EQUAL(24, iconRowLayout(1).r);
  TEST_ASSERT_EQUAL(24, iconRowLayout(4).r);
  int prev = 99;
  for (int slots = 1; slots <= 6; slots++) {
    IconRow g = iconRowLayout(slots);
    TEST_ASSERT_TRUE(g.r <= prev);  // plus d'agents : icones plus petites
    prev = g.r;
    TEST_ASSERT_TRUE(2 * g.r + 6 <= g.slotW);  // pas de chevauchement horizontal
    TEST_ASSERT_TRUE(g.cy - g.r - 2 >= 0);     // rebond en haut
    TEST_ASSERT_TRUE(g.nameCy - kNameH / 2 > g.cy + g.r + 6);  // nom sous le soulignement
    TEST_ASSERT_TRUE(g.nameCy + kNameH / 2 <= kStatusY - 1);   // nom au-dessus du separateur
  }
  TEST_ASSERT_EQUAL(21, iconRowLayout(5).r);
  TEST_ASSERT_EQUAL(17, iconRowLayout(6).r);
  TEST_ASSERT_TRUE(iconRowLayout(0).r == 24);  // robuste
}

void test_summary() {
  Summary a = buildSummary(3, 0, 0);
  TEST_ASSERT_EQUAL_STRING("3", a.pending.c_str());
  TEST_ASSERT_FALSE(a.showWorking);  // aucun agent suivi : pas d'engrenage
  Summary c = buildSummary(3, 0, 5);
  TEST_ASSERT_TRUE(c.showWorking);
  TEST_ASSERT_FALSE(c.anyWorking);
  TEST_ASSERT_EQUAL_STRING("0/5", c.working.c_str());
  Summary b = buildSummary(120, 2, 5);
  TEST_ASSERT_EQUAL_STRING("99+", b.pending.c_str());
  TEST_ASSERT_TRUE(b.showWorking);
  TEST_ASSERT_TRUE(b.anyWorking);
  TEST_ASSERT_EQUAL_STRING("2/5", b.working.c_str());
}

void test_menu_scroll_and_reset_hold() {
  TEST_ASSERT_EQUAL(0, (int)menuFirstRow(0, 7, 5));
  TEST_ASSERT_EQUAL(0, (int)menuFirstRow(4, 7, 5));
  TEST_ASSERT_EQUAL(1, (int)menuFirstRow(5, 7, 5));
  TEST_ASSERT_EQUAL(2, (int)menuFirstRow(6, 7, 5));
  TEST_ASSERT_EQUAL(0, (int)menuFirstRow(3, 4, 5));
  TEST_ASSERT_EQUAL(0, resetProgress(0));
  TEST_ASSERT_EQUAL(50, resetProgress(1500));
  TEST_ASSERT_EQUAL(100, resetProgress(5000));
  TEST_ASSERT_EQUAL(3, resetSecondsLeft(0));
  TEST_ASSERT_EQUAL(2, resetSecondsLeft(1000));
  TEST_ASSERT_EQUAL(1, resetSecondsLeft(2999));
  TEST_ASSERT_EQUAL(0, resetSecondsLeft(3000));
  TEST_ASSERT_FALSE(resetConfirmed(2999));
  TEST_ASSERT_TRUE(resetConfirmed(3000));
  // l'entree Reinitialiser n'ouvre jamais la confirmation par un simple defilement
  MenuState m;
  for (int i = 0; i < 4; i++) menuStep(m, MenuEvent::FrontClick);
  TEST_ASSERT_TRUE(m.item() == MenuItem::Reset);
  TEST_ASSERT_TRUE(menuStep(m, MenuEvent::FrontClick) == MenuAct::Redraw);
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
  RUN_TEST(test_parse_api_error);
  RUN_TEST(test_bodies);
  RUN_TEST(test_phase_machine);
  RUN_TEST(test_ack);
  RUN_TEST(test_screen_events);
  RUN_TEST(test_screen_ignores_filters);
  RUN_TEST(test_screen_state_change);
  RUN_TEST(test_order_states);
  RUN_TEST(test_all_config_agents_displayed);
  RUN_TEST(test_truncate);
  RUN_TEST(test_sleep);
  RUN_TEST(test_wav_pcm_header_roundtrip);
  RUN_TEST(test_wav_streaming_size_and_list_chunk);
  RUN_TEST(test_wav_invalid);
  RUN_TEST(test_ima_roundtrip);
  RUN_TEST(test_parse_real_server_sync);
  RUN_TEST(test_caps_json_exact);
  RUN_TEST(test_server_pcm_fixtures);
  RUN_TEST(test_server_ima_adpcm_fixtures);
  RUN_TEST(test_server_ima_without_fact_pads_last_block);
  RUN_TEST(test_status_icon);
  RUN_TEST(test_nudge_type_parse);
  RUN_TEST(test_nudge_type_in_sync);
  RUN_TEST(test_sound_durations_and_notes);
  RUN_TEST(test_sound_peak_normalised);
  RUN_TEST(test_sound_no_click_and_block_independent);
  RUN_TEST(test_sound_error_is_low_and_distinct);
  RUN_TEST(test_sound_priority_order);
  RUN_TEST(test_sound_scheduler_rules);
  RUN_TEST(test_notif_push_merge_order);
  RUN_TEST(test_notif_ack_and_capacity);
  RUN_TEST(test_notif_restore_roundtrip);
  RUN_TEST(test_sys_replace_and_merge);
  RUN_TEST(test_sys_isolated_from_agents);
  RUN_TEST(test_sys_expire);
  RUN_TEST(test_conn_report);
  RUN_TEST(test_sys_not_persisted_semantics);
  RUN_TEST(test_pick_alert);
  RUN_TEST(test_settings_cycles);
  RUN_TEST(test_formats);
  RUN_TEST(test_activity_and_agent_index);
  RUN_TEST(test_agent_label);
  RUN_TEST(test_battery_style);
  RUN_TEST(test_main_actions);
  RUN_TEST(test_menu_root);
  RUN_TEST(test_scene_styles);
  RUN_TEST(test_scene_calm_and_empty);
  RUN_TEST(test_scene_grouping);
  RUN_TEST(test_scene_breath_range);
  RUN_TEST(test_notif_layout_budget);
  RUN_TEST(test_icon_row_layout);
  RUN_TEST(test_summary);
  RUN_TEST(test_menu_scroll_and_reset_hold);
  return UNITY_END();
}
