#pragma once

#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace tinysearch {

// A small thread-safe LRU cache, used to memoize search results for
// repeated queries (spec sections 43-44). Key is caller-defined (the
// SearchEngine builds it from normalized_query + ranker + top_k).
template <typename Value>
class LruCache {
public:
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {}

    std::optional<Value> get(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it == index_.end()) {
            ++misses_;
            return std::nullopt;
        }
        ++hits_;
        // Move to front (most recently used).
        entries_.splice(entries_.begin(), entries_, it->second);
        return it->second->value;
    }

    void put(const std::string& key, Value value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = index_.find(key);
        if (it != index_.end()) {
            it->second->value = std::move(value);
            entries_.splice(entries_.begin(), entries_, it->second);
            return;
        }
        entries_.push_front(Entry{key, std::move(value)});
        index_[key] = entries_.begin();
        if (index_.size() > capacity_) {
            auto& last = entries_.back();
            index_.erase(last.key);
            entries_.pop_back();
        }
    }

    void invalidate_all() {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        index_.clear();
    }

    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }
    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.size();
    }

private:
    struct Entry {
        std::string key;
        Value value;
    };
    std::size_t capacity_;
    mutable std::mutex mutex_;
    std::list<Entry> entries_;  // front = most recently used
    std::unordered_map<std::string, typename std::list<Entry>::iterator> index_;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};

}  // namespace tinysearch
