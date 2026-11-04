// pgmmv/animation.h
//
// 动画资源：动作 / 方向 / 帧从对象里抽出来，对象只保留引用。
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pgmmv/json.h"
#include "pgmmv/object.h"

namespace pgmmv {

constexpr int kAnimationFormatVersion = 1;

// ★ 动画资源自带图集和帧尺寸 ★
//   以前帧索引要借对象的 image/frame 来解析，所以"动画"离不开对象；
//   现在这些都归动画自己，对象只负责引用（object.animation）。
struct AnimationDoc {
  int format_version = kAnimationFormatVersion;
  std::string id;
  std::string name;
  std::string image;      // 相对路径；空 = 还没选图
  int frame_w = 32;
  int frame_h = 32;
  std::vector<ActionDef> actions;
  JsonValue raw;
};

inline AnimationDoc AnimationCreateDefault(const std::string& id,
                                           const std::string& name) {
  AnimationDoc a;
  a.id = id;
  a.name = name;
  return a;
}

// 新建对象时用的默认动画：idle/run/jump/fall 四个动作
AnimationDoc AnimationCreateForObject(const std::string& id, const std::string& name,
                                      int frame_w, int frame_h);

bool AnimationLoad(const std::filesystem::path& path, AnimationDoc* out,
                    std::string* error);
bool AnimationSave(const std::filesystem::path& path, AnimationDoc* doc,
                   std::string* error);

}  // namespace pgmmv
