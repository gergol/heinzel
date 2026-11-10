#pragma once

// A list with a size limit fixed when the type is written. Every list inside a graph or a program is one:
// they are built in a constant expression and kept, which memory a growing container allocates cannot be.
// Running past the limit is a hard error, not a resize: the limits fit the graphs this library is for, and
// growing quietly would move a compile-time cost where nobody is looking.

#include <array>
#include <cstddef>

#include "heinzel/support/fail.hpp"

namespace heinzel {

template <class T, std::size_t N>
struct fixed_vector {
    std::array<T, N> data_{};
    std::size_t size_ = 0;

    constexpr std::size_t size() const { return size_; }

    constexpr bool empty() const { return size_ == 0; }

    static constexpr std::size_t capacity() { return N; }

    constexpr void push_back(const T& value) {
        if (size_ >= N) {
            fail("heinzel::fixed_vector: push_back past capacity");
        }
        data_[size_++] = value;
    }

    // A pop on an empty list is the caller's bug, reported at the line that did it rather than as a
    // wrong answer later.
    constexpr void pop_back() {
        if (size_ == 0) {
            fail("heinzel::fixed_vector: pop_back on an empty vector");
        }
        --size_;
    }

    constexpr void clear() { size_ = 0; }

    constexpr T& operator[](std::size_t i) { return data_[i]; }

    constexpr const T& operator[](std::size_t i) const { return data_[i]; }

    constexpr T* begin() { return data_.data(); }

    constexpr T* end() { return data_.data() + size_; }

    constexpr const T* begin() const { return data_.data(); }

    constexpr const T* end() const { return data_.data() + size_; }

    // Only the part in use is compared: slots past the size may hold leftovers of a pop or a clear,
    // and two lists holding the same things must be equal. Graph and program equality rest on this.
    constexpr bool operator==(const fixed_vector& o) const {
        if (size_ != o.size_) {
            return false;
        }
        for (std::size_t i = 0; i < size_; ++i) {
            if (!(data_[i] == o.data_[i])) {
                return false;
            }
        }
        return true;
    }
};

} // namespace heinzel
