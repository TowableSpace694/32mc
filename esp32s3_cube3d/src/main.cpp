#include <Arduino.h>

#include "controls.h"
#include "game_shared.h"
#include "mc_client.h"
#include "rendering.h"
#include "web_control.h"
#include "world.h"

#include <algorithm>
#include <cmath>

using namespace game;

namespace {

bool s_prevGameplayReady = false;

void resetActionLatch() {
  clearAllActions();
  s_prevJumpDown = false;
  s_prevBreakDown = false;
  s_prevPlaceDown = false;
  s_prevInvPrevDown = false;
  s_prevInvNextDown = false;
  for (int i = 0; i < kInvSlots; ++i) {
    s_prevSlotDown[i] = false;
  }
  s_velY = 0.0f;
}

void tickFpsAndLog(unsigned long now) {
  s_frameCounter++;
  if (now - s_lastFpsMs >= 1000) {
    s_fps = s_frameCounter;
    s_frameCounter = 0;
    s_lastFpsMs = now;
    Serial.printf("[stat] fps=%u pos=(%.2f,%.2f,%.2f)\n", s_fps, s_camX, s_camY, s_camZ);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(80);

  if (kTftBlk >= 0) {
    pinMode(kTftBlk, OUTPUT);
    digitalWrite(kTftBlk, HIGH);
  }
  pinMode(kBtnLeft, INPUT_PULLUP);
  pinMode(kBtnRight, INPUT_PULLUP);

  SPI.begin(kTftSclk, -1, kTftMosi, kTftCs);
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(24000000);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);

  clearWorld();
  s_gameStarted = true;
  initHotbarDefaults();
  s_selectedSlot = 0;
  wifiConnectNow();
  setupWeb();

  s_lastInputMs = millis();
  s_lastFpsMs = millis();
  s_lastFrameMs = millis();
}

void loop() {
  updateWifi();
  mcUpdate();
  server.handleClient();
  checkInputTimeout();

  const unsigned long now = millis();
  const float dt = static_cast<float>(now - s_lastFrameMs) * 0.001f;
  s_lastFrameMs = now;

  if (!mcReadyForGameplay()) {
    if (s_prevGameplayReady) {
      s_prevGameplayReady = false;
      resetActionLatch();
    }
    drawHomeScreen();
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), kScreenW, kScreenH);
    tickFpsAndLog(now);
    return;
  }

  if (!s_prevGameplayReady) {
    s_prevGameplayReady = true;
    resetActionLatch();
  }

  updateCamera(dt);
  drawWorld();
  drawAimHighlight();
  drawHud();
  drawCrosshair();
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), kScreenW, kScreenH);

  tickFpsAndLog(now);
}
