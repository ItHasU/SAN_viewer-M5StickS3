// Portail de configuration Wi-Fi (AP temporaire) + champs SAN.
#pragma once
#include "hw_state.h"

enum class PortalResult { Saved, Cancelled, Timeout };

// Point d'acces + portail WiFiManager, non bloquant pour WiFiManager mais bloquant pour l'appelant (timeout
// kPortalTimeoutS). Affiche l'icone Wi-Fi + le nom de l'AP. `cancelable` : un clic sur un bouton abandonne.
// Saved : la config est enregistree dans `s` et en NVS. Cancelled / Timeout : rien n'est modifie.
PortalResult runConfigPortal(Settings &s, bool cancelable);
