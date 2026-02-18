#pragma once

#include "game_shared.h"

namespace game {

void clearWorld();
void buildWorld();
bool isSolidVoxel(int x, int y, int z);
void setVoxel(int x, int y, int z, uint8_t blockId);
int supportYBelowPlayer(int x, int z, float camY);
bool isPlayerCollidingAt(float camX, float camY, float camZ);
bool inWorldXYZ(int x, int y, int z);
RayHit raycastCenter();

}  // namespace game
