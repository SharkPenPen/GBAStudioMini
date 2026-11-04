// engine/base/span.h
// 非拥有连续内存视图；C++17 自写，避免引入 <span>。
#pragma once

#include "base/types.h"

namespace eng {

// 非拥有视图。★ 不检查生命周期 ★ —— 调用方负责保证被指向的内存还活着。
template <typename T>
class Span {
 public:
  constexpr Span() : data_(nullptr), size_(0) {}
  constexpr Span(T* data, usize size) : data_(data), size_(size) {}

  // 从定长数组隐式构造（最常见的用法：传数组名）
  template <usize N>
  constexpr Span(T (&arr)[N]) : data_(arr), size_(N) {}

  constexpr T*    data() const { return data_; }
  constexpr usize size() const { return size_; }
  constexpr bool  empty() const { return size_ == 0; }

  constexpr T& operator[](usize i) const { return data_[i]; }

  constexpr T* begin() const { return data_; }
  constexpr T* end() const { return data_ + size_; }

  // 取前 n 个（n 超长时夹到 size）
  constexpr Span first(usize n) const {
    return Span(data_, n < size_ ? n : size_);
  }

  // 从 i 开始到末尾（i 越界时返回空）
  constexpr Span sub(usize i) const {
    return i <= size_ ? Span(data_ + i, size_ - i) : Span();
  }

 private:
  T*    data_;
  usize size_;
};

}  // namespace eng
