#pragma once

#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sr {

template <typename Value>
class Handles {
public:
    using Id = std::int32_t;
    Id Add(std::uint64_t owner, std::uint32_t type, Value value) {
        if (!owner || !type)
            throw std::runtime_error("invalid handle owner/type");

        for (std::size_t i = 0; i < entries_.size(); ++i) {
            auto& entry = entries_[i];

            if (entry.owner || entry.generation == MaxGeneration)
                continue;

            ++entry.generation;
            entry.owner = owner;
            entry.type = type;
            entry.value = std::move(value);
            return static_cast<Id>((entry.generation << 12) | (i + 1));
        }

        if (entries_.size() >= 4095)
            throw std::runtime_error("handle capacity exhausted");

        entries_.push_back({1, type, owner, std::move(value)});
        return static_cast<Id>((1u << 12) | entries_.size());
    }

    Value& Get(Id id, std::uint64_t owner, std::uint32_t type) {
        return Check(id, owner, type).value;
    }

    bool Contains(Id id, std::uint64_t owner, std::uint32_t type) {
        try {
            Check(id, owner, type);
            return true;
        } catch (const std::runtime_error&) {
            return false;
        }
    }

    void Remove(Id id, std::uint64_t owner, std::uint32_t type) {
        auto& entry = Check(id, owner, type);
        // A resource destructor may reenter this table and grow its storage.
        // Detach its value before destruction so no entry reference survives.
        [[maybe_unused]] Value retired = std::move(entry.value);
        entry.owner = 0;
        entry.type = 0;
        entry.value = {};
    }

    std::vector<Id> Owned(std::uint64_t owner, std::uint32_t type = 0) const {
        std::vector<Id> ids;

        if (!owner)
            return ids;

        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto& entry = entries_[i];

            if (entry.owner == owner && (!type || type == entry.type))
                ids.push_back(static_cast<Id>((entry.generation << 12) | (i + 1)));
        }

        return ids;
    }

    void Retire(std::uint64_t owner, std::uint32_t type = 0) {
        std::vector<Value> retired;
        retired.reserve(entries_.size());

        for (auto& entry : entries_) if (entry.owner == owner && (!type || entry.type == type)) {
            retired.push_back(std::move(entry.value));
            entry.owner = 0;
            entry.type = 0;
            entry.value = {};
        }

        // Every selected handle is invalid before the first destroy callback.
    }

    std::size_t Count() const {
        std::size_t count = 0;

        for (const auto& entry : entries_)
            if (entry.owner)
                ++count;

        return count;
    }

private:
    static constexpr std::uint32_t MaxGeneration = (1u << 19) - 1;

    struct Entry {
        std::uint32_t generation = 0;
        std::uint32_t type = 0;
        std::uint64_t owner = 0;
        Value value{};
    };
    std::vector<Entry> entries_;
    Entry& Check(Id id, std::uint64_t owner, std::uint32_t type) {
        const auto raw = static_cast<std::uint32_t>(id);
        const auto slot = raw & 4095u;

        if (id <= 0 || !slot || slot > entries_.size())
            throw std::runtime_error("invalid handle");

        auto& entry = entries_[slot - 1];

        if (!owner || entry.owner != owner || entry.type != type || entry.generation != (raw >> 12))
            throw std::runtime_error("stale, foreign or wrong-type handle");

        return entry;
    }
};

}
