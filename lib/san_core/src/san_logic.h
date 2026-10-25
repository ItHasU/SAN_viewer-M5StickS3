// Logique de decision pure : machine a etats, curseur d'ack, ecran, troncature, veille.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "san_types.h"

namespace san {

// ---- Machine a etats (SPEC 12.1) ----
enum class Phase { Boot, Register, Pairing, Run };
const char *phaseName(Phase p);
// Apres le boot : sans jeton -> Register ; avec jeton -> Run si le viewer etait deja actif, sinon Pairing.
Phase phaseAfterBoot(bool hasToken, bool wasActive);
Phase phaseOnRegistered();
Phase phaseOnStatus(Phase cur, DeviceStatus st);
// 401 dans n'importe quel etat -> Register (le jeton doit etre efface) ; 403 en Run -> Pairing.
Phase phaseOnHttp(Phase cur, int httpCode);
bool httpCodeClearsToken(int httpCode);  // 401

// ---- Curseur d'acquittement (SPEC 9.2) ----
// Le curseur ne fait qu'avancer ; les evenements sont traites dans l'ordre, processedCount = nombre
// d'evenements entierement traites (clip joue/stocke). L'ack ne couvre que le prefixe traite.
int64_t advanceAck(int64_t current, int64_t candidate);
int64_t ackAfterProcessing(int64_t current, const std::vector<EventItem> &events, size_t processedCount);

// Ordre d'affichage : tous les agents de la config (ordre de la config) ayant un etat. Aucun filtre local
// (ni abonnement ni muet) ; config vide : etats tels quels.
std::vector<AgentState> orderStates(const Config &cfg, const std::vector<AgentState> &states);

// ---- Texte ----
// Tronque a maxChars points de code UTF-8 ("..." final inclus dans la limite). maxChars<=0 : inchange.
std::string truncateChars(const std::string &s, int maxChars);
size_t utf8Length(const std::string &s);

// ---- Veille ----
// Duree de deep sleep : next_sync_s serveur borne [10, 3600], defaut 60 ; echecs consecutifs -> backoff.
int computeSleepSeconds(int nextSyncS, int consecutiveFailures);

}  // namespace san
