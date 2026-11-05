#pragma once

#include <cstdint>

namespace eng {

// One glyph from a native-size UI face. rows[] is padded to 16 for all sizes;
// only the first H bits are used. Bit x of a row = pixel at x.
struct UiFaceGlyph {
  uint32_t code;
  uint16_t rows[16];
  uint8_t advance;
};

}  // namespace eng
