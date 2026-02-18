#pragma once

#include "game_shared.h"

namespace game {

bool actionDown(const char *action);
void clearAllActions();
void setActionByKey(const String &keyCode, bool down);
bool anyActionActive();

int inventoryTotal();
void initHotbarDefaults();
bool inventoryTakeFromSelected(uint8_t *outBlockId);
bool inventoryAddBlock(uint8_t blockId);
unsigned long breakCooldownMsFor(uint8_t blockId);
void inventorySelectPrev();
void inventorySelectNext();

void updateCamera(float dtSec);
void checkInputTimeout();

}  // namespace game
