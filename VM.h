#ifndef JC2_VM_H
#define JC2_VM_H

#include "Bytecode.h"
#include "Value.h"
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <set>

namespace jc {

    using NativeCallable = std::function<Value(const std::vector<Value>&)>;

    class VM {
    private:

        struct RefWriteback {
            int argIndex;
            Value modifiedValue;
        };
        std::vector<RefWriteback> pendingRefWritebacks;

        // ★ 多帧栈：支持嵌套函数调用
        std::vector<CallFrame> frames;
        static constexpr int MAX_FRAMES = 1024;

        CallFrame& frame() { return frames.back(); }
        const Chunk& currentChunk() { return frame().function->chunk; }

        std::vector<Value> stack;
        static constexpr int MAX_STACK = 65536;

        std::map<std::string, Value> globals;
        std::map<std::string, NativeCallable> nativeBuiltins;
        std::set<std::string> constGlobals;              // ★ 新增：const 变量追踪

        // ★ 存储编译后的函数对象
        std::vector<std::shared_ptr<CompiledFunction>> compiledFunctions;

        // 栈操作
        void push(const Value& val);
        Value pop();
        Value& peek(int distance = 0);

        // 读取指令流（从当前帧）
        uint8_t readByte();
        uint16_t readShort();
        OpCode readOp();

        static bool isTruthy(const Value& val);

        // ★ 执行主循环
        Value run(int targetFrameDepth = 0);
        int currentTargetFrameDepth = 0;

        // ★ 异常处理帧
        struct ExceptionHandler {
            int frameIndex = 0;     // 哪个 CallFrame
            int ip = 0;            // catch 块的起始地址
            int stackSize = 0;      // 进入 try 时的栈大小
            std::string catchVarName = ""; // catch 变量名（空则不绑定）
        };
        std::vector<ExceptionHandler> exceptionHandlers;

        void throwError(const std::string& msg);
        Value callDunder(const Value& obj, const std::string& name,
            const std::vector<Value>& args);

        std::map<std::string, std::set<int>> builtinArity;  // ★ 新增
        std::set<std::string> importedModules;               // ★ 防重复导入

    public:
        VM();

        void registerBuiltin(const std::string& name, NativeCallable fn, std::set<int> arity);
        void setGlobal(const std::string& name, const Value& val);
        inline static VM* activeVM = nullptr;
        // ★ 接受编译后的函数列表
        void setCompiledFunctions(const std::vector<std::shared_ptr<CompiledFunction>>& fns) {
            compiledFunctions = fns;  // ★ 拷贝，不移动
        }
        Value callVMFunction(int fnIdx, const std::vector<Value>& args,
            std::shared_ptr<std::vector<Value>> upvalues = nullptr);

        Value execute(const Chunk& mainChunk);

        const std::map<std::string, Value>& getGlobals() const { return globals; }
        void clearGlobals() {
            globals.clear();
            constGlobals.clear();
        }
        void removeGlobal(const std::string& name) {
            globals.erase(name);
            constGlobals.erase(name);
        }
    };

} // namespace jc

#endif // JC2_VM_H
