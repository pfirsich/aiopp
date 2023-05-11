#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include <fmt/format.h>

struct CompleterMapUnordered {
    CompleterMapUnordered() { map.reserve(1024); }

    void insert(uint64_t key, void* value) { map.emplace(key, value); }
    void* remove(uint64_t key)
    {
        const auto it = map.find(key);
        if (it == map.end()) {
            return nullptr;
        }
        const auto value = it->second;
        map.erase(it);
        return value;
    }

    std::unordered_map<uint64_t, void*> map;
};

class CompleterMapBase {
public:
    CompleterMapBase()
        : entries_(1543) // Hard-coded getNextSize(1024)
    {
    }

    size_t size() const { return size_; }
    size_t capacity() const { return entries_.size(); }
    float loadFactor() const { return static_cast<float>(size_) / entries_.size(); }

protected:
    static void* Empty;
    static void* Tombstone;

    struct Entry {
        uint64_t key = std::numeric_limits<uint64_t>::max();
        void* value = nullptr;
    };

    std::vector<Entry> entries_;
    size_t size_ = 0;
};

void* CompleterMapBase::Empty = nullptr;
void* CompleterMapBase::Tombstone = reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max());

struct CompleterMapLinear : public CompleterMapBase {
    void insert(uint64_t key, void* value)
    {
        assert(size() < capacity());
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto& entry = entries_[(key + i) % entries_.size()];
            if (entry.value == Empty || entry.value == Tombstone) {
                entry.key = key;
                entry.value = value;
                size_++;
                return;
            }
        }
        std::abort();
    }

    std::optional<size_t> lookup(uint64_t key) const
    {
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto idx = (key + i) % entries_.size();
            auto& entry = entries_[idx];
            if (entry.value == Empty) {
                return std::nullopt;
            } else if (entry.value != Tombstone && entry.key == key) {
                return idx;
            }
        }
        return std::nullopt;
    }

    void* remove(uint64_t key)
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
};

struct CompleterMapQuadratic : public CompleterMapBase {
    void insert(uint64_t key, void* value)
    {
        assert(size() < capacity());
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto& entry = entries_[(key + i + i * i) % entries_.size()];
            if (entry.value == Empty || entry.value == Tombstone) {
                entry.key = key;
                entry.value = value;
                size_++;
                return;
            }
        }
        std::abort();
    }

    std::optional<size_t> lookup(uint64_t key) const
    {
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto idx = (key + i + i * i) % entries_.size();
            auto& entry = entries_[idx];
            if (entry.value == Empty) {
                return std::nullopt;
            } else if (entry.value != Tombstone && entry.key == key) {
                return idx;
            }
        }
        return std::nullopt;
    }

    void* remove(uint64_t key)
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
};

struct CompleterMapDouble : public CompleterMapBase {
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

    void insert(uint64_t key, void* value)
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

    std::optional<size_t> lookup(uint64_t key) const
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

    void* remove(uint64_t key)
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
};

struct CompleterMapRobinHood : public CompleterMapBase {
    static uint16_t getProbeLength(void* ptr)
    {
        return static_cast<uint16_t>((reinterpret_cast<uintptr_t>(ptr) & (0xfffful << 48)) >> 48);
    }

    static void* encodeProbeLength(void* ptr, uint16_t probeLength)
    {
        return reinterpret_cast<void*>((static_cast<uintptr_t>(probeLength) << 48)
            | (reinterpret_cast<uintptr_t>(ptr) & 0xffff'ffff'ffff));
    }

    static void* stripProbeLength(void* ptr)
    {
        return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) & 0xffff'ffff'ffff);
    }

    void insert(uint64_t key, void* value)
    {
        assert(size() < capacity());
        size_++;
        for (size_t i = 0; i < entries_.size(); ++i) {
            auto& entry = entries_[(key + i) % entries_.size()];
            if (entry.value == Empty) {
                entry.key = key;
                entry.value = encodeProbeLength(value, i);
                return;
            } else if (getProbeLength(entry.value) < i) {
                // Steal position from the richer entry
                const auto richer = entry;
                entry.key = key;
                entry.value = encodeProbeLength(value, i);
                key = richer.key;
                value = richer.value;
                // will be incremented immediately, which is good, because we just inserted i = 0
                i = 0;
            }
        }
        std::abort();
    }

    std::optional<size_t> lookup(uint64_t key) const
    {
        for (size_t i = 0; i < entries_.size(); ++i) {
            const auto idx = (key + i) % entries_.size();
            auto& entry = entries_[idx];
            if (entry.value == Empty) {
                return std::nullopt;
            } else if (entry.key == key) {
                return idx;
            }
        }
        return std::nullopt;
    }

    void* remove(uint64_t key)
    {
        const auto idx = lookup(key);
        if (!idx) {
            return nullptr;
        }
        const auto value = stripProbeLength(entries_[*idx].value);
        entries_[*idx].value = Empty;
        size_--;
        // Shift backwards
        size_t i = (*idx + 1) % entries_.size();
        while (entries_[i].value != Empty) {
            // Don't shift a key past the optimal position
            const auto pl = getProbeLength(entries_[i].value);
            if (pl == 0) {
                break;
            }
            const auto prev = i > 0 ? i - 1 : entries_.size() - 1;
            entries_[prev].key = entries_[i].key;
            entries_[prev].value = encodeProbeLength(entries_[i].value, pl - 1);
            entries_[i].value = Empty;
            i = (i + 1) % entries_.size();
        }
        return value;
    }
};

