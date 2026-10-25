#include "san_board.h"

#include <algorithm>

namespace san {

Mood moodFromStatus(const std::string &s, bool stale) {
  if (stale) return Mood::Unknown;
  if (s == "working") return Mood::Thinking;
  if (s == "waiting") return Mood::Question;
  if (s == "error") return Mood::Problem;
  if (s == "done" || s == "idle") return Mood::Ready;
  return Mood::Unknown;
}

uint16_t moodColor(Mood m) {
  switch (m) {
    case Mood::Thinking: return 0x07FF;  // cyan
    case Mood::Ready: return 0x07E0;     // vert
    case Mood::Problem: return 0xF800;   // rouge
    case Mood::Question: return 0xFFE0;  // jaune
    default: return 0x7BEF;              // gris
  }
}

int moodRank(Mood m) {
  switch (m) {
    case Mood::Question: return 3;
    case Mood::Problem: return 2;
    case Mood::Ready: return 1;
    default: return 0;
  }
}

uint16_t clipHash(const std::string &id) {
  uint32_t h = 2166136261u;
  for (unsigned char c : id) { h ^= c; h *= 16777619u; }
  return (uint16_t)(h ^ (h >> 16));
}

static bool hasSeen(const std::vector<uint16_t> &seen, uint16_t h) {
  return std::find(seen.begin(), seen.end(), h) != seen.end();
}

void updateSeen(std::vector<uint16_t> &seen, const std::vector<std::string> &clips) {
  for (auto &c : clips) {
    uint16_t h = clipHash(c);
    if (hasSeen(seen, h)) continue;
    seen.push_back(h);
    if (seen.size() > kSeenMax) seen.erase(seen.begin());
  }
}

SyncDiff diffStates(const std::vector<PrevAgent> &prev, const std::vector<AgentState> &states) {
  SyncDiff d;
  auto offer = [&](const std::string &agent, int rank) {
    if (rank > 0 && rank >= d.pickRank) { d.pickRank = rank; d.pick = agent; }  // a rang egal : le dernier
  };
  for (auto &s : states) {
    const PrevAgent *p = nullptr;
    for (auto &q : prev) if (q.agent == s.agent) { p = &q; break; }
    if (!p || p->status.empty()) continue;  // premiere apparition : rien a signaler
    if (!s.stale) {
      Mood now = moodFromStatus(s.status, false), before = moodFromStatus(p->status, false);
      if (now != before && moodRank(now) > 0) { d.changed = true; offer(s.agent, moodRank(now)); }
    }
    VoiceArrival va;
    va.agent = s.agent;
    for (auto &c : s.voiceClips) if (!hasSeen(p->seen, clipHash(c))) va.clips.push_back(c);
    if (!va.clips.empty()) {
      d.changed = true;
      offer(s.agent, kRankVoice);
      d.arrivals.push_back(va);
    }
  }
  return d;
}

AgentRow buildAgentRow(const std::vector<AgentState> &states, size_t sel, size_t maxShown) {
  AgentRow r;
  if (maxShown < 2) maxShown = 2;
  size_t n = states.size(), first = 0, count = n;
  if (sel >= n) sel = 0;
  if (n > maxShown) {
    count = maxShown - 1;
    if (sel >= count) first = sel - count + 1;
    r.more = (int)(n - count);
  }
  for (size_t i = 0; i < n; i++) {
    Mood m = moodFromStatus(states[i].status, states[i].stale);
    if (m == Mood::Thinking) r.thinking = true;
    if (i >= first && i < first + count) r.cells.push_back({m, states[i].voicePending, i == sel});
  }
  return r;
}

std::string formatBadge(int n) {
  if (n < 0) n = 0;
  return n > 9 ? "9+" : std::to_string(n);
}

AgentRowLayout agentRowLayout(int slots, int width) {
  AgentRowLayout g{24, width, 0};
  if (slots < 1) slots = 1;
  g.slotW = width / slots;
  int r = (g.slotW - 6) / 2;
  g.r = r > 24 ? 24 : r < 8 ? 8 : r;
  int block = 2 * g.r + 12;  // rebond (2) + soulignement (6) + marges
  int top = (kAgentsH - block) / 2;
  if (top < 0) top = 0;
  g.cy = top + 4 + g.r;
  return g;
}

int calmBreath(uint32_t frame) {
  int p = (int)(frame % 48), tri = p < 24 ? p : 48 - p;
  return tri * 4 / 24 - 2;
}

bool connReportSound(uint8_t &fails, bool ok, bool screenOn) {
  if (ok) { fails = 0; return false; }
  if (fails < 250) fails++;
  return screenOn || fails == 2;
}

}  // namespace san
