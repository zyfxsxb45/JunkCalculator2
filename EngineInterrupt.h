#ifndef JC2_ENGINE_INTERRUPT_H
#define JC2_ENGINE_INTERRUPT_H

#include <exception>
#include <atomic>

namespace jc {

    class EngineInterruptError : public std::exception {
    public:
        const char* what() const noexcept override {
            return "KeyboardInterrupt";
        }
    };

    extern std::atomic<bool> g_interruptRequested;

    inline void checkInterrupt() {
        if (g_interruptRequested.load(std::memory_order_relaxed)) {
            throw EngineInterruptError();
        }
    }

} // namespace jc

#endif // JC2_ENGINE_INTERRUPT_H
