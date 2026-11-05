#include "runtime/ui.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/render/font.h"
#include "runtime/p3d.h"

namespace r4 {
namespace {

eng::Rect2 R(int x, int y, int w, int h) {
  return eng::Rect2{eng::Vec2{static_cast<float>(x), static_cast<float>(y)},
                    eng::Vec2{static_cast<float>(w), static_cast<float>(h)}};
}

int GlyphAdvance(eng::u32 cp, int spacing) {
  eng::GlyphView g;
  eng::FontGlyph(cp, &g);
  return g.advance + spacing;
}

int MapValue(const UiBinding& b, int raw) {
  if (b.map == 1) {
    const int in_span = b.in_max - b.in_min;
    if (in_span <= 0) return b.out_min;
    int v = raw - b.in_min;
    v = std::max(0, std::min(in_span, v));
    return b.out_min + v * (b.out_max - b.out_min) / in_span;
  }
  if (b.map == 2) {
    const int step = std::max(1, b.in_max);
    return raw / step;
  }
  return raw;
}

void DrawPanel(const UiElement& e, UiRect r, const UiDrawContext& ctx) {
  const eng::Texture* tex = eng::TextureGet(e.texture);
  if (tex == nullptr || !tex->valid()) {
    ctx.dl->DrawRect(R(r.x, r.y, r.w, r.h), e.color);
    return;
  }
  eng::TexRegion src = e.src;
  if (src.whole()) {
    src = eng::TexRegion{0, 0, tex->width, tex->height};
  }
  const int l = e.slice.left;
  const int rr = e.slice.right;
  const int t = e.slice.top;
  const int bb = e.slice.bottom;
  if (l + rr >= r.w || t + bb >= r.h || l <= 0 || t <= 0) {
    ctx.dl->DrawImage(R(r.x, r.y, r.w, r.h), e.texture, e.color, src);
    return;
  }
  // 四角
  ctx.dl->DrawImage(R(r.x, r.y, l, t), e.texture, e.color,
                    eng::TexRegion{src.x, src.y, l, t});
  ctx.dl->DrawImage(R(r.x + r.w - rr, r.y, rr, t), e.texture, e.color,
                    eng::TexRegion{src.x + src.w - rr, src.y, rr, t});
  ctx.dl->DrawImage(R(r.x, r.y + r.h - bb, l, bb), e.texture, e.color,
                    eng::TexRegion{src.x, src.y + src.h - bb, l, bb});
  ctx.dl->DrawImage(R(r.x + r.w - rr, r.y + r.h - bb, rr, bb), e.texture,
                    e.color,
                    eng::TexRegion{src.x + src.w - rr, src.y + src.h - bb, rr, bb});
  // 四边平铺（不拉伸）
  const int inner_w = r.w - l - rr;
  for (int x = 0; x < inner_w;) {
    const int tw = std::min(l, inner_w - x);
    if (tw <= 0) break;
    ctx.dl->DrawImage(R(r.x + l + x, r.y, tw, t), e.texture, e.color,
                      eng::TexRegion{src.x + l, src.y, tw, t});
    ctx.dl->DrawImage(R(r.x + l + x, r.y + r.h - bb, tw, bb), e.texture, e.color,
                      eng::TexRegion{src.x + l, src.y + src.h - bb, tw, bb});
    x += tw;
  }
  const int inner_h = r.h - t - bb;
  for (int y = 0; y < inner_h;) {
    const int th = std::min(t, inner_h - y);
    if (th <= 0) break;
    ctx.dl->DrawImage(R(r.x, r.y + t + y, l, th), e.texture, e.color,
                      eng::TexRegion{src.x, src.y + t, l, th});
    ctx.dl->DrawImage(R(r.x + r.w - rr, r.y + t + y, rr, th), e.texture, e.color,
                      eng::TexRegion{src.x + src.w - rr, src.y + t, rr, th});
    y += th;
  }
  // 中间：平铺中心块；没图就纯色
  if (e.slice.tile && src.w > l + rr && src.h > t + bb) {
    const int cw = src.w - l - rr;
    const int ch = src.h - t - bb;
    for (int y = 0; y < inner_h; y += ch) {
      const int th = std::min(ch, inner_h - y);
      for (int x = 0; x < inner_w; x += cw) {
        const int tw = std::min(cw, inner_w - x);
        ctx.dl->DrawImage(R(r.x + l + x, r.y + t + y, tw, th), e.texture,
                          e.color, eng::TexRegion{src.x + l, src.y + t, tw, th});
      }
    }
  } else {
    ctx.dl->DrawRect(R(r.x + l, r.y + t, inner_w, inner_h), e.color);
  }
}

void DrawText(const std::string& text, UiRect r, const UiElement& e,
              const UiDrawContext& ctx) {
  const std::vector<std::string> lines =
      UiWrapText(text, std::max(1, r.w), e.font_size, 0);
  const int line_h = eng::FontLineHeight();
  int y = r.y;
  for (const std::string& line : lines) {
    const int line_w = UiTextWidth(line, e.font_size, 0);
    int x = r.x;
    if (e.align == 1) x = r.x + (r.w - line_w) / 2;
    else if (e.align == 2) x = r.x + r.w - line_w;
    for (size_t i = 0; i < line.size();) {
      eng::u32 cp = 0;
      const int adv = eng::Utf8Decode(line.c_str() + i, &cp);
      if (adv <= 0) break;
      eng::GlyphView g;
      eng::FontGlyph(cp, &g);
      if (g.valid && g.w > 0 && g.h > 0) {
        ctx.dl->DrawImage(R(x + g.dx, y + g.dy, g.w, g.h), eng::FontTexture(),
                          e.color, eng::TexRegion{g.x, g.y, g.w, g.h});
      }
      x += g.advance;
      i += static_cast<size_t>(adv);
    }
    y += line_h;
  }
}

void DrawElement(const UiElement& e, UiRect parent, const UiDrawContext& ctx);

void DrawValueBar(const UiElement& e, UiRect r, const UiDrawContext& ctx) {
  int raw = 0;
  if (ctx.value != nullptr && !e.binding.name.empty()) {
    raw = ctx.value(ctx.user, e.binding.name.c_str());
  }
  const int mapped = MapValue(e.binding, raw);
  const int out_max = e.binding.out_max != 0 ? e.binding.out_max : 100;
  int fill = r.w;
  if (out_max > 0) {
    fill = std::max(0, std::min(r.w, r.w * mapped / out_max));
  }
  ctx.dl->DrawRect(R(r.x, r.y, r.w, r.h), 0xFF202020u);
  ctx.dl->DrawRect(R(r.x, r.y, fill, r.h), e.color);
}

void DrawValueImage(const UiElement& e, UiRect r, const UiDrawContext& ctx) {
  int raw = 0;
  if (ctx.value != nullptr && !e.binding.name.empty()) {
    raw = ctx.value(ctx.user, e.binding.name.c_str());
  }
  const int frames = std::max(1, e.frames);
  int frame = MapValue(e.binding, raw);
  frame = std::max(0, std::min(frames - 1, frame));
  const int fw = std::max(1, r.w / frames);
  ctx.dl->DrawImage(R(r.x, r.y, r.w, r.h), e.texture, e.color,
                    eng::TexRegion{e.src.x + frame * fw, e.src.y, fw, r.h});
}

void DrawElement(const UiElement& e, UiRect parent, const UiDrawContext& ctx) {
  if (!e.visible_var.empty() && ctx.value != nullptr) {
    if (ctx.value(ctx.user, e.visible_var.c_str()) != e.visible_value) return;
  }
  int iw = e.w;
  int ih = e.h;
  if (iw <= 0) {
    if (e.kind == UiKind::kText) iw = UiTextWidth(e.text, e.font_size, 0);
    else if (e.texture != 0) {
      const eng::Texture* t = eng::TextureGet(e.texture);
      if (t != nullptr && t->valid()) iw = t->width;
    }
  }
  if (ih <= 0) {
    if (e.kind == UiKind::kText) ih = eng::FontLineHeight();
    else if (e.texture != 0) {
      const eng::Texture* t = eng::TextureGet(e.texture);
      if (t != nullptr && t->valid()) ih = t->height;
    }
  }
  const UiRect r = UiAnchorRect(e, parent, iw, ih);
  switch (e.kind) {
    case UiKind::kPanel: DrawPanel(e, r, ctx); break;
    case UiKind::kText: {
      std::string s = e.text;
      if (!e.binding.name.empty() && ctx.value != nullptr) {
        const int v = MapValue(e.binding, ctx.value(ctx.user, e.binding.name.c_str()));
        char buf[96];
        std::snprintf(buf, sizeof(buf), e.binding.format.empty() ? "%d" : e.binding.format.c_str(), v);
        s = buf;
      }
      DrawText(s, r, e, ctx);
      break;
    }
    case UiKind::kImage:
      ctx.dl->DrawImage(R(r.x, r.y, r.w, r.h), e.texture, e.color, e.src);
      break;
    case UiKind::kValueImage: DrawValueImage(e, r, ctx); break;
    case UiKind::kValueBar: DrawValueBar(e, r, ctx); break;
    case UiKind::kValueIcons: {
      int raw = 0;
      if (ctx.value != nullptr && !e.binding.name.empty()) {
        raw = ctx.value(ctx.user, e.binding.name.c_str());
      }
      const int n = std::max(0, std::min(e.frames, MapValue(e.binding, raw)));
      for (int i = 0; i < n; ++i) {
        ctx.dl->DrawImage(R(r.x + i * r.w, r.y, r.w, r.h), e.texture, e.color, e.src);
      }
      break;
    }
    default: break;
  }
  if (!e.children.empty()) {
    const int pad = e.padding;
    const UiRect content{r.x + pad, r.y + pad, std::max(0, r.w - pad * 2),
                         std::max(0, r.h - pad * 2)};
    for (const UiElement& child : e.children) DrawElement(child, content, ctx);
  }
}

}  // namespace

UiRect UiLayoutRect(UiLayout layout, int view_w, int view_h) {
  if (layout == UiLayout::kSafe) {
    const int w = std::min(view_w, 400);
    const int h = std::min(view_h, 240);
    return UiRect{(view_w - w) / 2, (view_h - h) / 2, w, h};
  }
  return UiRect{0, 0, view_w, view_h};
}

UiRect UiAnchorRect(const UiElement& e, UiRect layout, int intrinsic_w,
                    int intrinsic_h) {
  const int w = e.w > 0 ? e.w : intrinsic_w;
  const int h = e.h > 0 ? e.h : intrinsic_h;
  const int col = static_cast<int>(e.anchor) % 3;
  const int row = static_cast<int>(e.anchor) / 3;
  const int x = layout.x + (layout.w - w) * col / 2 + e.x;
  const int y = layout.y + (layout.h - h) * row / 2 + e.y;
  return UiRect{x, y, w, h};
}

int UiTextWidth(const std::string& text, int font_size, int spacing) {
  (void)font_size;
  int w = 0;
  for (size_t i = 0; i < text.size();) {
    eng::u32 cp = 0;
    const int adv = eng::Utf8Decode(text.c_str() + i, &cp);
    if (adv <= 0) break;
    w += GlyphAdvance(cp, spacing);
    i += static_cast<size_t>(adv);
  }
  return w > 0 ? w - spacing : 0;
}

std::vector<std::string> UiWrapText(const std::string& text, int max_w,
                                    int font_size, int spacing) {
  (void)font_size;
  std::vector<std::string> out;
  size_t start = 0;
  while (true) {
    const size_t nl = text.find('\n', start);
    const std::string para =
        text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    std::string line;
    int line_w = 0;
    size_t last_space = std::string::npos;
    for (size_t i = 0; i < para.size();) {
      eng::u32 cp = 0;
      const int adv = eng::Utf8Decode(para.c_str() + i, &cp);
      if (adv <= 0) break;
      const int gw = GlyphAdvance(cp, spacing);
      if (line_w + gw > max_w && !line.empty()) {
        if (last_space != std::string::npos) {
          out.push_back(line.substr(0, last_space));
          line = line.substr(last_space + 1);
        } else {
          out.push_back(line);
          line.clear();
        }
        line_w = UiTextWidth(line, font_size, spacing);
        last_space = std::string::npos;
      }
      if (cp == ' ') last_space = line.size();
      line.append(para, i, static_cast<size_t>(adv));
      line_w += gw;
      i += static_cast<size_t>(adv);
    }
    out.push_back(line);
    if (nl == std::string::npos) break;
    start = nl + 1;
  }
  if (out.empty()) out.push_back("");
  return out;
}

void UiDrawScreen(const UiScreen& screen, const UiDrawContext& ctx) {
  if (ctx.dl == nullptr) return;
  const UiRect layout = UiLayoutRect(screen.layout, ctx.view_w, ctx.view_h);
  for (const UiElement& e : screen.elements) DrawElement(e, layout, ctx);
}
UiElement UiElementFromData(const UiElementData& d, const eng::TextureId* textures,
                            int texture_count) {
  UiElement e;
  e.id = d.id;
  e.kind = static_cast<UiKind>(d.kind);
  e.anchor = static_cast<UiAnchor>(d.anchor);
  e.x = d.x;
  e.y = d.y;
  e.w = d.w;
  e.h = d.h;
  e.slice = UiNineSlice{d.slice_l, d.slice_r, d.slice_t, d.slice_b, d.slice_tile};
  e.texture = (d.image >= 0 && d.image < texture_count && textures != nullptr)
                  ? textures[d.image]
                  : 0;
  e.src = eng::TexRegion{d.src_x, d.src_y, d.src_w, d.src_h};
  e.frames = d.frames;
  e.frame = d.frame;
  e.text = d.text;
  e.font_size = d.font_size;
  e.color = d.color;
  e.align = d.align;
  e.binding.name = d.binding.name;
  e.binding.map = d.binding.map;
  e.binding.in_min = d.binding.in_min;
  e.binding.in_max = d.binding.in_max;
  e.binding.out_min = d.binding.out_min;
  e.binding.out_max = d.binding.out_max;
  e.binding.format = d.binding.format;
  e.visible_var = d.visible_var;
  e.visible_value = d.visible_value;
  e.padding = d.padding;
  for (const UiElementData& child : d.children) {
    e.children.push_back(UiElementFromData(child, textures, texture_count));
  }
  return e;
}

UiScreen UiScreenFromData(const UiScreenData& d, const eng::TextureId* textures,
                          int texture_count) {
  UiScreen s;
  s.id = d.id;
  s.name = d.name;
  s.modal = d.modal;
  s.layout = static_cast<UiLayout>(d.layout);
  for (const UiElementData& e : d.elements) {
    s.elements.push_back(UiElementFromData(e, textures, texture_count));
  }
  return s;
}

}  // namespace r4
