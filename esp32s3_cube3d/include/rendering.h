#pragma once

#include "game_shared.h"

namespace game {

void updateCameraBasis();
bool projectToScreen(const Vec3 &w, ProjVert &out);
void buildVisibleFaces();
void drawWorld();
void drawHud();
void drawHomeScreen();
void drawCrosshair();
void drawAimHighlight();

}  // namespace game
