#pragma once
#include "XboxRoundedRaster.hpp"
#include <memory>
namespace helengine::xbox {
template<class Texture> class XboxRoundedTextureCache {
public:
    explicit XboxRoundedTextureCache(size_t budget = 4*1024*1024, size_t capacity = 64)
        : budget_(budget), capacity_(capacity) {}
    // Only after the previous frame's GPU work has drained. A hit or insertion
    // pins the resource until this boundary, including batches not flushed yet.
    void BeginFrame() { for (auto& entry : entries_) entry.Pinned = false; }
    void Clear() { entries_.clear(); bytes_ = 0; }
    size_t Bytes() const { return bytes_; }
    template<class Factory> Texture* Get(XboxRoundedStyle style, Factory factory) {
        if (!NormalizeRoundedStyle(style)) return nullptr;
        for (auto& entry : entries_) if (entry.Style == style) {
            entry.Pinned = true;
            return entry.Value.get();
        }
        // Contiguous GPU allocations consume whole 4 KiB pages.
        const size_t pixels = size_t(RoundedRasterExtent(style.Width))*RoundedRasterExtent(style.Height);
        const size_t cost = (pixels*4 + 4095) & ~size_t(4095);
        if (!capacity_ || cost > budget_) return nullptr;
        while (cost > budget_ - bytes_ || entries_.size() >= capacity_) {
            auto victim = entries_.begin();
            while (victim != entries_.end() && victim->Pinned) ++victim;
            if (victim == entries_.end()) return nullptr;
            bytes_ -= victim->Cost;
            entries_.erase(victim);
        }
        auto value = factory(style);
        if (!value) return nullptr;
        auto* result = value.get();
        entries_.push_back({style, std::move(value), cost, true});
        bytes_ += cost;
        return result;
    }
private:
    struct Entry {
        XboxRoundedStyle Style;
        std::unique_ptr<Texture> Value;
        size_t Cost;
        bool Pinned;
    };
    std::vector<Entry> entries_;
    size_t budget_, capacity_, bytes_ = 0;
};
}
