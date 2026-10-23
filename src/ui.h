// Ecrans (portrait 135x240). Toute la decision d'affichage est dans san_core ; ici, le dessin seulement.
#pragma once
#include <Arduino.h>

#include <string>
#include <vector>

#include "san_types.h"

namespace ui {
void begin();
void backlightOn();
void backlightOff();
void message(const char *title, const std::string &text, uint16_t color = 0xFFFF);
void pairing(const std::string &code, const std::string &url);
// sel : agent selectionne ; battery : -1 inconnu.
void status(const std::vector<san::AgentState> &states, const san::Config &cfg, size_t sel, int battery);
void alert(const std::string &agentName, const std::string &kind, const std::string &text, bool high);
void recording(const std::string &agentName, int ms, int maxMs);
// Menu de config : curseur, mode ; items = cfg.agents.
void menu(const san::Config &cfg, size_t cursor, const std::string &note);
}  // namespace ui
