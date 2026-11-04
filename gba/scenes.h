#pragma once
#include <stdint.h>
#include "generated.h"
#include "scene_1_ns.h"
#include "scene_2_ns.h"
#include "scene_3_ns.h"

struct GbaSceneData {
  int map_w;
  int map_h;
  int bg1_base;
  int bg2_base;
  int bg3_base;
  int bg_size;
  int par1x, par1y, par2x, par2y, par3x, par3y;
  const uint16_t* palette;
  const uint8_t* tiles;
  int tile_count;
  const uint16_t* map1;
  const uint16_t* map2;
  const uint16_t* map3;
  const uint8_t* collision;
  int spawn_x;
  int spawn_y;
  int font_base;
  int font_count;
  int hp;
  int hp_max;
  int obj_w;
  int obj_h;
  int obj_tile_count;
  int obj_frame_count;
  const uint16_t* obj_palette;
  const uint8_t* obj_tiles;
  const int* anim_start;
  const int* anim_len;
  int enemy_count;
  const int* enemy_x;
  const int* enemy_y;
  int enemy_w;
  int enemy_h;
  int enemy_tile_base;
  int enemy_tile_count;
  int enemy_frame_count;
  const uint16_t* enemy_palette;
  const uint8_t* enemy_tiles;
  int trigger_count;
  const int* trig_x, *trig_y, *trig_w, *trig_h, *trig_once, *trig_type, *trig_value, *trig_target;
};

#define SCENE_FIELDS(p) \
  p::kGbaMapW, p::kGbaMapH, p::kGbaBg1Base, p::kGbaBg2Base, p::kGbaBg3Base, \
  p::kGbaBgSizeBits, p::kGbaParallaxBg1X, p::kGbaParallaxBg1Y, \
  p::kGbaParallaxBg2X, p::kGbaParallaxBg2Y, p::kGbaParallaxBg3X, p::kGbaParallaxBg3Y, \
  p::kGbaPaletteBanks, p::kGbaTiles, p::kGbaTileCount, \
  p::kGbaMapBg1, p::kGbaMapBg2, p::kGbaMapBg3, p::kGbaCollision, \
  p::kGbaSpawnX, p::kGbaSpawnY, p::kGbaFontTileBase, p::kGbaFontCount, \
  p::kGbaPlayerHp, p::kGbaPlayerHpMax, p::kGbaObjW, p::kGbaObjH, \
  p::kGbaObjTileCount, p::kGbaObjFrameCount, p::kGbaObjPalette, p::kGbaObjTiles, \
  p::kGbaObjAnimStart, p::kGbaObjAnimLen, \
  p::kGbaEnemyCount, p::kGbaEnemyX, p::kGbaEnemyY, p::kGbaEnemyW, p::kGbaEnemyH, \
  p::kGbaEnemyTileBase, p::kGbaEnemyTileCount, p::kGbaEnemyFrameCount, \
  p::kGbaEnemyPalette, p::kGbaEnemyTiles, \
  p::kGbaTriggerCount, p::kGbaTriggerX, p::kGbaTriggerY, p::kGbaTriggerW, \
  p::kGbaTriggerH, p::kGbaTriggerOnce, p::kGbaTriggerType, p::kGbaTriggerValue, \
  p::kGbaTriggerTarget

static const GbaSceneData kGbaScenes[4] = {
    {SCENE_FIELDS()},
    {SCENE_FIELDS(gba_s1)},
    {SCENE_FIELDS(gba_s2)},
    {SCENE_FIELDS(gba_s2)},
};