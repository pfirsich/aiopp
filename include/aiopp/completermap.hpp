#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

using std::size_t;

/* This is a custom map, because the access patterns are very specific:
 * - Values are inserted with incrementing keys
 * - We roughly know the upper bounds of elements, so we don't need rehashing
 * - A lookup should never miss. We will always hit
 * - It's almost always insert and then get/remove (same time) (rarely another get inbetween)
 * - Values are void*
 *
 * Since it's so specific, there is no point in making this generic.
 */

namespace aiopp {
class CompleterMap {
public:
    CompleterMap(size_t numEntries);

    // If the key already exists, you mess up the map, so don't do that.
    void insert(uint64_t key, void* value);
    void* get(uint64_t key) const;
    void* remove(uint64_t key);

    size_t size() const { return size_; }
    size_t capacity() const { return entries_.size(); }
    float loadFactor() const { return static_cast<float>(size_) / entries_.size(); }

private:
    static void* Empty;
    static void* Tombstone;

    struct Entry {
        uint64_t key = std::numeric_limits<uint64_t>::max();
        void* value = nullptr;
    };

    static size_t getNextSize(size_t num);

    std::optional<size_t> lookup(uint64_t key) const;

    std::vector<Entry> entries_;
    size_t size_ = 0;
};
}
