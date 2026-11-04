// engine/base/array.h
// 固定容量数组：热路径零 malloc；push_back 满时返回 false，由调用方记账。
#pragma once

#include "base/types.h"

namespace eng {

template <typename T, usize N>
class Array {
 public:
  constexpr usize capacity() const { return N; }
  usize size() const { return size_; }
  bool  empty() const { return size_ == 0; }
  bool  full() const { return size_ == N; }

  // ★ 满了返回 false ★（不覆盖、不崩、不静默丢）
  bool push_back(const T& v) {
    if (size_ >= N) {
      return false;
    }
    items_[size_++] = v;
    return true;
  }

  bool pop_back() {
    if (size_ == 0) {
      return false;
    }
    --size_;
    return true;
  }

  T&       operator[](usize i) { return items_[i]; }
  const T& operator[](usize i) const { return items_[i]; }

  T&       back() { return items_[size_ - 1]; }
  const T& back() const { return items_[size_ - 1]; }

  T*       data() { return items_; }
  const T* data() const { return items_; }

  T* begin() { return items_; }
  T* end() { return items_ + size_; }
  const T* begin() const { return items_; }
  const T* end() const { return items_ + size_; }

  void clear() { size_ = 0; }

 private:
  // T 需要有默认构造（池里的对象基本都是 POD 或简单结构）
  T     items_[N] = {};
  usize size_ = 0;
};

}  // namespace eng
