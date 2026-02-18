#pragma once

#include "game_shared.h"

namespace game {

void mcSetConfig(const String &host, uint16_t port, const String &playerName, bool autoConnect);
void mcForceReconnect();
void mcUpdate();
bool mcReadyForGameplay();
void mcSetHeldSlot(uint8_t slot);
bool mcTryPlaceBlockServer(const RayHit &hit, uint8_t localBlockId);
bool mcTryBreakBlockServer(const RayHit &hit);

}  // namespace game
