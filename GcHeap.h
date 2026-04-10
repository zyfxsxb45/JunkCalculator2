// GcHeap.h
#ifndef JC2_GCHEAP_H
#define JC2_GCHEAP_H

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iostream>
#include <unordered_set>
#include <vector>

namespace jc {

    class GcHeap {
    public:
        static GcHeap& get() {
            static GcHeap instance;
            return instance;
        }

        // ═══════════════════════════════════════════
        // 注册条目：每个可 GC 容器记录一条
        // ═══════════════════════════════════════════
        struct GcEntry {
            const void* id;                  // 对象身份标识 (内部 shared_ptr 的裸指针)
            std::function<bool()> alive;     // 是否仍存活 (weak_ptr 未 expired)
            std::function<void()> release;   // 打碎循环 (clear 容器内容)
        };

        // 注册一个新的容器对象 (内部自动去重)
        void track(const void* id,
            std::function<bool()> aliveFn,
            std::function<void()> releaseFn)
        {
            if (knownIds_.count(id)) return;   // 同一个 shared_ptr 只注册一次
            knownIds_.insert(id);
            entries_.push_back({ id, std::move(aliveFn), std::move(releaseFn) });
            allocsSinceGc_++;
        }

        // 是否达到触发阈值
        bool shouldCollect() const {
            return allocsSinceGc_ >= gcThreshold_;
        }

        // ═══════════════════════════════════════════
        // 清扫引擎 (由 VM 在标记完成后调用)
        // ═══════════════════════════════════════════
        int sweep(const std::unordered_set<const void*>& markedIds) {
            // ═══ 两阶段清扫：先分类计数，再统一释放 ═══
            std::vector<GcEntry> surviving;
            std::vector<GcEntry> unreachable;
            surviving.reserve(entries_.size());

            for (auto& entry : entries_) {
                if (!entry.alive()) {
                    // 已被引用计数自然释放，丢弃记录
                    knownIds_.erase(entry.id);
                    continue;
                }
                if (markedIds.count(entry.id)) {
                    // 可达且存活，保留
                    surviving.push_back(std::move(entry));
                }
                else {
                    // 活着但不可达 → 孤岛！先收集，稍后统一打碎
                    unreachable.push_back(std::move(entry));
                }
            }

            // ★ 在释放之前就锁定计数，因为释放 A 可能级联销毁 B
            int freed = static_cast<int>(unreachable.size());

            // 统一打碎所有孤岛的循环引用
            for (auto& entry : unreachable) {
                if (entry.alive()) entry.release();  // 可能已被级联销毁，检查再释放
                knownIds_.erase(entry.id);
            }

            entries_ = std::move(surviving);

            // 自适应阈值
            allocsSinceGc_ = 0;
            gcThreshold_ = std::max(static_cast<size_t>(256), entries_.size() * 2);

            return freed;
        }

        // 统计信息 (供 gcinfo() 使用)
        size_t trackedCount() const { return entries_.size(); }
        size_t threshold() const { return gcThreshold_; }
        size_t allocsSinceGc() const { return allocsSinceGc_; }

    private:
        GcHeap() = default;

        std::vector<GcEntry> entries_;
        std::unordered_set<const void*> knownIds_;
        size_t allocsSinceGc_ = 0;
        size_t gcThreshold_ = 256;
    };

} // namespace jc
#endif // JC2_GCHEAP_H
