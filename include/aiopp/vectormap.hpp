#pragma once

#include <cassert>
#include <type_traits>
#include <vector>

namespace aiopp {
template <typename T>
class VectorMap {
private:
    using DataElem = std::aligned_storage_t<sizeof(T), alignof(T)>;

public:
    VectorMap() = default;
    VectorMap(size_t size)
        : data_(new DataElem[size])
        , occupied_(size, bool_ { false })
    {
    }

    ~VectorMap() { delete[] data_; }

    size_t size() const { return size_; }

    size_t occupied() const { return numOccupied_; }

    void resize(size_t size)
    {
        assert(size > size_);
        auto newData = new DataElem[size];
        for (size_t i = 0; i < size_; ++i) {
            new (newData + i) T { std::move(*reinterpret_cast<T*>(data_ + i)) };
            reinterpret_cast<T*>(data_ + i)->~T();
        }
        delete[] data_;
        data_ = newData;
        size_ = size;
        occupied_.resize(size, bool_ { false });
    }

    void insert(size_t index, const T& v) { emplace(index, v); }

    void insert(size_t index, T&& v) { emplace(index, std::move(v)); }

    template <typename... Args>
    T& emplace(size_t index, Args&&... args)
    {
        assert(!contains(index));
        if (index >= size_) {
            resize(std::max(std::max(size_ * 2, index + 1), 0ul));
        }
        new (data_ + index) T { std::forward<Args>(args)... };
        occupied_[index].value = true;
        numOccupied_++;
        return *reinterpret_cast<T*>(data_ + index);
    }

    bool contains(size_t index) const { return index < occupied_.size() && occupied_[index].value; }

    void remove(size_t index)
    {
        assert(contains(index));
        reinterpret_cast<T*>(data_ + index)->~T();
        occupied_[index].value = false;
        numOccupied_--;
    }

    T& operator[](size_t index)
    {
        assert(contains(index));
        return *reinterpret_cast<T*>(data_ + index);
    }

    const T& operator[](size_t index) const
    {
        assert(contains(index));
        return *reinterpret_cast<const T*>(data_ + index);
    }

private:
    struct bool_ {
        bool value;
    };

    DataElem* data_ = nullptr;
    size_t size_ = 0;
    size_t numOccupied_ = 0;
    std::vector<bool_> occupied_;
};
}
