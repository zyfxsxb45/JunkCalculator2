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

    enum class ObjType {
        STRING, BIGINT, FRACTION, COMPLEX, BASENUM,
        REAL_MATRIX, COMPLEX_MATRIX, STRING_MATRIX,
        LIST, DICT, SET,
        CLOSURE, CLASS, INSTANCE, SUPER_PROXY, SYMBOLIC
    };

    struct Obj {
        ObjType type;
        bool isMarked;
        uint32_t refCount = 0; // ★ 新增引用计数，用于 COW
        Obj* next;
        virtual ~Obj() = default;
    };

    class GcHeap {
    public:
        static GcHeap& get() {
            static GcHeap instance;
            return instance;
        }

        template<typename T, typename... Args>
        T* allocate(Args&&... args) {
            T* object = new T(std::forward<Args>(args)...);
            object->isMarked = false;
            object->next = objects_;
            objects_ = object;
            allocsSinceGc_++;
            return object;
        }

        bool shouldCollect() const {
            return allocsSinceGc_ >= gcThreshold_;
        }

        int sweep() {
            Obj** object = &objects_;
            int freed = 0;
            while (*object != nullptr) {
                if (!(*object)->isMarked) {
                    Obj* unreached = *object;
                    *object = unreached->next;
                    delete unreached;
                    freed++;
                } else {
                    (*object)->isMarked = false;
                    object = &(*object)->next;
                }
            }
            allocsSinceGc_ = 0;
            gcThreshold_ = std::max(static_cast<size_t>(256), trackedCount() * 2);
            return freed;
        }

        size_t trackedCount() const {
            size_t count = 0;
            Obj* curr = objects_;
            while(curr) { count++; curr = curr->next; }
            return count;
        }
        size_t threshold() const { return gcThreshold_; }
        size_t allocsSinceGc() const { return allocsSinceGc_; }

    private:
        GcHeap() = default;
        Obj* objects_ = nullptr;
        size_t allocsSinceGc_ = 0;
        size_t gcThreshold_ = 256;
    };

} // namespace jc
#endif // JC2_GCHEAP_H
