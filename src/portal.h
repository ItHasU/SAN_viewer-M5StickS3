// Portail de configuration Wi-Fi (AP temporaire) + champs SAN.
#pragma once
#include "hw_state.h"

// Bloquant (timeout kPortalTimeoutS). Renvoie true si la config est sauvegardee dans `s`.
bool runConfigPortal(Settings &s);
