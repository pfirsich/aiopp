#include "aiopp/completermap.hpp"

#include <cstdlib>

namespace aiopp {
void* CompleterMap::Empty = nullptr;
void* CompleterMap::Tombstone = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max());

CompleterMap::CompleterMap(size_t numEntries)
    : entries_(getNextSize(numEntries))
{
}

namespace {
    // SplitMix64: https://nullprogram.com/blog/2018/07/31/
    static uint64_t hash(uint64_t x)
    {
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9U;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebU;
        x ^= x >> 31;
        return x;
    }
}

void CompleterMap::insert(uint64_t key, void* value)
{
    assert(size() < capacity());
    const auto h = (hash(key) % (entries_.size() - 1)) + 1;
    for (size_t i = 0; i < entries_.size(); ++i) {
        auto& entry = entries_[(key + i * h) % entries_.size()];
        if (entry.value == Empty || entry.value == Tombstone) {
            entry.key = key;
            entry.value = value;
            size_++;
            return;
        }
    }
    std::abort();
}

void* CompleterMap::get(uint64_t key) const
{
    const auto idx = lookup(key);
    if (!idx) {
        return nullptr;
    }
    return entries_[*idx].value;
}

void* CompleterMap::remove(uint64_t key)
{
    const auto idx = lookup(key);
    if (!idx) {
        return nullptr;
    }
    const auto value = entries_[*idx].value;
    entries_[*idx].value = Tombstone;
    size_--;
    return value;
}

size_t CompleterMap::getNextSize(size_t num)
{
    // https://www.planetmath.org/goodhashtableprimes
    static constexpr auto primes = std::to_array<size_t>(
        { 53, 97, 193, 389, 769, 1543, 3079, 6151, 12289, 24593, 49157, 98317, 196613 });
    assert(num <= primes.back());
    for (const auto prime : primes) {
        if (num < prime) {
            return prime;
        }
    }
    assert(false && "Completer map size too large");
    std::abort();
}

std::optional<size_t> CompleterMap::lookup(uint64_t key) const
{
    const auto h = (hash(key) % (entries_.size() - 1)) + 1;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto idx = (key + i * h) % entries_.size();
        auto& entry = entries_[idx];
        if (entry.value == Empty) {
            return std::nullopt;
        } else if (entry.value != Tombstone && entry.key == key) {
            return idx;
        }
    }
    return std::nullopt;
}
}
