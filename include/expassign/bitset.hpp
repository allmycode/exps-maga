#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace expassign {

// Плотный битсет фиксированного (задаваемого в рантайме) размера с быстрыми
// побитовыми операциями — основной строительный блок индексного матчера.
class DynamicBitset {
 public:
  DynamicBitset() = default;
  explicit DynamicBitset(size_t size)
      : size_(size), words_((size + 63) / 64, 0) {}

  size_t Size() const { return size_; }

  void Set(size_t i) { words_[i >> 6] |= uint64_t{1} << (i & 63); }
  void Reset(size_t i) { words_[i >> 6] &= ~(uint64_t{1} << (i & 63)); }
  bool Test(size_t i) const {
    return (words_[i >> 6] >> (i & 63)) & 1;
  }

  void ResetAll() {
    for (auto& w : words_) w = 0;
  }

  void SetAll() {
    for (auto& w : words_) w = ~uint64_t{0};
    ClearTail();
  }

  // Копирование без реаллокации (размеры должны совпадать).
  void CopyFrom(const DynamicBitset& other) {
    size_ = other.size_;
    words_.assign(other.words_.begin(), other.words_.end());
  }

  DynamicBitset& operator&=(const DynamicBitset& other) {
    for (size_t i = 0; i < words_.size(); ++i) words_[i] &= other.words_[i];
    return *this;
  }

  DynamicBitset& operator|=(const DynamicBitset& other) {
    for (size_t i = 0; i < words_.size(); ++i) words_[i] |= other.words_[i];
    return *this;
  }

  bool Any() const {
    for (uint64_t w : words_) {
      if (w) return true;
    }
    return false;
  }

  template <typename F>
  void ForEachSet(F&& f) const {
    for (size_t wi = 0; wi < words_.size(); ++wi) {
      uint64_t w = words_[wi];
      while (w) {
        unsigned bit = static_cast<unsigned>(std::countr_zero(w));
        w &= w - 1;
        f(wi * 64 + bit);
      }
    }
  }

 private:
  void ClearTail() {
    size_t tail = size_ & 63;
    if (tail && !words_.empty()) {
      words_.back() &= (uint64_t{1} << tail) - 1;
    }
  }

  size_t size_ = 0;
  std::vector<uint64_t> words_;
};

}  // namespace expassign
