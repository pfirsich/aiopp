#pragma once

#include <queue>

#include "vectormap.hpp"

namespace aiopp {
template <typename T>
class SlotMap {
public:
    SlotMap()
    {
    }

    SlotMap(size_t size)
        : data_(size)
    {
    }

    size_t size() const
    {
        return data_.occupied();
    }

    bool contains(size_t index) const
    {
        return data_.contains(index);
    }

    void resize(size_t size)
    {
        assert(size > data_.size());
        nextIndex_ = data_.size();
        data_.resize(size);
    }

    size_t insert(const T& v)
    {
        const auto idx = getNewIndex();
        data_.insert(idx, v);
        return idx;
    }

    size_t insert(T&& v)
    {
        const auto idx = getNewIndex();
        data_.insert(idx, std::move(v));
        return idx;
    }

    template <typename... Args>
    size_t emplace(Args&&... args)
    {
        const auto idx = getNewIndex();
        data_.emplace(idx, std::forward<Args>(args)...);
        return idx;
    }

    void remove(size_t index)
    {
        assert(data_.contains(index));
        data_.remove(index);
        freeList_.push(index);
    }

    T& operator[](size_t index)
    {
        assert(data_.contains(index));
        return data_[index];
    }

    const T& operator[](size_t index) const
    {
        assert(data_.contains(index));
        return data_[index];
    }

private:
    size_t getNewIndex()
    {
        if (!freeList_.empty()) {
            const auto idx = freeList_.front();
            freeList_.pop();
            return idx;
        }
        return nextIndex_++;
    }

    VectorMap<T> data_;
    size_t nextIndex_ = 0;
    std::queue<size_t> freeList_;
};
}
