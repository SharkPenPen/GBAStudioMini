// pgmmv/project.h
//
// R1a：project.json + 资源登记的最小数据层。
//
// 约束：
//   - 这是编辑器/工具侧代码（PC only）；
//   - 运行时不解析 JSON；将来导出阶段才生成 game.p3d；
//   - 文档里未知字段要保留（前向兼容），所以 Project/Asset 都保存原始 JsonValue。
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "pgmmv/json.h"
#include "pgmmv/animation.h"
#include "pgmmv/object.h"
#include "pgmmv/scene.h"
#include "pgmmv/tileset.h"
#include "pgmmv/ui.h"

namespace pgmmv {

constexpr int kProjectFormatVersion = 1;

enum class AssetKind {
  kImage = 0,
  kText,
  kSwitch,
  kVariable,
  kAudio,
  kMesh,      // 3D 预留，R1 不实现
  kMaterial,  // 3D 预留，R1 不实现
  kUnknown,
};

const char* AssetKindName(AssetKind kind);
AssetKind AssetKindFromName(const std::string& name);

struct Asset {
  std::string id;
  std::string name;
  std::string type;  // 原样保存的 type 字符串（未知类型也保留）
  std::string path;  // 相对项目根，使用 '/'，例如 assets/player.png
  std::string source;  // 原始文件绝对路径（“更新”时从它重读；可为空）
  AssetKind kind = AssetKind::kUnknown;
  int width = 0;
  int height = 0;
  int split_x = 1;  // 图像分区默认值（水平格数）
  int split_y = 1;  // 图像分区默认值（垂直格数）
  JsonValue raw;    // 该资源对象的原始 JSON，保留未知字段
};

// 视口固定 480×272；不考虑旧工程。
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;

struct ProjectSettings {
  std::string initial_scene;
};

struct Project {
  std::filesystem::path root_dir;  // project.json 所在目录（不写入 JSON）
  JsonValue raw;                   // 完整 project.json 文档，保留未知字段
  int format_version = kProjectFormatVersion;
  std::string name = "Untitled";
  ProjectSettings settings;
  std::vector<Asset> assets;
  std::vector<ObjectDoc> objects;
  std::vector<AnimationDoc> animations;
  std::vector<TilesetDoc> tilesets;
  std::vector<SceneDoc> scenes;
  std::vector<UiScreenDoc> ui_screens;
  std::string persistent_ui;
  std::string title_ui;
  std::vector<Transition> transitions;
  std::map<std::string, int> variables;  // 全局变量（跨场景、可存档）

  const Asset* FindAsset(const std::string& id) const;
  Asset* FindAsset(const std::string& id);
  const TilesetDoc* FindTileset(const std::string& id) const;
  TilesetDoc* FindTileset(const std::string& id);
  const SceneDoc* FindScene(const std::string& id) const;
  SceneDoc* FindScene(const std::string& id);
  const Asset* FindAssetByPath(const std::string& path) const;
  const ObjectDoc* FindObject(const std::string& id) const;
  ObjectDoc* FindObject(const std::string& id);
};
inline const AnimationDoc* FindAnimation(const Project& p, const std::string& id) {
  for (const AnimationDoc& a : p.animations) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

inline AnimationDoc* FindAnimationMutable(Project& p, const std::string& id) {
  for (AnimationDoc& a : p.animations) {
    if (a.id == id) return &a;
  }
  return nullptr;
}

// 对象的动作 = 它引用的动画资源里的动作；没引用就是空
inline const std::vector<ActionDef>* ObjectActions(const Project& p,
                                                   const ObjectDoc& o) {
  const AnimationDoc* ad = FindAnimation(p, o.animation);
  return ad != nullptr ? &ad->actions : nullptr;
}

inline std::vector<ActionDef>* ObjectActionsMutable(Project& p, ObjectDoc& o) {
  AnimationDoc* ad = FindAnimationMutable(p, o.animation);
  return ad != nullptr ? &ad->actions : nullptr;
}

inline const ActionDef* FindObjectAction(const Project& p, const ObjectDoc& o,
                                         const std::string& name) {
  const std::vector<ActionDef>* acts = ObjectActions(p, o);
  if (acts == nullptr) return nullptr;
  for (const ActionDef& a : *acts) {
    if (a.name == name) return &a;
  }
  return nullptr;
}

inline ActionDef* FindObjectActionMutable(Project& p, ObjectDoc& o,
                                          const std::string& name) {
  std::vector<ActionDef>* acts = ObjectActionsMutable(p, o);
  if (acts == nullptr) return nullptr;
  for (ActionDef& a : *acts) {
    if (a.name == name) return &a;
  }
  return nullptr;
}


std::filesystem::path ProjectFilePath(const Project& p);
std::filesystem::path ProjectAssetPath(const Project& p, const Asset& a);
std::filesystem::path ProjectObjectPath(const Project& p, const ObjectDoc& o);
std::filesystem::path ProjectTilesetPath(const Project& p, const TilesetDoc& t);
std::filesystem::path ProjectScenePath(const Project& p, const SceneDoc& s);
std::filesystem::path ProjectTransitionsPath(const Project& p);

// 新建：创建目录与 assets/，写 project.json。目录已存在 project.json 时失败。
bool ProjectCreate(const std::filesystem::path& dir, const std::string& name,
                   Project* out, std::string* error);

// 打开：读取并解析 project.json，root_dir 设为文件所在目录。
bool ProjectLoad(const std::filesystem::path& project_json_path, Project* out,
                 std::string* error);

// 保存：把 typed 字段写回 raw（保留未知字段），再写 project.json。
bool ProjectSave(Project* p, std::string* error);

// 导入 PNG：解码验证尺寸 -> 复制到 assets/<id>.png -> 登记资源。
// 不自动保存项目；调用方决定何时 ProjectSave。
bool ProjectImportPng(Project* p, const std::filesystem::path& source_path,
                      std::string* new_asset_id, std::string* error);

// 读取并解码 PNG 为 RGBA8（编辑器的资源预览用）。
// 上限同样是 2048×2048；失败返回 false 并写 error。
bool LoadPngRgba(const std::filesystem::path& path,
                 std::vector<unsigned char>* rgba, int* width, int* height,
                 std::string* error);

// 把 typed 字段同步到 raw（保留未知字段）。ProjectSave 会调用。
void ProjectSyncRaw(Project* p);

}  // namespace pgmmv
