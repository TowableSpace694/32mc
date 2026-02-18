#include "game_shared.h"

namespace game {

Adafruit_ST7735 tft(kTftCs, kTftDc, kTftRst);
GFXcanvas16 canvas(kScreenW, kScreenH);
WebServer server(80);

uint8_t s_voxel[kWorldW][kWorldHMax][kWorldD];
FaceQuad s_faces[kMaxFaces];
int s_faceCount = 0;

float s_camX = (kWorldW - 1) * 0.5f;
float s_camY = 4.2f;
float s_camZ = (kWorldD - 1) * 0.5f;
float s_yaw = 0.0f;
float s_pitch = 0.0f;
float s_velY = 0.0f;
float s_camCy = 1.0f;
float s_camSy = 0.0f;
float s_camCp = 1.0f;
float s_camSp = 0.0f;

unsigned long s_lastFpsMs = 0;
unsigned long s_lastFrameMs = 0;
uint32_t s_frameCounter = 0;
uint16_t s_fps = 0;

unsigned long s_lastWifiAttemptMs = 0;
unsigned long s_lastInputMs = 0;
unsigned long s_lastEditMs = 0;
unsigned long s_lastMcAttemptMs = 0;
bool s_prevJumpDown = false;
bool s_prevBreakDown = false;
bool s_prevPlaceDown = false;
bool s_prevInvPrevDown = false;
bool s_prevInvNextDown = false;
bool s_prevSlotDown[kInvSlots] = {};
bool s_gameStarted = false;
HotbarSlot s_hotbar[kInvSlots] = {};
int s_selectedSlot = 0;

KeyBinding s_bindings[kBindingCount] = {
    {"turn_left", "ArrowLeft", false},
    {"turn_right", "ArrowRight", false},
    {"look_up", "ArrowUp", false},
    {"look_down", "ArrowDown", false},
    {"move_fwd", "KeyW", false},
    {"move_back", "KeyS", false},
    {"strafe_left", "KeyA", false},
    {"strafe_right", "KeyD", false},
    {"jump", "Space", false},
    {"break_block", "KeyQ", false},
    {"place_block", "KeyR", false},
    {"inv_prev", "KeyZ", false},
    {"inv_next", "KeyX", false},
    {"slot_1", "Digit1", false},
    {"slot_2", "Digit2", false},
    {"slot_3", "Digit3", false},
    {"slot_4", "Digit4", false},
    {"slot_5", "Digit5", false},
};

const char *kSlotActions[kInvSlots] = {"slot_1", "slot_2", "slot_3", "slot_4", "slot_5"};

bool s_hasAimHit = false;
RayHit s_aimHit = {false, 0, 0, 0, 0, 0, 0, 0, 0, 0};
RemotePlayerView s_remotePlayers[kRemotePlayerMax] = {};

String s_mcHost = kMcDefaultHost;
uint16_t s_mcPort = kMcDefaultPort;
String s_mcPlayerName = kMcDefaultPlayer;
bool s_mcAutoConnect = true;
String s_mcState = "IDLE";

const char *blockShortName(uint8_t blockId) {
  switch (blockId) {
    case BLOCK_GRASS:
      return "GR";
    case BLOCK_DIRT:
      return "DR";
    case BLOCK_STONE:
      return "ST";
    case BLOCK_WOOD:
      return "WD";
    case BLOCK_SAND:
      return "SA";
    case BLOCK_ORE:
      return "OR";
    case BLOCK_BEDROCK:
      return "BD";
    default:
      return "__";
  }
}

uint16_t blockTopColor(uint8_t blockId) {
  switch (blockId) {
    case BLOCK_GRASS:
      return kGrassTop;
    case BLOCK_DIRT:
      return kDirt;
    case BLOCK_STONE:
      return kStone;
    case BLOCK_WOOD:
      return kWood;
    case BLOCK_SAND:
      return kSand;
    case BLOCK_ORE:
      return kOre;
    case BLOCK_BEDROCK:
      return kBedrock;
    default:
      return ST77XX_BLACK;
  }
}

uint16_t blockSideColor(uint8_t blockId) {
  switch (blockId) {
    case BLOCK_GRASS:
      return kGrassSide;
    case BLOCK_DIRT:
      return kDirt;
    case BLOCK_STONE:
      return kStone;
    case BLOCK_WOOD:
      return rgb565(120, 86, 52);
    case BLOCK_SAND:
      return rgb565(206, 189, 128);
    case BLOCK_ORE:
      return rgb565(158, 148, 118);
    case BLOCK_BEDROCK:
      return rgb565(45, 45, 55);
    default:
      return ST77XX_BLACK;
  }
}

}  // namespace game
