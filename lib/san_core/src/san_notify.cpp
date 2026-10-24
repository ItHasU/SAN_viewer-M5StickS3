#include "san_notify.h"

#include <algorithm>

namespace san {

void NotifQueue::push(const std::string &agent, NudgeType type, bool voice, bool high, int64_t id) {
  for (size_t i = 0; i < items_.size(); i++) {
    Notif &n = items_[i];
    if (n.agent == agent && n.type == type && n.voice == voice) {
      Notif m = n;
      m.count++;
      m.high = m.high || high;
      m.id = id > m.id ? id : m.id;
      items_.erase(items_.begin() + i);
      items_.push_back(m);  // redevient la plus recente
      return;
    }
  }
  if (items_.size() >= kCapacity) items_.erase(items_.begin());
  Notif n;
  n.agent = agent; n.type = type; n.voice = voice; n.high = high; n.id = id;
  items_.push_back(n);
}

int NotifQueue::total() const {
  int t = 0;
  for (auto &n : items_) t += n.count;
  return t;
}

const Notif *NotifQueue::recent(size_t index) const {
  if (index >= items_.size()) return nullptr;
  return &items_[items_.size() - 1 - index];
}

std::vector<Notif> NotifQueue::latest(size_t n) const {
  std::vector<Notif> v;
  for (size_t i = 0; i < n && i < items_.size(); i++) v.push_back(items_[items_.size() - 1 - i]);
  return v;
}

bool NotifQueue::ackLatest() { return ackRecent(0); }

bool NotifQueue::ackRecent(size_t index) {
  if (index >= items_.size()) return false;
  items_.erase(items_.begin() + (items_.size() - 1 - index));
  return true;
}

size_t NotifQueue::ackAgent(const std::string &agent) {
  size_t before = items_.size();
  items_.erase(std::remove_if(items_.begin(), items_.end(), [&](const Notif &n) { return n.agent == agent; }),
               items_.end());
  return before - items_.size();
}

void NotifQueue::restore(const std::vector<Notif> &v) {
  items_.clear();
  size_t start = v.size() > kCapacity ? v.size() - kCapacity : 0;
  for (size_t i = start; i < v.size(); i++) items_.push_back(v[i]);
}

static int importance(NudgeType t) {
  switch (t) {
    case NudgeType::Error: return 5;
    case NudgeType::Question: return 4;
    case NudgeType::Notification: return 3;
    case NudgeType::Done: return 2;
    default: return 1;
  }
}

bool pickAlert(const std::vector<EventItem> &events, NudgeType &type, bool &high) {
  bool found = false;
  int best = -1;
  for (auto &e : events) {
    if (e.kind != "nudge") continue;
    int score = importance(e.type) + (e.isHigh() ? 10 : 0);
    if (score >= best) { best = score; type = e.type; high = e.isHigh(); found = true; }
  }
  return found;
}

}  // namespace san