struct MapAction {
    enum class Type { Insert, Remove } type;
    uint64_t key;
};

uint64_t parseInt(std::string_view str)
{
    uint64_t v = 0;
    const auto res = std::from_chars(str.data(), str.data() + str.size(), v);
    assert(res.ec == std::errc {} && res.ptr == str.data() + str.size());
    return v;
}

// Returns the number of bytes consumed from buffer
size_t parseActions(std::string_view buffer, std::vector<MapAction>& actions)
{
    size_t cursor = 0;
    while (cursor < buffer.size()) {
        // Process all the lines in the buffer
        const auto nl = buffer.find('\n', cursor);
        if (nl == std::string_view::npos) {
            // No lines left
            return cursor;
        }
        const auto line = buffer.substr(cursor, nl - cursor);
        if (line.starts_with("insert ")) {
            actions.push_back(MapAction { MapAction::Type::Insert, parseInt(line.substr(7)) });
        } else if (line.starts_with("remove ")) {
            actions.push_back(MapAction { MapAction::Type::Remove, parseInt(line.substr(7)) });
        }
        cursor = nl + 1;
    }
    return cursor;
}

std::vector<MapAction> loadActions()
{
    const auto fd = ::open("map_log", O_RDONLY);
    if (fd == -1) {
        fmt::print("Error opening file: {}\n", errno);
        std::exit(1);
    }
    std::vector<MapAction> actions;
    std::string buffer;
    size_t offset = 0;
    while (true) {
        buffer.resize(1024, '\0');
        const auto n = ::read(fd, buffer.data() + offset, buffer.size() - offset);
        if (n < 0) {
            fmt::print("Error reading file: {}\n", errno);
            std::exit(1);
        } else if (n == 0) {
            break;
        }
        assert(n > 0);
        buffer.resize(offset + n);
        const auto parsed = parseActions(buffer, actions);
        offset = buffer.size() - parsed;
        buffer.erase(0, parsed);
    }
    assert(offset == 0);
    return actions;
}

template <typename Map>
std::vector<int64_t> benchmark(const std::vector<MapAction>& actions, size_t num)
{
    std::vector<int64_t> times;
    for (size_t i = 0; i < num; ++i) {
        Map map;
        auto ptr = std::make_unique<int>(0);
        using Clock = std::chrono::steady_clock;
        const auto start = Clock::now();
        for (const auto& action : actions) {
            if (action.type == MapAction::Type::Insert) {
                map.insert(action.key, ptr.get());
            } else if (action.type == MapAction::Type::Remove) {
                const auto p = map.remove(action.key);
                assert(p == ptr.get());
            }
        }
        times.push_back(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count());
    }
    return times;
}

void printResults(std::string_view name, const std::vector<int64_t>& times)
{
    const auto avg = std::accumulate(times.begin(), times.end(), 0ul) / times.size();
    fmt::print("{:<16}: avg {:>7}us (", name, avg);
    for (size_t i = 0; i < times.size(); ++i) {
        if (i > 0) {
            fmt::print(", ");
        }
        fmt::print("{:>7}us", times[i]);
    }
    fmt::print(")\n");
}

int main()
{
    const auto actions = loadActions();

    const size_t num = 5;
    printResults("unordered_map", benchmark<CompleterMapUnordered>(actions, num));
    printResults("linear", benchmark<CompleterMapLinear>(actions, num));
    printResults("quadratic", benchmark<CompleterMapQuadratic>(actions, num));
    printResults("double", benchmark<CompleterMapDouble>(actions, num));
    printResults("robinhood", benchmark<CompleterMapRobinHood>(actions, num));
    return 0;
}
