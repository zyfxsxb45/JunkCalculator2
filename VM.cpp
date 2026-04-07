#include "VM.h"
#include "Module.h"
#include <iostream>
#include <cmath>
#include <stdexcept>

namespace jc {

    static bool vmValuesEqual(const Value& lhs, const Value& rhs) {
        // ── 1. 同 variant index 快速路径 ──
        if (lhs.data.index() == rhs.data.index()) {
            // none == none
            if (std::holds_alternative<std::monostate>(lhs.data))
                return true;
            // double == double
            if (std::holds_alternative<double>(lhs.data))
                return Tol::isEq(std::get<double>(lhs.data), std::get<double>(rhs.data));
            // BigInt == BigInt
            if (std::holds_alternative<BigInt>(lhs.data))
                return std::get<BigInt>(lhs.data) == std::get<BigInt>(rhs.data);
            // Complex == Complex
            if (std::holds_alternative<Complex>(lhs.data))
                return std::get<Complex>(lhs.data) == std::get<Complex>(rhs.data);
            // Fraction == Fraction
            if (std::holds_alternative<Fraction>(lhs.data))
                return std::get<Fraction>(lhs.data) == std::get<Fraction>(rhs.data);
            // String == String
            if (std::holds_alternative<std::string>(lhs.data))
                return std::get<std::string>(lhs.data) == std::get<std::string>(rhs.data);
            // BaseNum == BaseNum
            if (std::holds_alternative<BaseNum>(lhs.data))
                return std::get<BaseNum>(lhs.data).getValue() ==
                std::get<BaseNum>(rhs.data).getValue();
            // RealMatrix == RealMatrix
            if (std::holds_alternative<RealMatrix>(lhs.data)) {
                const auto& a = std::get<RealMatrix>(lhs.data);
                const auto& b = std::get<RealMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!Tol::isEq(a(i, j), b(i, j))) return false;
                return true;
            }
            // ComplexMatrix == ComplexMatrix
            if (std::holds_alternative<ComplexMatrix>(lhs.data)) {
                const auto& a = std::get<ComplexMatrix>(lhs.data);
                const auto& b = std::get<ComplexMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!(a(i, j) == b(i, j))) return false;
                return true;
            }
            // List — 递归逐元素比较
            if (std::holds_alternative<List>(lhs.data)) {
                const auto& a = std::get<List>(lhs.data);
                const auto& b = std::get<List>(rhs.data);
                if (a.size() != b.size()) return false;
                for (size_t i = 0; i < a.size(); ++i) {
                    try {
                        Value va = std::any_cast<Value>(a.raw()[i]);
                        Value vb = std::any_cast<Value>(b.raw()[i]);
                        if (!vmValuesEqual(va, vb)) return false;
                    }
                    catch (...) { return false; }
                }
                return true;
            }
            // Dict — 键值对逐项比较
            if (std::holds_alternative<Dict>(lhs.data)) {
                const auto& a = std::get<Dict>(lhs.data);
                const auto& b = std::get<Dict>(rhs.data);
                if (a.size() != b.size()) return false;
                for (const auto& [key, val] : a.getEntries()) {
                    const auto* bval = b.get(key);
                    if (!bval) return false;
                    try {
                        Value va = std::any_cast<Value>(val);
                        Value vb = std::any_cast<Value>(*bval);
                        if (!vmValuesEqual(va, vb)) return false;
                    }
                    catch (...) { return false; }
                }
                return true;
            }
            // StringMatrix — 逐元素比较
            if (std::holds_alternative<StringMatrix>(lhs.data)) {
                const auto& a = std::get<StringMatrix>(lhs.data);
                const auto& b = std::get<StringMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (a(i, j) != b(i, j)) return false;
                return true;
            }
            // Instance identity
            if (std::holds_alternative<std::shared_ptr<Instance>>(lhs.data))
                return std::get<std::shared_ptr<Instance>>(lhs.data).get() ==
                std::get<std::shared_ptr<Instance>>(rhs.data).get();
            return false;
        }

        // ── 2. 跨类型比较 ──
        // BigInt vs Fraction
        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data))
            return Fraction(std::get<BigInt>(lhs.data)) == std::get<Fraction>(rhs.data);
        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))
            return std::get<Fraction>(lhs.data) == Fraction(std::get<BigInt>(rhs.data));

        // RealMatrix vs ComplexMatrix
        if ((std::holds_alternative<RealMatrix>(lhs.data) && std::holds_alternative<ComplexMatrix>(rhs.data)) ||
            (std::holds_alternative<ComplexMatrix>(lhs.data) && std::holds_alternative<RealMatrix>(rhs.data))) {
            try {
                ComplexMatrix a = lhs.asComplexMatrix(), b = rhs.asComplexMatrix();
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!(a(i, j) == b(i, j))) return false;
                return true;
            }
            catch (...) { return false; }
        }

        // ── 3. 通用数值降级比较 ──
        // none vs anything-else → false
        if (std::holds_alternative<std::monostate>(lhs.data) ||
            std::holds_alternative<std::monostate>(rhs.data))
            return false;

        // 尝试转 Complex 比较（覆盖 double/BigInt/Fraction/Complex 的所有交叉组合）
        try { return lhs.asComplex() == rhs.asComplex(); }
        catch (...) { return false; }
    }

    // ════════════════════════════════════════════════════
    // Dunder 方法调度辅助
    // ════════════════════════════════════════════════════

    // 查找实例的 dunder 方法（沿继承链）
    static std::shared_ptr<FunctionClosure> findDunder(
        const Value& val, const std::string& name)
    {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(val.data))
            return nullptr;
        auto inst = std::get<std::shared_ptr<Instance>>(val.data);
        auto c = inst->classDef;
        while (c) {
            auto it = c->methods.find(name);
            if (it != c->methods.end()) return it->second;
            c = c->parent;
        }
        return nullptr;
    }

    // 调用 dunder 方法（设置 self，通过 NativeCallable 执行）
    Value VM::callDunder(const Value& obj, const std::string& name,
        const std::vector<Value>& args)
    {
        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
        auto method = findDunder(obj, name);
        if (!method || !method->nativeFn.has_value() ||
            method->nativeFn.type() != typeid(NativeCallable))
            throw std::runtime_error("VM Error: No callable dunder '" + name + "'.");

        globals["self"] = Value(inst);
        auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
        return fn(args);
    }

    VM::VM() {
        globals["PI"] = Value(3.14159265358979323846);
        globals["E"] = Value(2.71828182845904523536);
        globals["i"] = Value(Complex(0.0, 1.0));
        globals["I"] = Value(Complex(0.0, 1.0));
        globals["true"] = Value(1.0);
        globals["false"] = Value(0.0);

        registerBuiltin("__vm_delete__", [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1 || !std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("VM Error: __vm_delete__ expects a string variable name.");
            const std::string& name = std::get<std::string>(args[0].data);
            auto it = globals.find(name);
            if (it == globals.end())
                throw std::runtime_error("Runtime Error: Undefined variable '" + name + "'.");
            globals.erase(it);
            constGlobals.erase(name);
            return Value::none();
            });
    }

    void VM::registerBuiltin(const std::string& name, NativeCallable fn) {
        nativeBuiltins[name] = std::move(fn);
    }

    void VM::setGlobal(const std::string& name, const Value& val) {
        globals[name] = val;
    }

    // ── 栈操作 ──

    void VM::push(const Value& val) {
        if (static_cast<int>(stack.size()) >= MAX_STACK)
            throw std::runtime_error("VM Error: Stack overflow.");
        stack.push_back(val);
    }

    Value VM::pop() {
        if (stack.empty()) throw std::runtime_error("VM Error: Stack underflow.");
        Value val = std::move(stack.back());
        stack.pop_back();
        return val;
    }

    Value& VM::peek(int distance) {
        return stack[stack.size() - 1 - distance];
    }

    // ── 指令流读取（从当前帧）──

    uint8_t VM::readByte() {
        return currentChunk().code[frame().ip++];
    }

    uint16_t VM::readShort() {
        uint8_t hi = readByte();
        uint8_t lo = readByte();
        return static_cast<uint16_t>((hi << 8) | lo);
    }

    OpCode VM::readOp() { return static_cast<OpCode>(readByte()); }

    bool VM::isTruthy(const Value& val) {
        if (std::holds_alternative<std::monostate>(val.data)) return false;
        if (std::holds_alternative<double>(val.data))
            return !Tol::isEq(std::get<double>(val.data), 0.0);
        if (std::holds_alternative<BigInt>(val.data))
            return !std::get<BigInt>(val.data).isZero();
        if (std::holds_alternative<std::string>(val.data))
            return !std::get<std::string>(val.data).empty();
        return true;
    }

    // ── 入口 ──

    Value VM::execute(const Chunk& c) {
        activeVM = this;
        auto mainFn = std::make_shared<CompiledFunction>();
        mainFn->name = "<script>";
        mainFn->chunk = c;
        stack.clear();
        frames.clear();
        exceptionHandlers.clear();          // ★ 清除跨语句残留的过期处理器
        CallFrame mainFrame;
        mainFrame.function = mainFn.get();
        mainFrame.ip = 0;
        mainFrame.stackBase = 0;
        frames.push_back(mainFrame);
        return run(0);
    }

    // ★ 如果有异常处理器，走 handler；否则直接 C++ throw
    void VM::throwError(const std::string& msg) {
        if (!exceptionHandlers.empty()) {
            auto handler = exceptionHandlers.back();
            exceptionHandlers.pop_back();
            while (static_cast<int>(frames.size()) > handler.frameIndex + 1)
                frames.pop_back();
            stack.resize(handler.stackSize);
            push(Value(msg));
            frame().ip = handler.ip;
        }
        else {
            throw std::runtime_error(msg);
        }
    }

    Value VM::callVMFunction(int fnIdx, const std::vector<Value>& args,
        std::shared_ptr<std::vector<Value>> upvalues) {
        if (fnIdx < 0 || fnIdx >= static_cast<int>(compiledFunctions.size()))
            throw std::runtime_error("VM Error: Invalid function index in callback.");

        auto& fn = compiledFunctions[fnIdx];

        // ★ 保存外层状态
        int savedTargetFrameDepth = currentTargetFrameDepth;
        auto savedRefWritebacks = pendingRefWritebacks;
        pendingRefWritebacks.clear();

        // 压入参数
        for (const auto& arg : args)
            push(arg);

        // 填充默认参数
        int padCount = fn->maxArity - static_cast<int>(args.size());
        for (int j = 0; j < padCount; ++j)
            push(Value::none());

        // 创建帧
        CallFrame newFrame;
        newFrame.function = fn.get();
        newFrame.ip = 0;
        newFrame.stackBase = static_cast<int>(stack.size()) - fn->maxArity;
        newFrame.upvalues = upvalues;
        frames.push_back(newFrame);

        int boundary = static_cast<int>(frames.size()) - 1;

        Value result;
        try {
            result = run(boundary);
        }
        catch (...) {
            // ★ 异常安全：确保状态恢复
            currentTargetFrameDepth = savedTargetFrameDepth;
            pendingRefWritebacks = savedRefWritebacks;
            throw;  // 继续传播
        }

        // ★ 提取本次调用的 ref writebacks
        auto myRefWritebacks = pendingRefWritebacks;

        // ★ 恢复外层状态
        currentTargetFrameDepth = savedTargetFrameDepth;
        pendingRefWritebacks = savedRefWritebacks;

        // ★ 将本次调用的 ref writebacks 合并到外层
        if (!myRefWritebacks.empty()) {
            pendingRefWritebacks = myRefWritebacks;
        }

        return result;
    }

    // ══════════════════════════════════════════════
    // 核心执行循环
    // ══════════════════════════════════════════════
    Value VM::run(int targetFrameDepth) {
        currentTargetFrameDepth = targetFrameDepth;
        while (true) {
            if (frame().ip >= static_cast<int>(currentChunk().code.size())) {
                return stack.empty() ? Value::none() : pop();
            }

            OpCode op;
            try {
                op = readOp();
            }
            catch (...) {
                return stack.empty() ? Value::none() : pop();
            }

            try {

                switch (op) {

                    // ── 常量与特殊值 ──
                case OpCode::OP_CONSTANT: {
                    uint16_t idx = readShort();
                    push(currentChunk().constants[idx]);
                    break;
                }
                case OpCode::OP_NONE:  push(Value::none()); break;
                case OpCode::OP_TRUE:  push(Value(1.0)); break;
                case OpCode::OP_FALSE: push(Value(0.0)); break;
                case OpCode::OP_POP:   pop(); break;

                    // ── 算术运算 ──
                case OpCode::OP_ADD: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__add__");
                    if (d) { push(callDunder(a, "__add__", { b })); break; }
                    d = findDunder(b, "__radd__");
                    if (d) { push(callDunder(b, "__radd__", { a })); break; }
                    push(a + b);
                    break;
                }
                case OpCode::OP_SUBTRACT: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__sub__");
                    if (d) { push(callDunder(a, "__sub__", { b })); break; }
                    d = findDunder(b, "__rsub__");
                    if (d) { push(callDunder(b, "__rsub__", { a })); break; }
                    push(a - b);
                    break;
                }
                case OpCode::OP_MULTIPLY: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__mul__");
                    if (d) { push(callDunder(a, "__mul__", { b })); break; }
                    d = findDunder(b, "__rmul__");
                    if (d) { push(callDunder(b, "__rmul__", { a })); break; }
                    push(a * b);
                    break;
                }
                case OpCode::OP_DIVIDE: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__div__");
                    if (d) { push(callDunder(a, "__div__", { b })); break; }
                    d = findDunder(b, "__rdiv__");
                    if (d) { push(callDunder(b, "__rdiv__", { a })); break; }
                    push(a / b);
                    break;
                }
                case OpCode::OP_MODULO: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__mod__");
                    if (d) { push(callDunder(a, "__mod__", { b })); break; }
                    d = findDunder(b, "__rmod__");
                    if (d) { push(callDunder(b, "__rmod__", { a })); break; }
                    push(a % b);
                    break;
                }
                case OpCode::OP_POWER: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__pow__");
                    if (d) { push(callDunder(a, "__pow__", { b })); break; }
                    d = findDunder(b, "__rpow__");
                    if (d) { push(callDunder(b, "__rpow__", { a })); break; }
                    push(a ^ b);
                    break;
                }
                case OpCode::OP_NEGATE: {
                    Value a = pop();
                    auto d = findDunder(a, "__neg__");
                    if (d) { push(callDunder(a, "__neg__", {})); break; }
                    push(-a);
                    break;
                }
                case OpCode::OP_NOT: { push(Value(isTruthy(pop()) ? 0.0 : 1.0)); break; }

                                   // ── 比较 ──
                // ── 比较运算（带 dunder 调度）──
                case OpCode::OP_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__eq__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__eq__", { b })) ? 1.0 : 0.0)); break; }
                    push(Value(vmValuesEqual(a, b) ? 1.0 : 0.0));
                    break;
                }
                case OpCode::OP_NOT_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__neq__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__neq__", { b })) ? 1.0 : 0.0)); break; }
                    d = findDunder(a, "__eq__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__eq__", { b })) ? 0.0 : 1.0)); break; }
                    push(Value(vmValuesEqual(a, b) ? 0.0 : 1.0));
                    break;
                }
                case OpCode::OP_LESS: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__lt__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__lt__", { b })) ? 1.0 : 0.0)); break; }
                    if (std::holds_alternative<BigInt>(a.data) && std::holds_alternative<BigInt>(b.data))
                        push(Value(std::get<BigInt>(a.data) < std::get<BigInt>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<Fraction>(a.data) && std::holds_alternative<Fraction>(b.data))
                        push(Value(std::get<Fraction>(a.data) < std::get<Fraction>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<std::string>(a.data) && std::holds_alternative<std::string>(b.data))
                        push(Value(std::get<std::string>(a.data) < std::get<std::string>(b.data) ? 1.0 : 0.0));
                    else
                        push(Value(a.asDouble() < b.asDouble() ? 1.0 : 0.0));
                    break;
                }
                case OpCode::OP_LESS_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__le__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__le__", { b })) ? 1.0 : 0.0)); break; }
                    if (std::holds_alternative<BigInt>(a.data) && std::holds_alternative<BigInt>(b.data))
                        push(Value(std::get<BigInt>(a.data) <= std::get<BigInt>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<Fraction>(a.data) && std::holds_alternative<Fraction>(b.data))
                        push(Value(std::get<Fraction>(a.data) <= std::get<Fraction>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<std::string>(a.data) && std::holds_alternative<std::string>(b.data))
                        push(Value(std::get<std::string>(a.data) <= std::get<std::string>(b.data) ? 1.0 : 0.0));
                    else
                        push(Value(a.asDouble() <= b.asDouble() ? 1.0 : 0.0));
                    break;
                }
                case OpCode::OP_GREATER: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__gt__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__gt__", { b })) ? 1.0 : 0.0)); break; }
                    if (std::holds_alternative<BigInt>(a.data) && std::holds_alternative<BigInt>(b.data))
                        push(Value(std::get<BigInt>(a.data) > std::get<BigInt>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<Fraction>(a.data) && std::holds_alternative<Fraction>(b.data))
                        push(Value(std::get<Fraction>(a.data) > std::get<Fraction>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<std::string>(a.data) && std::holds_alternative<std::string>(b.data))
                        push(Value(std::get<std::string>(a.data) > std::get<std::string>(b.data) ? 1.0 : 0.0));
                    else
                        push(Value(a.asDouble() > b.asDouble() ? 1.0 : 0.0));
                    break;
                }
                case OpCode::OP_GREATER_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__ge__");
                    if (d) { push(Value(isTruthy(callDunder(a, "__ge__", { b })) ? 1.0 : 0.0)); break; }
                    if (std::holds_alternative<BigInt>(a.data) && std::holds_alternative<BigInt>(b.data))
                        push(Value(std::get<BigInt>(a.data) >= std::get<BigInt>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<Fraction>(a.data) && std::holds_alternative<Fraction>(b.data))
                        push(Value(std::get<Fraction>(a.data) >= std::get<Fraction>(b.data) ? 1.0 : 0.0));
                    else if (std::holds_alternative<std::string>(a.data) && std::holds_alternative<std::string>(b.data))
                        push(Value(std::get<std::string>(a.data) >= std::get<std::string>(b.data) ? 1.0 : 0.0));
                    else
                        push(Value(a.asDouble() >= b.asDouble() ? 1.0 : 0.0));
                    break;
                }

                                             // ── 全局变量 ──
                case OpCode::OP_GET_GLOBAL: {
                    uint16_t idx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[idx].data);
                    auto it = globals.find(name);
                    if (it != globals.end()) {
                        push(it->second);
                    }
                    else if (nativeBuiltins.count(name)) {
                        // ★★★ 包装为真正的 FunctionClosure，而非字符串标记 ★★★
                        auto closure = std::make_shared<FunctionClosure>(
                            std::vector<std::string>{},
                            std::vector<bool>{},
                            name,
                            nullptr
                        );
                        closure->nativeFn = std::make_any<NativeCallable>(nativeBuiltins[name]);
                        push(Value(closure));
                    }
                    else {
                        throw std::runtime_error("VM Error: Undefined variable '" + name + "'.");
                    }
                    break;
                }
                case OpCode::OP_SET_GLOBAL: {
                    uint16_t idx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[idx].data);
                    // ★ const 保护
                    if (constGlobals.count(name))
                        throw std::runtime_error("Runtime Error: Cannot modify const variable '" + name + "'.");
                    globals[name] = peek(0);
                    break;
                }
                case OpCode::OP_DEFINE_GLOBAL: {
                    uint16_t idx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[idx].data);
                    globals[name] = peek(0);       // ★ 改为 peek（不弹出，与 SET_GLOBAL 一致）
                    constGlobals.insert(name);      // ★ 标记为 const
                    break;
                }

                                             // ── 局部变量 ──
                case OpCode::OP_GET_LOCAL: {
                    uint16_t slot = readShort();
                    push(stack[frame().stackBase + slot]);
                    break;
                }
                case OpCode::OP_SET_LOCAL: {
                    uint16_t slot = readShort();
                    stack[frame().stackBase + slot] = peek(0);
                    break;
                }

                                         // ── 跳转 ──
                case OpCode::OP_JUMP: {
                    uint16_t offset = readShort();
                    frame().ip += offset;
                    break;
                }
                case OpCode::OP_JUMP_IF_FALSE: {
                    uint16_t offset = readShort();
                    if (!isTruthy(peek(0))) frame().ip += offset;
                    break;
                }
                case OpCode::OP_LOOP: {
                    uint16_t offset = readShort();
                    frame().ip -= offset;
                    break;
                }

                                    // ── ★★★ 函数闭包创建 ★★★ ──
                case OpCode::OP_CLOSURE: {
                    uint16_t fnConstIdx = readShort();
                    int idx = static_cast<int>(std::round(
                        currentChunk().constants[fnConstIdx].asDouble()));
                    if (idx < 0 || idx >= static_cast<int>(compiledFunctions.size()))
                        throw std::runtime_error("VM Error: Invalid function index.");

                    auto& fn = compiledFunctions[idx];

                    // ★ 捕获 upvalues（无论有没有，统一处理）
                    std::shared_ptr<std::vector<Value>> captures;
                    if (!fn->upvalues.empty()) {
                        captures = std::make_shared<std::vector<Value>>();
                        for (auto& uv : fn->upvalues) {
                            if (uv.isLocal) {
                                int captureIdx = frame().stackBase + uv.index;
                                if (captureIdx >= 0 && captureIdx < static_cast<int>(stack.size()))
                                    captures->push_back(stack[captureIdx]);
                                else
                                    captures->push_back(Value::none());
                            }
                            else {
                                if (frame().upvalues &&
                                    uv.index < static_cast<int>(frame().upvalues->size()))
                                    captures->push_back((*frame().upvalues)[uv.index]);
                                else
                                    captures->push_back(Value::none());
                            }
                        }
                    }

                    // ★★★ 始终创建 FunctionClosure + 真正的 NativeCallable ★★★
                    auto closure = std::make_shared<FunctionClosure>(
                        std::vector<std::string>{},
                        std::vector<bool>{},
                        fn->name,
                        nullptr
                    );

                    // ★ NativeCallable 回调 VM 执行编译后的函数
                    int capturedFnIdx = idx;
                    auto capturedUpvalues = captures;  // 可能是 nullptr
                    VM* vm = this;

                    closure->nativeFn = std::make_any<NativeCallable>(
                        [vm, capturedFnIdx, capturedUpvalues](const std::vector<Value>& args) -> Value {
                            return vm->callVMFunction(capturedFnIdx, args, capturedUpvalues);
                        }
                    );

                    // ★ 同时存储 upvalues 以便 OP_GET_UPVALUE 使用
                    if (captures) {
                        closure->capturedEnv = std::make_any<std::shared_ptr<std::vector<Value>>>(captures);
                    }

                    // ★ 设置参数信息使 acceptsArgCount/minArgs/maxArgs 正确工作
                    for (int j = 0; j < fn->maxArity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                    }
                    closure->defaultValues.resize(fn->maxArity, Value::none());
                    // arity 之前的参数标记为必需
                    for (int j = 0; j < fn->arity; ++j) {
                        closure->defaultValues[j] = Value(); // non-none = required (dummy trick)
                    }
                    // 修正：用一个空的非none值标记"必需"不正确
                    // 只保留 arity 个参数名
                    closure->paramNames.clear();
                    closure->isRef.clear();
                    closure->defaultValues.clear();
                    for (int j = 0; j < fn->arity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                    }
                    // 有默认值的参数
                    for (int j = fn->arity; j < fn->maxArity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                        closure->defaultValues.push_back(Value::none());
                    }

                    push(Value(closure));
                    break;
                }

                                       // ── ★★★ 统一函数调用 ★★★ ──
                case OpCode::OP_CALL: {
                    uint8_t argc = readByte();
                    Value callee = stack[stack.size() - 1 - argc]; // ★ 值拷贝，防 push 导致引用失效
                    pendingRefWritebacks.clear(); // ★★★ 清除上一次残留，防止跨调用污染 ★★★

                    // ════════════════════════════════════════════════
                    // 路径 A: 类构造函数 ClassName(args)
                    // ════════════════════════════════════════════════
                    // 在 OP_CALL 路径 A（类构造函数）中：
                    if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(callee.data)) {
                        auto cls = std::get<std::shared_ptr<ClassDefinition>>(callee.data);
                        auto instance = std::make_shared<Instance>();
                        instance->classDef = cls;

                        // 查找 init（沿继承链）
                        std::shared_ptr<FunctionClosure> initMethod;
                        std::shared_ptr<ClassDefinition> initOwner;
                        auto c = cls;
                        while (c) {
                            auto it = c->methods.find("init");
                            if (it != c->methods.end()) {
                                initMethod = it->second;
                                initOwner = c;
                                break;
                            }
                            c = c->parent;
                        }

                        if (initMethod && initMethod->nativeFn.has_value() &&
                            initMethod->nativeFn.type() == typeid(NativeCallable)) {
                            globals["self"] = Value(instance);
                            globals["__class__"] = Value(initOwner);  // ★ 记录 init 所属类
                            std::vector<Value> args(argc);
                            for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                            pop();
                            auto& fn = std::any_cast<NativeCallable&>(initMethod->nativeFn);
                            fn(args);
                            push(Value(instance));
                        }
                        else if (!initMethod) {
                            for (int j = 0; j < argc; ++j) pop();
                            pop();
                            push(Value(instance));
                        }
                        else {
                            throw std::runtime_error("VM Error: init has no callable implementation.");
                        }
                        break;
                    }

                    // ════════════════════════════════════════════════
                    // 路径 B: FunctionClosure（闭包对象）
                    // ════════════════════════════════════════════════
                    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(callee.data)) {
                        auto closure = std::get<std::shared_ptr<FunctionClosure>>(callee.data);
                        if (closure->nativeFn.has_value() &&
                            closure->nativeFn.type() == typeid(NativeCallable)) {
                            // ★ 所有闭包（VM编译的 + Evaluator原生的）都走这条路
                            std::vector<Value> args(argc);
                            for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                            pop(); // callee
                            auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
                            push(fn(args));
                            break;
                        }
                        throw std::runtime_error("VM Error: Invalid closure (unrecognized nativeFn type).");
                    }

                    // ════════════════════════════════════════════════
                    // 路径 C: 字符串标记 "__fn:N"（无捕获的普通函数）
                    // ════════════════════════════════════════════════
                    if (std::holds_alternative<std::string>(callee.data)) {
                        const std::string& tag = std::get<std::string>(callee.data);

                        if (tag.size() >= 5 && tag.substr(0, 5) == "__fn:") {
                            int fnIdx = std::stoi(tag.substr(5));
                            if (fnIdx < 0 || fnIdx >= static_cast<int>(compiledFunctions.size()))
                                throw std::runtime_error("VM Error: Function index " +
                                    std::to_string(fnIdx) + " out of range (have " +
                                    std::to_string(compiledFunctions.size()) + ").");
                            auto& fn = compiledFunctions[fnIdx];

                            if (static_cast<int>(argc) < fn->arity || static_cast<int>(argc) > fn->maxArity)
                                throw std::runtime_error("VM Error: '" + fn->name +
                                    "' expects " + std::to_string(fn->arity) +
                                    " to " + std::to_string(fn->maxArity) +
                                    " arguments, got " + std::to_string(argc) + ".");

                            if (static_cast<int>(frames.size()) >= MAX_FRAMES)
                                throw std::runtime_error("VM Error: Stack overflow (call depth exceeded).");

                            // 填充默认参数
                            int padCount = fn->maxArity - static_cast<int>(argc);
                            for (int j = 0; j < padCount; ++j)
                                push(Value::none());
                            int effectiveArgc = fn->maxArity;

                            CallFrame newFrame;
                            newFrame.function = fn.get();
                            newFrame.ip = 0;
                            newFrame.stackBase = static_cast<int>(stack.size()) - effectiveArgc;

                            // 移除 callee
                            stack.erase(stack.begin() + newFrame.stackBase - 1);
                            newFrame.stackBase--;

                            frames.push_back(newFrame);
                            break;
                        }

                        // ════════════════════════════════════════════════
                        // 路径 D: 内建函数标记 "__builtin:name"
                        // ════════════════════════════════════════════════
                        if (tag.size() >= 10 && tag.substr(0, 10) == "__builtin:") {
                            std::string fnName = tag.substr(10);
                            std::vector<Value> args(argc);
                            for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                            pop(); // callee
                            auto nit = nativeBuiltins.find(fnName);
                            if (nit == nativeBuiltins.end())
                                throw std::runtime_error("VM Error: Unknown builtin '" + fnName + "'.");
                            push(nit->second(args));
                            break;
                        }
                    }

                    // ════════════════════════════════════════════════
                    // 路径 E: 无法识别 → 报错
                    // ════════════════════════════════════════════════
                    {
                        std::vector<Value> args(argc);
                        for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                        Value calleeVal = pop();
                        std::string desc;
                        if (std::holds_alternative<std::string>(calleeVal.data))
                            desc = std::get<std::string>(calleeVal.data);
                        else {
                            std::ostringstream oss; oss << calleeVal;
                            desc = oss.str();
                        }
                        throw std::runtime_error("VM Error: '" + desc + "' is not callable.");
                    }
                } // end OP_CALL

                case OpCode::OP_GET_UPVALUE: {
                    uint16_t idx = readShort();
                    if (!frame().upvalues || idx >= frame().upvalues->size())
                        throw std::runtime_error("VM Error: Invalid upvalue index " +
                            std::to_string(idx) + ".");
                    push((*frame().upvalues)[idx]);
                    break;
                }

                case OpCode::OP_SET_UPVALUE: {
                    uint16_t idx = readShort();
                    if (!frame().upvalues || idx >= frame().upvalues->size())
                        throw std::runtime_error("VM Error: Invalid upvalue index " +
                            std::to_string(idx) + ".");
                    (*frame().upvalues)[idx] = peek(0);
                    break;
                }

                case OpCode::OP_SUPER_INVOKE: {
                    uint16_t nameIdx = readShort();
                    uint8_t argc = readByte();
                    std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);

                    // 栈: [..., self, arg0, arg1, ..., argN-1]
                    Value selfVal = stack[stack.size() - 1 - argc];

                    if (!std::holds_alternative<std::shared_ptr<Instance>>(selfVal.data))
                        throw std::runtime_error("VM Error: 'super' requires an instance context.");

                    auto inst = std::get<std::shared_ptr<Instance>>(selfVal.data);

                    // ★ 从 __class__ 获取当前方法所属的类，然后取其父类
                    auto classIt = globals.find("__class__");
                    if (classIt == globals.end() ||
                        !std::holds_alternative<std::shared_ptr<ClassDefinition>>(classIt->second.data))
                        throw std::runtime_error("VM Error: 'super' requires class context (__class__).");

                    auto currentClass = std::get<std::shared_ptr<ClassDefinition>>(classIt->second.data);
                    auto parentClass = currentClass->parent;
                    if (!parentClass)
                        throw std::runtime_error("VM Error: Class '" + currentClass->name +
                            "' has no parent class.");

                    // 从父类开始查找方法
                    std::shared_ptr<FunctionClosure> method;
                    std::shared_ptr<ClassDefinition> owningClass;
                    auto c = parentClass;
                    while (c) {
                        auto it = c->methods.find(methodName);
                        if (it != c->methods.end()) {
                            method = it->second;
                            owningClass = c;
                            break;
                        }
                        c = c->parent;
                    }
                    if (!method)
                        throw std::runtime_error("VM Error: Parent class has no method '" +
                            methodName + "'.");

                    // ★ 设置上下文：self 不变，__class__ 切换到方法所属类
                    globals["self"] = Value(inst);
                    globals["__class__"] = Value(owningClass);

                    if (method->nativeFn.has_value() &&
                        method->nativeFn.type() == typeid(NativeCallable)) {
                        std::vector<Value> args(argc);
                        for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                        pop(); // self
                        auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
                        push(fn(args));
                        break;
                    }

                    throw std::runtime_error("VM Error: Parent method '" + methodName +
                        "' has no callable implementation.");
                }

                case OpCode::OP_GET_SUPER: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);

                    Value selfVal = pop();

                    if (!std::holds_alternative<std::shared_ptr<Instance>>(selfVal.data))
                        throw std::runtime_error("VM Error: 'super' requires an instance context.");

                    auto inst = std::get<std::shared_ptr<Instance>>(selfVal.data);

                    auto classIt = globals.find("__class__");
                    if (classIt == globals.end() ||
                        !std::holds_alternative<std::shared_ptr<ClassDefinition>>(classIt->second.data))
                        throw std::runtime_error("VM Error: 'super' requires class context.");

                    auto currentClass = std::get<std::shared_ptr<ClassDefinition>>(classIt->second.data);
                    auto parentClass = currentClass->parent;
                    if (!parentClass)
                        throw std::runtime_error("VM Error: No parent class.");

                    // 从父类查找方法/字段
                    auto c = parentClass;
                    while (c) {
                        auto it = c->methods.find(field);
                        if (it != c->methods.end()) {
                            push(Value(it->second));
                            break;
                        }
                        c = c->parent;
                    }
                    if (!c)
                        throw std::runtime_error("VM Error: Parent class has no method '" + field + "'.");
                    break;
                }

                                    // ── 旧版内建函数调用（保留兼容性）──
                case OpCode::OP_CALL_BUILTIN: {
                    uint16_t nameIdx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    uint8_t argc = readByte();
                    std::vector<Value> args(argc);
                    for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                    auto it = nativeBuiltins.find(name);
                    if (it == nativeBuiltins.end())
                        throw std::runtime_error("VM Error: Unknown builtin '" + name + "'.");
                    push(it->second(args));
                    break;
                }

                case OpCode::OP_REF_WRITEBACK: {
                    uint8_t count = readByte();

                    for (int k = 0; k < count; ++k) {
                        uint8_t argIndex = readByte();
                        uint8_t sourceType = readByte();
                        uint16_t sourceRef = readShort();

                        // 在 pendingRefWritebacks 中查找匹配的 argIndex
                        bool found = false;
                        Value modifiedVal;
                        for (auto& rw : pendingRefWritebacks) {
                            if (rw.argIndex == static_cast<int>(argIndex)) {
                                modifiedVal = rw.modifiedValue;
                                found = true;
                                break;
                            }
                        }

                        if (!found) continue;  // 该参数不是 ref，跳过

                        // 写回到参数来源
                        switch (sourceType) {
                        case 1: { // global
                            std::string name = std::get<std::string>(
                                currentChunk().constants[sourceRef].data);
                            globals[name] = modifiedVal;
                            break;
                        }
                        case 2: { // local
                            int localIdx = frame().stackBase + sourceRef;
                            if (localIdx < static_cast<int>(stack.size()))
                                stack[localIdx] = modifiedVal;
                            break;
                        }
                        case 3: { // upvalue
                            if (frame().upvalues &&
                                sourceRef < frame().upvalues->size())
                                (*frame().upvalues)[sourceRef] = modifiedVal;
                            break;
                        }
                        }
                    }

                    pendingRefWritebacks.clear();
                    break;
                }

                                            // ── ★★★ 返回 ★★★ ──
                case OpCode::OP_RETURN: {
                    Value result = pop();
                    int base = frame().stackBase;
                    std::string fnName = frame().function->name;

                    // ★ 保存 ref 值
                    pendingRefWritebacks.clear();
                    const auto& refFlags = frame().function->paramIsRef;
                    if (!refFlags.empty()) {
                        for (int i = 0; i < static_cast<int>(refFlags.size()); ++i) {
                            if (refFlags[i]) {
                                int localIdx = base + i;
                                if (localIdx < static_cast<int>(stack.size())) {
                                    pendingRefWritebacks.push_back({ i, stack[localIdx] });
                                }
                            }
                        }
                    }

                    frames.pop_back();

                    if (static_cast<int>(frames.size()) <= currentTargetFrameDepth) {
                        if (currentTargetFrameDepth == 0) {
                            stack.clear();
                        }
                        else {
                            stack.resize(base);
                        }
                        return result;
                    }

                    stack.resize(base);
                    if (fnName == "init") {
                        auto it = globals.find("self");
                        if (it != globals.end())
                            push(it->second);
                        else
                            push(result);
                    }
                    else {
                        push(result);
                    }
                    break;
                }

                                      // ── ★ 字符串转换 ──
                case OpCode::OP_STRINGIFY: {
                    Value v = pop();
                    if (std::holds_alternative<std::string>(v.data)) {
                        push(v);
                    }
                    else {
                        // ★ Instance __str__ 钩子
                        auto d = findDunder(v, "__str__");
                        if (d) {
                            push(callDunder(v, "__str__", {}));
                        }
                        else {
                            std::ostringstream oss;
                            oss << v;
                            push(Value(oss.str()));
                        }
                    }
                    break;
                }

                                         // ── ★ 字符串拼接 ──
                case OpCode::OP_CONCAT_STRINGS: {
                    uint16_t count = readShort();
                    std::string result;
                    // 从栈底到栈顶顺序拼接
                    std::vector<std::string> parts(count);
                    for (int j = count - 1; j >= 0; --j) {
                        Value v = pop();
                        if (std::holds_alternative<std::string>(v.data))
                            parts[j] = std::get<std::string>(v.data);
                        else {
                            std::ostringstream oss; oss << v;
                            parts[j] = oss.str();
                        }
                    }
                    for (const auto& p : parts) result += p;
                    push(Value(result));
                    break;
                }

                case OpCode::OP_SLICE_GET: {
                    uint8_t dims = readByte();

                    // 每个维度在栈上有 3 个值：start, end, step
                    // 栈布局 (1D): [obj, start, end, step]
                    // 栈布局 (2D): [obj, rStart, rEnd, rStep, cStart, cEnd, cStep]

                    // 辅助：从栈上读取可选 int（none → -1 表示缺省）
                    auto readOptionalInt = [this]() -> std::pair<bool, int> {
                        Value v = pop();
                        if (v.isNone()) return { false, 0 };
                        return { true, static_cast<int>(std::round(v.asDouble())) };
                        };

                    // 辅助：构建索引列表
                    auto buildSliceIndices = [](int dimSize, std::pair<bool, int> start,
                        std::pair<bool, int> end,
                        std::pair<bool, int> step) -> std::vector<int> {
                            int sp = step.first ? step.second : 1;
                            if (sp == 0) throw std::runtime_error("VM Error: Slice step cannot be zero.");

                            int st, en;
                            if (sp > 0) {
                                st = start.first ? start.second : 0;
                                en = end.first ? end.second : dimSize;
                            }
                            else {
                                st = start.first ? start.second : dimSize - 1;
                                en = end.first ? end.second : -1;
                            }

                            // 负索引处理
                            if (st < 0) st = dimSize + st;
                            if (en < 0 && end.first) en = dimSize + en;

                            // 边界裁剪
                            if (sp > 0) {
                                st = std::max(0, std::min(dimSize, st));
                                en = std::max(0, std::min(dimSize, en));
                            }
                            else {
                                st = std::max(-1, std::min(dimSize - 1, st));
                                en = std::max(-1, std::min(dimSize - 1, en));
                            }

                            std::vector<int> ids;
                            if (sp > 0) {
                                for (int i = st; i < en; i += sp) ids.push_back(i);
                            }
                            else {
                                for (int i = st; i > en; i += sp) ids.push_back(i);
                            }
                            return ids;
                        };

                    if (dims == 1) {
                        auto step = readOptionalInt();
                        auto end = readOptionalInt();
                        auto start = readOptionalInt();
                        Value obj = pop();

                        // ── String 切片 ──
                        if (std::holds_alternative<std::string>(obj.data)) {
                            const auto& s = std::get<std::string>(obj.data);
                            auto ids = buildSliceIndices(static_cast<int>(s.size()), start, end, step);
                            std::string result;
                            for (int id : ids) result += s[id];
                            push(Value(result));
                            break;
                        }

                        // ── RealMatrix 切片 ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            const auto& m = std::get<RealMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);
                            std::vector<double> result;
                            if (m.getRows() == 1) {
                                for (int id : ids) result.push_back(m(0, id));
                                push(Value(RealMatrix(1, static_cast<int>(result.size()), result)));
                            }
                            else if (m.getCols() == 1) {
                                for (int id : ids) result.push_back(m(id, 0));
                                push(Value(RealMatrix(static_cast<int>(result.size()), 1, result)));
                            }
                            else {
                                // 多行矩阵：取行子集
                                int rc = static_cast<int>(ids.size());
                                std::vector<double> flat;
                                for (int id : ids)
                                    for (int j = 0; j < m.getCols(); ++j)
                                        flat.push_back(m(id, j));
                                push(Value(RealMatrix(rc, m.getCols(), flat)));
                            }
                            break;
                        }

                        // ── ComplexMatrix 切片 ──
                        if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            const auto& m = std::get<ComplexMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);
                            if (m.getRows() == 1) {
                                std::vector<Complex> result;
                                for (int id : ids) result.push_back(m(0, id));
                                push(Value(ComplexMatrix(1, static_cast<int>(result.size()), result)));
                            }
                            else if (m.getCols() == 1) {
                                std::vector<Complex> result;
                                for (int id : ids) result.push_back(m(id, 0));
                                push(Value(ComplexMatrix(static_cast<int>(result.size()), 1, result)));
                            }
                            else {
                                int rc = static_cast<int>(ids.size());
                                std::vector<Complex> flat;
                                for (int id : ids)
                                    for (int j = 0; j < m.getCols(); ++j)
                                        flat.push_back(m(id, j));
                                push(Value(ComplexMatrix(rc, m.getCols(), flat)));
                            }
                            break;
                        }

                        // ── StringMatrix 切片 ──
                        if (std::holds_alternative<StringMatrix>(obj.data)) {
                            const auto& m = std::get<StringMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);
                            if (m.getRows() == 1) {
                                std::vector<std::string> result;
                                for (int id : ids) result.push_back(m(0, id));
                                push(Value(StringMatrix(1, static_cast<int>(result.size()), result)));
                            }
                            else if (m.getCols() == 1) {
                                std::vector<std::string> result;
                                for (int id : ids) result.push_back(m(id, 0));
                                push(Value(StringMatrix(static_cast<int>(result.size()), 1, result)));
                            }
                            else {
                                int rc = static_cast<int>(ids.size());
                                std::vector<std::string> flat;
                                for (int id : ids)
                                    for (int j = 0; j < m.getCols(); ++j)
                                        flat.push_back(m(id, j));
                                push(Value(StringMatrix(rc, m.getCols(), flat)));
                            }
                            break;
                        }

                        // ── List 切片 ──
                        if (std::holds_alternative<List>(obj.data)) {
                            const auto& L = std::get<List>(obj.data);
                            auto ids = buildSliceIndices(static_cast<int>(L.size()), start, end, step);
                            List result;
                            for (int id : ids) result.push_back(L.raw()[id]);
                            push(Value(result));
                            break;
                        }

                        throw std::runtime_error("VM Error: Cannot slice this type.");
                    }
                    else if (dims == 2) {
                        // 2D 切片：栈上 [obj, rStart, rEnd, rStep, cStart, cEnd, cStep]
                        auto cStep = readOptionalInt();
                        auto cEnd = readOptionalInt();
                        auto cStart = readOptionalInt();
                        auto rStep = readOptionalInt();
                        auto rEnd = readOptionalInt();
                        auto rStart = readOptionalInt();
                        Value obj = pop();

                        auto processMatSlice = [&](const auto& m) {
                            auto rIds = buildSliceIndices(m.getRows(), rStart, rEnd, rStep);
                            auto cIds = buildSliceIndices(m.getCols(), cStart, cEnd, cStep);

                            using MatType = std::decay_t<decltype(m)>;
                            using ElemType = std::decay_t<decltype(m(0, 0))>;
                            std::vector<ElemType> flat;
                            for (int ri : rIds)
                                for (int ci : cIds)
                                    flat.push_back(m(ri, ci));
                            push(Value(MatType(static_cast<int>(rIds.size()),
                                static_cast<int>(cIds.size()), flat)));
                            };

                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            processMatSlice(std::get<RealMatrix>(obj.data));
                        }
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            processMatSlice(std::get<ComplexMatrix>(obj.data));
                        }
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            processMatSlice(std::get<StringMatrix>(obj.data));
                        }
                        else {
                            throw std::runtime_error("VM Error: 2D slicing requires a matrix.");
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Unsupported slice dimensionality.");
                    }
                    break;
                }

                                              // ── ★ 解构 ──
                case OpCode::OP_DESTRUCT: {
                    uint8_t count = readByte();
                    Value rhs = pop();

                    std::vector<Value> elements;
                    if (std::holds_alternative<RealMatrix>(rhs.data)) {
                        for (double d : std::get<RealMatrix>(rhs.data).rawData())
                            elements.push_back(Value(d));
                    }
                    else if (std::holds_alternative<ComplexMatrix>(rhs.data)) {
                        for (const auto& c : std::get<ComplexMatrix>(rhs.data).rawData())
                            elements.push_back(Value(c));
                    }
                    else if (std::holds_alternative<List>(rhs.data)) {
                        for (const auto& e : std::get<List>(rhs.data).raw())
                            elements.push_back(std::any_cast<Value>(e));
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot destructure this type.");
                    }

                    if (static_cast<int>(elements.size()) != count)
                        throw std::runtime_error("VM Error: Destructuring size mismatch.");

                    // ★ 正序压栈：elements[0] 最深，elements[N-1] 在栈顶
                    for (int j = 0; j < count; ++j) {
                        push(elements[j]);
                    }
                    break;
                }

                                        // ── ★ 异常处理 ──
                case OpCode::OP_TRY_BEGIN: {
                    uint16_t catchRelOffset = readShort();
                    uint16_t catchNameIdx = readShort();
                    (void)catchNameIdx;

                    ExceptionHandler handler;
                    handler.frameIndex = static_cast<int>(frames.size()) - 1;
                    // ★ catch 地址 = 当前 ip + catchRelOffset
                    // 当前 ip 已经指向 TRY_BEGIN 之后的第一条指令
                    handler.ip = frame().ip + catchRelOffset;
                    handler.stackSize = static_cast<int>(stack.size());
                    exceptionHandlers.push_back(handler);
                    break;
                }

                case OpCode::OP_TRY_END: {
                    if (!exceptionHandlers.empty())
                        exceptionHandlers.pop_back();
                    break;
                }

                case OpCode::OP_THROW: {
                    Value errVal = pop();
                    std::string msg;
                    if (std::holds_alternative<std::string>(errVal.data))
                        msg = std::get<std::string>(errVal.data);
                    else {
                        std::ostringstream oss; oss << errVal;
                        msg = oss.str();
                    }

                    // ★ 只处理属于当前 run() 作用域的处理器
                    if (!exceptionHandlers.empty() &&
                        exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                        auto handler = exceptionHandlers.back();
                        exceptionHandlers.pop_back();
                        while (static_cast<int>(frames.size()) > handler.frameIndex + 1)
                            frames.pop_back();
                        stack.resize(handler.stackSize);
                        push(Value(msg));
                        frame().ip = handler.ip;
                        break;
                    }
                    // ★ 处理器属于外层 run() → 通过 C++ 异常传播
                    throw std::runtime_error(msg);
                }

                                     // ── ★ 字典构建 ──
                case OpCode::OP_BUILD_DICT: {
                    uint16_t count = readShort();
                    Dict d;
                    // 栈上有 2*count 个值：key1, val1, key2, val2, ...
                    // 先收集到临时数组
                    std::vector<std::pair<std::string, Value>> pairs(count);
                    for (int j = count - 1; j >= 0; --j) {
                        Value val = pop();
                        Value key = pop();
                        std::string keyStr;
                        if (std::holds_alternative<std::string>(key.data))
                            keyStr = std::get<std::string>(key.data);
                        else {
                            std::ostringstream oss; oss << key;
                            keyStr = oss.str();
                        }
                        pairs[j] = { keyStr, val };
                    }
                    for (auto& [k, v] : pairs)
                        d.set(k, std::make_any<Value>(v));
                    push(Value(d));
                    break;
                }

                                          // ── ★ 栈复制 ──
                case OpCode::OP_DUP: {
                    push(peek(0));
                    break;
                }

                                   // ── ★ 迭代器：for-in ──
                case OpCode::OP_ITER_INIT: {
                    uint8_t destructFlag = readByte();  // ★ 新增：0=keys only, 1=pairs
                    // 将栈顶的可迭代对象展开为 [List_of_elements, index=0]
                    Value iterable = pop();
                    List elements;
                    if (std::holds_alternative<RealMatrix>(iterable.data)) {
                        const auto& m = std::get<RealMatrix>(iterable.data);
                        if (m.getRows() == 1) {
                            for (int j = 0; j < m.getCols(); ++j)
                                elements.push_back(std::make_any<Value>(Value(m(0, j))));
                        }
                        else if (m.getCols() == 1) {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements.push_back(std::make_any<Value>(Value(m(ii, 0))));
                        }
                        else {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements.push_back(std::make_any<Value>(Value(m.getRow(ii))));
                        }
                    }
                    else if (std::holds_alternative<List>(iterable.data)) {
                        elements = std::get<List>(iterable.data);
                    }
                    else if (std::holds_alternative<std::string>(iterable.data)) {
                        for (char c : std::get<std::string>(iterable.data))
                            elements.push_back(std::make_any<Value>(Value(std::string(1, c))));
                    }
                    else if (std::holds_alternative<Dict>(iterable.data)) {
                        const auto& d = std::get<Dict>(iterable.data);
                        if (destructFlag) {
                            // ★ 解构模式：产出 [key, value] 对
                            for (const auto& [key, val] : d.getEntries()) {
                                List pair;
                                pair.push_back(std::make_any<Value>(Value(key)));
                                pair.push_back(std::make_any<Value>(std::any_cast<Value>(val)));
                                elements.push_back(std::make_any<Value>(Value(pair)));
                            }
                        }
                        else {
                            // ★ 非解构模式：只产出 key（与 Evaluator 一致）
                            for (const auto& [key, val] : d.getEntries()) {
                                elements.push_back(std::make_any<Value>(Value(key)));
                            }
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot iterate over this type.");
                    }
                    push(Value(elements));           // 元素列表
                    push(Value(0.0));                // 索引 = 0
                    break;
                }

                case OpCode::OP_ITER_NEXT: {
                    uint16_t offset = readShort();
                    // 栈: [..., elements(List), index(double)]
                    double idx = peek(0).asDouble();
                    const auto& elems = std::get<List>(peek(1).data);
                    int i = static_cast<int>(idx);
                    if (i >= static_cast<int>(elems.size())) {
                        // 迭代结束
                        frame().ip += offset;
                    }
                    else {
                        // 取元素，递增索引
                        Value elem = std::any_cast<Value>(elems.raw()[i]);
                        stack[stack.size() - 1] = Value(idx + 1);  // 更新索引
                        push(elem);  // 压入当前元素
                    }
                    break;
                }

                                         // ── ★ 矩阵构建 ──
                case OpCode::OP_BUILD_MATRIX: {
                    uint16_t rows = readShort();
                    uint16_t cols = readShort();
                    int total = rows * cols;

                    bool hasComplex = false;
                    bool hasString = false;
                    bool hasOther = false;

                    auto canBeMatrixElement = [](const Value& v) -> bool {
                        return std::holds_alternative<double>(v.data) ||
                            std::holds_alternative<BigInt>(v.data) ||
                            std::holds_alternative<Fraction>(v.data) ||
                            std::holds_alternative<BaseNum>(v.data) ||
                            std::holds_alternative<Complex>(v.data) ||
                            std::holds_alternative<std::string>(v.data) ||
                            std::holds_alternative<RealMatrix>(v.data) ||
                            std::holds_alternative<ComplexMatrix>(v.data) ||
                            std::holds_alternative<StringMatrix>(v.data);
                        };

                    // 类型扫描
                    for (int ii = 0; ii < total; ++ii) {
                        const Value& v = stack[stack.size() - total + ii];
                        if (std::holds_alternative<Complex>(v.data) ||
                            std::holds_alternative<ComplexMatrix>(v.data))
                            hasComplex = true;
                        if (std::holds_alternative<std::string>(v.data) ||
                            std::holds_alternative<StringMatrix>(v.data))
                            hasString = true;
                        if (!canBeMatrixElement(v))
                            hasOther = true;
                    }

                    Value result;

                    // ═══ 含不可矩阵化元素 → 降级为 List ═══
                    if (hasOther) {
                        if (rows == 1) {
                            // 单行 → 扁平 List
                            List L;
                            for (int ii = 0; ii < total; ++ii)
                                L.push_back(std::make_any<Value>(stack[stack.size() - total + ii]));
                            result = Value(L);
                        }
                        else {
                            // 多行 → List of Lists
                            List outer;
                            for (int i = 0; i < rows; ++i) {
                                List inner;
                                for (int j = 0; j < cols; ++j)
                                    inner.push_back(std::make_any<Value>(
                                        stack[stack.size() - total + i * cols + j]));
                                outer.push_back(std::make_any<Value>(Value(inner)));
                            }
                            result = Value(outer);
                        }
                    }
                    // ═══ 含子矩阵 → 块拼接（与 Evaluator 一致）═══
                    else {
                        // 检查是否含有子矩阵需要块拼接
                        bool hasSubMatrix = false;
                        for (int ii = 0; ii < total; ++ii) {
                            const Value& v = stack[stack.size() - total + ii];
                            if (std::holds_alternative<RealMatrix>(v.data) ||
                                std::holds_alternative<ComplexMatrix>(v.data) ||
                                std::holds_alternative<StringMatrix>(v.data))
                                hasSubMatrix = true;
                        }

                        if (hasSubMatrix) {
                            // ★ 块矩阵拼接：逐元素升维为矩阵，逐行 integR，逐列 integC
                            auto extractCell = [&](Value& cell) {
                                // 标量 → 1×1 矩阵
                                if (!std::holds_alternative<RealMatrix>(cell.data) &&
                                    !std::holds_alternative<ComplexMatrix>(cell.data) &&
                                    !std::holds_alternative<StringMatrix>(cell.data)) {
                                    if (hasString) {
                                        std::ostringstream oss; oss << cell;
                                        cell = Value(StringMatrix(1, 1, { oss.str() }));
                                    }
                                    else if (hasComplex) {
                                        cell = Value(ComplexMatrix(1, 1, { cell.asComplex() }));
                                    }
                                    else {
                                        cell = Value(RealMatrix(1, 1, { cell.asDouble() }));
                                    }
                                }
                                // 类型统一升维
                                if (hasString) {
                                    if (std::holds_alternative<RealMatrix>(cell.data)) {
                                        const auto& m = std::get<RealMatrix>(cell.data);
                                        std::vector<std::string> flat;
                                        for (int i = 0; i < m.getRows(); ++i)
                                            for (int j = 0; j < m.getCols(); ++j) {
                                                std::ostringstream oss; oss << Value(m(i, j));
                                                flat.push_back(oss.str());
                                            }
                                        cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                                    }
                                    else if (std::holds_alternative<ComplexMatrix>(cell.data)) {
                                        const auto& m = std::get<ComplexMatrix>(cell.data);
                                        std::vector<std::string> flat;
                                        for (int i = 0; i < m.getRows(); ++i)
                                            for (int j = 0; j < m.getCols(); ++j) {
                                                std::ostringstream oss; oss << Value(m(i, j));
                                                flat.push_back(oss.str());
                                            }
                                        cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                                    }
                                }
                                else if (hasComplex && std::holds_alternative<RealMatrix>(cell.data)) {
                                    cell = Value(cell.asComplexMatrix());
                                }
                                };

                            try {
                                int idx = 0;
                                Value matResult = Value::none();
                                for (int i = 0; i < rows; ++i) {
                                    Value rowResult = Value::none();
                                    for (int j = 0; j < cols; ++j) {
                                        Value cell = stack[stack.size() - total + idx++];
                                        extractCell(cell);
                                        if (rowResult.isNone()) {
                                            rowResult = cell;
                                        }
                                        else {
                                            if (hasString)
                                                rowResult = Value(std::get<StringMatrix>(rowResult.data)
                                                    .integR(std::get<StringMatrix>(cell.data)));
                                            else if (hasComplex)
                                                rowResult = Value(std::get<ComplexMatrix>(rowResult.data)
                                                    .integR(std::get<ComplexMatrix>(cell.data)));
                                            else
                                                rowResult = Value(std::get<RealMatrix>(rowResult.data)
                                                    .integR(std::get<RealMatrix>(cell.data)));
                                        }
                                    }
                                    if (matResult.isNone()) {
                                        matResult = rowResult;
                                    }
                                    else {
                                        if (hasString)
                                            matResult = Value(std::get<StringMatrix>(matResult.data)
                                                .integC(std::get<StringMatrix>(rowResult.data)));
                                        else if (hasComplex)
                                            matResult = Value(std::get<ComplexMatrix>(matResult.data)
                                                .integC(std::get<ComplexMatrix>(rowResult.data)));
                                        else
                                            matResult = Value(std::get<RealMatrix>(matResult.data)
                                                .integC(std::get<RealMatrix>(rowResult.data)));
                                    }
                                }
                                result = matResult;
                            }
                            catch (...) {
                                throw std::runtime_error(
                                    "VM Error: Dimension mismatch during block matrix concatenation.");
                            }
                        }
                        // ═══ 纯标量 → 直接构建矩阵 ═══
                        else if (hasString) {
                            std::vector<std::string> flat(total);
                            for (int ii = 0; ii < total; ++ii) {
                                const Value& v = stack[stack.size() - total + ii];
                                if (std::holds_alternative<std::string>(v.data))
                                    flat[ii] = std::get<std::string>(v.data);
                                else {
                                    std::ostringstream oss; oss << v;
                                    flat[ii] = oss.str();
                                }
                            }
                            result = Value(StringMatrix(rows, cols, flat));
                        }
                        else if (hasComplex) {
                            std::vector<Complex> flat(total);
                            for (int ii = 0; ii < total; ++ii)
                                flat[ii] = stack[stack.size() - total + ii].asComplex();
                            result = Value(ComplexMatrix(rows, cols, flat));
                        }
                        else {
                            std::vector<double> flat(total);
                            for (int ii = 0; ii < total; ++ii)
                                flat[ii] = stack[stack.size() - total + ii].asDouble();
                            result = Value(RealMatrix(rows, cols, flat));
                        }
                    }

                    for (int ii = 0; ii < total; ++ii) pop();
                    push(result);
                    break;
                }

                case OpCode::OP_IN: {
                    Value haystack = pop(), needle = pop();

                    // ── String in String ──
                    if (std::holds_alternative<std::string>(needle.data) &&
                        std::holds_alternative<std::string>(haystack.data)) {
                        bool found = std::get<std::string>(haystack.data).find(
                            std::get<std::string>(needle.data)) != std::string::npos;
                        push(Value(found ? 1.0 : 0.0));
                        break;
                    }
                    if (std::holds_alternative<std::string>(haystack.data)) {
                        throw std::runtime_error(
                            "VM Error: 'in' on string requires a string on the left side.");
                    }

                    // ── RealMatrix ──
                    if (std::holds_alternative<RealMatrix>(haystack.data)) {
                        const auto& m = std::get<RealMatrix>(haystack.data);
                        double target;
                        try { target = needle.asDouble(); }
                        catch (...) { push(Value(0.0)); goto in_done; }
                        for (const auto& v : m.rawData()) {
                            if (Tol::isEq(v, target, 1e4)) { push(Value(1.0)); goto in_done; }
                        }
                        push(Value(0.0));
                        break;
                    }

                    // ── ComplexMatrix ──
                    if (std::holds_alternative<ComplexMatrix>(haystack.data)) {
                        const auto& m = std::get<ComplexMatrix>(haystack.data);
                        Complex target;
                        try { target = needle.asComplex(); }
                        catch (...) { push(Value(0.0)); goto in_done; }
                        for (const auto& v : m.rawData()) {
                            if (v == target) { push(Value(1.0)); goto in_done; }
                        }
                        push(Value(0.0));
                        break;
                    }

                    // ── StringMatrix ──
                    if (std::holds_alternative<StringMatrix>(haystack.data)) {
                        if (!std::holds_alternative<std::string>(needle.data))
                            throw std::runtime_error(
                                "VM Error: 'in' on StringMatrix requires a string needle.");
                        const auto& m = std::get<StringMatrix>(haystack.data);
                        const auto& target = std::get<std::string>(needle.data);
                        for (const auto& v : m.rawData()) {
                            if (v == target) { push(Value(1.0)); goto in_done; }
                        }
                        push(Value(0.0));
                        break;
                    }

                    // ── List ──
                    if (std::holds_alternative<List>(haystack.data)) {
                        const auto& L = std::get<List>(haystack.data);
                        for (const auto& e : L.raw()) {
                            try {
                                Value elem = std::any_cast<Value>(e);
                                if (vmValuesEqual(needle, elem)) {
                                    push(Value(1.0));
                                    goto in_done;
                                }
                            }
                            catch (...) {}
                        }
                        push(Value(0.0));
                        break;
                    }

                    // ── Dict ──
                    if (std::holds_alternative<Dict>(haystack.data)) {
                        std::string key;
                        if (std::holds_alternative<std::string>(needle.data))
                            key = std::get<std::string>(needle.data);
                        else {
                            std::ostringstream oss; oss << needle;
                            key = oss.str();
                        }
                        push(Value(std::get<Dict>(haystack.data).has(key) ? 1.0 : 0.0));
                        break;
                    }

                    // ── Instance Dunder (__contains__) ──
                    if (std::holds_alternative<std::shared_ptr<Instance>>(haystack.data)) {
                        auto method = findDunder(haystack, "__contains__");
                        if (method) {
                            push(Value(isTruthy(callDunder(haystack, "__contains__", { needle })) ? 1.0 : 0.0));
                            break;
                        }
                    }

                    throw std::runtime_error(
                        "VM Error: 'in' requires an array, vector, matrix, string, list, dict, or instance.");

                in_done:
                    break;
                }

                case OpCode::OP_SLICE_SET: {
                    uint8_t dims = readByte();

                    auto readOptionalInt = [this]() -> std::pair<bool, int> {
                        Value v = pop();
                        if (v.isNone()) return { false, 0 };
                        return { true, static_cast<int>(std::round(v.asDouble())) };
                        };

                    auto buildSliceIndices = [](int dimSize, std::pair<bool, int> start,
                        std::pair<bool, int> end,
                        std::pair<bool, int> step) -> std::vector<int> {
                            int sp = step.first ? step.second : 1;
                            if (sp == 0) throw std::runtime_error("VM Error: Slice step cannot be zero.");
                            int st, en;
                            if (sp > 0) {
                                st = start.first ? start.second : 0;
                                en = end.first ? end.second : dimSize;
                            }
                            else {
                                st = start.first ? start.second : dimSize - 1;
                                en = end.first ? end.second : -1;
                            }
                            if (st < 0) st = dimSize + st;
                            if (en < 0 && end.first) en = dimSize + en;
                            if (sp > 0) {
                                st = std::max(0, std::min(dimSize, st));
                                en = std::max(0, std::min(dimSize, en));
                            }
                            else {
                                st = std::max(-1, std::min(dimSize - 1, st));
                                en = std::max(-1, std::min(dimSize - 1, en));
                            }
                            std::vector<int> ids;
                            if (sp > 0) { for (int i = st; i < en; i += sp) ids.push_back(i); }
                            else { for (int i = st; i > en; i += sp) ids.push_back(i); }
                            return ids;
                        };

                    if (dims == 1) {
                        // 栈: [obj, start, end, step, value]
                        Value val = pop();
                        auto step = readOptionalInt();
                        auto end = readOptionalInt();
                        auto start = readOptionalInt();
                        Value& obj = peek(0);

                        // ── RealMatrix 1D 切片赋值 ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            auto& m = std::get<RealMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);

                            // 标量广播
                            if (std::holds_alternative<double>(val.data) ||
                                std::holds_alternative<BigInt>(val.data) ||
                                std::holds_alternative<Fraction>(val.data)) {
                                double v = val.asDouble();
                                if (m.getRows() == 1) {
                                    for (int id : ids) m(0, id) = v;
                                }
                                else {
                                    for (int id : ids) m(id, 0) = v;
                                }
                            }
                            // 数组赋值
                            else if (std::holds_alternative<RealMatrix>(val.data)) {
                                const auto& src = std::get<RealMatrix>(val.data);
                                auto srcFlat = src.rawData();
                                if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()))
                                    throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                                if (m.getRows() == 1) {
                                    for (size_t k = 0; k < ids.size(); ++k) m(0, ids[k]) = srcFlat[k];
                                }
                                else {
                                    for (size_t k = 0; k < ids.size(); ++k) m(ids[k], 0) = srcFlat[k];
                                }
                            }
                            else {
                                throw std::runtime_error("VM Error: Cannot assign this type to slice.");
                            }
                        }
                        // ── List 1D 切片赋值 ──
                        else if (std::holds_alternative<List>(obj.data)) {
                            auto& L = std::get<List>(obj.data);
                            auto ids = buildSliceIndices(static_cast<int>(L.size()), start, end, step);
                            if (std::holds_alternative<List>(val.data)) {
                                const auto& srcL = std::get<List>(val.data);
                                if (srcL.size() != ids.size())
                                    throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                                for (size_t k = 0; k < ids.size(); ++k)
                                    L.set(ids[k], srcL.raw()[k]);
                            }
                            else {
                                // 标量广播到所有位置
                                for (int id : ids)
                                    L.set(id, std::make_any<Value>(val));
                            }
                        }
                        // ── String 切片赋值 ──
                        else if (std::holds_alternative<std::string>(obj.data)) {
                            auto& s = std::get<std::string>(obj.data);
                            auto ids = buildSliceIndices(static_cast<int>(s.size()), start, end, step);
                            if (!std::holds_alternative<std::string>(val.data))
                                throw std::runtime_error("VM Error: String slice assignment requires a string.");
                            const auto& src = std::get<std::string>(val.data);
                            if (static_cast<int>(src.size()) != static_cast<int>(ids.size()))
                                throw std::runtime_error("VM Error: String slice assignment size mismatch.");
                            for (size_t k = 0; k < ids.size(); ++k) s[ids[k]] = src[k];
                        }
                        // ── ComplexMatrix ──
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            auto& m = std::get<ComplexMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);
                            if (std::holds_alternative<ComplexMatrix>(val.data)) {
                                auto srcFlat = std::get<ComplexMatrix>(val.data).rawData();
                                if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()))
                                    throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                                if (m.getRows() == 1) {
                                    for (size_t k = 0; k < ids.size(); ++k) m(0, ids[k]) = srcFlat[k];
                                }
                                else {
                                    for (size_t k = 0; k < ids.size(); ++k) m(ids[k], 0) = srcFlat[k];
                                }
                            }
                            else {
                                Complex cv = val.asComplex();
                                if (m.getRows() == 1) {
                                    for (int id : ids) m(0, id) = cv;
                                }
                                else {
                                    for (int id : ids) m(id, 0) = cv;
                                }
                            }
                        }
                        // ── StringMatrix ──
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            auto& m = std::get<StringMatrix>(obj.data);
                            int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                            auto ids = buildSliceIndices(n, start, end, step);
                            if (std::holds_alternative<StringMatrix>(val.data)) {
                                auto srcFlat = std::get<StringMatrix>(val.data).rawData();
                                if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()))
                                    throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                                if (m.getRows() == 1) {
                                    for (size_t k = 0; k < ids.size(); ++k) m(0, ids[k]) = srcFlat[k];
                                }
                                else {
                                    for (size_t k = 0; k < ids.size(); ++k) m(ids[k], 0) = srcFlat[k];
                                }
                            }
                            else {
                                std::string sv;
                                if (std::holds_alternative<std::string>(val.data)) sv = std::get<std::string>(val.data);
                                else { std::ostringstream oss; oss << val; sv = oss.str(); }
                                if (m.getRows() == 1) {
                                    for (int id : ids) m(0, id) = sv;
                                }
                                else {
                                    for (int id : ids) m(id, 0) = sv;
                                }
                            }
                        }
                        else {
                            throw std::runtime_error("VM Error: Cannot slice-assign this type.");
                        }
                    }
                    else if (dims == 2) {
                        // 栈: [obj, rStart, rEnd, rStep, cStart, cEnd, cStep, value]
                        Value val = pop();
                        auto cStep = readOptionalInt();
                        auto cEnd = readOptionalInt();
                        auto cStart = readOptionalInt();
                        auto rStep = readOptionalInt();
                        auto rEnd = readOptionalInt();
                        auto rStart = readOptionalInt();
                        Value& obj = peek(0);

                        auto processMatSliceSet = [&](auto& m) {
                            auto rIds = buildSliceIndices(m.getRows(), rStart, rEnd, rStep);
                            auto cIds = buildSliceIndices(m.getCols(), cStart, cEnd, cStep);
                            int dstR = static_cast<int>(rIds.size());
                            int dstC = static_cast<int>(cIds.size());

                            using MatType = std::decay_t<decltype(m)>;
                            using ElemType = std::decay_t<decltype(m(0, 0))>;

                            // 标量广播
                            bool isScalar = false;
                            ElemType scalarVal{};
                            if constexpr (std::is_same_v<ElemType, double>) {
                                if (std::holds_alternative<double>(val.data) ||
                                    std::holds_alternative<BigInt>(val.data) ||
                                    std::holds_alternative<Fraction>(val.data)) {
                                    isScalar = true;
                                    scalarVal = val.asDouble();
                                }
                            }
                            else if constexpr (std::is_same_v<ElemType, Complex>) {
                                try { scalarVal = val.asComplex(); isScalar = true; }
                                catch (...) {}
                                if (std::holds_alternative<ComplexMatrix>(val.data)) isScalar = false;
                            }
                            else if constexpr (std::is_same_v<ElemType, std::string>) {
                                if (std::holds_alternative<std::string>(val.data)) {
                                    isScalar = true;
                                    scalarVal = std::get<std::string>(val.data);
                                }
                            }

                            if (isScalar) {
                                for (int ri : rIds)
                                    for (int ci : cIds)
                                        m(ri, ci) = scalarVal;
                                return;
                            }

                            // 矩阵块注入
                            if (std::holds_alternative<MatType>(val.data)) {
                                const auto& src = std::get<MatType>(val.data);
                                if (src.getRows() != dstR || src.getCols() != dstC)
                                    throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                                for (int i = 0; i < dstR; ++i)
                                    for (int j = 0; j < dstC; ++j)
                                        m(rIds[i], cIds[j]) = src(i, j);
                                return;
                            }

                            throw std::runtime_error("VM Error: Type mismatch in matrix slice assignment.");
                            };

                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            processMatSliceSet(std::get<RealMatrix>(obj.data));
                        }
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            processMatSliceSet(std::get<ComplexMatrix>(obj.data));
                        }
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            processMatSliceSet(std::get<StringMatrix>(obj.data));
                        }
                        else {
                            throw std::runtime_error("VM Error: 2D slice assignment requires a matrix.");
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Unsupported slice assignment dimensionality.");
                    }
                    break;
                }

                                            // ── ★ 索引读取 ──
                case OpCode::OP_INDEX_GET: {
                    uint8_t dims = readByte();
                    if (dims == 1) {
                        Value idx = pop();
                        Value obj = pop();

                        // ── Dict：key 是字符串 ──
                        if (std::holds_alternative<Dict>(obj.data)) {
                            std::string key;
                            if (std::holds_alternative<std::string>(idx.data))
                                key = std::get<std::string>(idx.data);
                            else {
                                std::ostringstream oss; oss << idx;
                                key = oss.str();
                            }
                            const auto* entry = std::get<Dict>(obj.data).get(key);
                            if (!entry) throw std::runtime_error("VM Error: Key '" + key + "' not found.");
                            push(std::any_cast<Value>(*entry));
                            break;
                        }

                        int i = static_cast<int>(std::round(idx.asDouble()));

                        // ── RealMatrix ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            const auto& m = std::get<RealMatrix>(obj.data);
                            if (m.getRows() == 1) {
                                if (i < 0) i = m.getCols() + i;
                                push(Value(m(0, i)));
                            }
                            else if (m.getCols() == 1) {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m(i, 0)));
                            }
                            else {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m.getRow(i)));
                            }
                        }
                        // ── ComplexMatrix ──
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            const auto& m = std::get<ComplexMatrix>(obj.data);
                            if (m.getRows() == 1) {
                                if (i < 0) i = m.getCols() + i;
                                push(Value(m(0, i)));
                            }
                            else if (m.getCols() == 1) {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m(i, 0)));
                            }
                            else {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m.getRow(i)));
                            }
                        }
                        // ── StringMatrix ──
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            const auto& m = std::get<StringMatrix>(obj.data);
                            if (m.getRows() == 1) {
                                if (i < 0) i = m.getCols() + i;
                                push(Value(m(0, i)));
                            }
                            else if (m.getCols() == 1) {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m(i, 0)));
                            }
                            else {
                                if (i < 0) i = m.getRows() + i;
                                push(Value(m.getRow(i)));
                            }
                        }
                        // ── List ──
                        else if (std::holds_alternative<List>(obj.data)) {
                            push(std::any_cast<Value>(std::get<List>(obj.data).at(i)));
                        }
                        // ── String ──
                        else if (std::holds_alternative<std::string>(obj.data)) {
                            const auto& s = std::get<std::string>(obj.data);
                            if (i < 0) i = static_cast<int>(s.size()) + i;
                            if (i < 0 || i >= static_cast<int>(s.size()))
                                throw std::runtime_error("VM Error: String index out of bounds.");
                            push(Value(std::string(1, s[i])));
                        }
                        // ── Instance (__getitem__) ──
                        else if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
                            auto c = inst->classDef;
                            while (c) {
                                auto it = c->methods.find("__getitem__");
                                if (it != c->methods.end()) {
                                    // 简化：通过全局变量传递 self，调用 __getitem__(idx)
                                    globals["self"] = Value(inst);
                                    auto& fc = it->second;
                                    if (fc->nativeFn.has_value()) {
                                        std::string tag = std::any_cast<std::string>(fc->nativeFn);
                                        if (tag.substr(0, 5) == "__fn:") {
                                            int fnIdx = std::stoi(tag.substr(5));
                                            auto& fn = compiledFunctions[fnIdx];
                                            CallFrame newFrame;
                                            newFrame.function = fn.get();
                                            newFrame.ip = 0;
                                            newFrame.stackBase = static_cast<int>(stack.size());
                                            push(idx); // 参数
                                            frames.push_back(newFrame);
                                            goto indexGetDone;
                                        }
                                    }
                                    break;
                                }
                                c = c->parent;
                            }
                            throw std::runtime_error("VM Error: Cannot index this instance (no __getitem__).");
                        }
                        else {
                            throw std::runtime_error("VM Error: Cannot index this type.");
                        }
                    }
                    else if (dims == 2) {
                        Value col = pop();
                        Value row = pop();
                        Value obj = pop();
                        int r = static_cast<int>(std::round(row.asDouble()));
                        int c = static_cast<int>(std::round(col.asDouble()));

                        // ── RealMatrix ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            const auto& m = std::get<RealMatrix>(obj.data);
                            if (r < 0) r = m.getRows() + r;
                            if (c < 0) c = m.getCols() + c;
                            push(Value(m(r, c)));
                        }
                        // ── ComplexMatrix ──
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            const auto& m = std::get<ComplexMatrix>(obj.data);
                            if (r < 0) r = m.getRows() + r;
                            if (c < 0) c = m.getCols() + c;
                            push(Value(m(r, c)));
                        }
                        // ── StringMatrix ──
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            const auto& m = std::get<StringMatrix>(obj.data);
                            if (r < 0) r = m.getRows() + r;
                            if (c < 0) c = m.getCols() + c;
                            push(Value(m(r, c)));
                        }
                        else {
                            throw std::runtime_error("VM Error: 2D indexing requires a matrix.");
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Unsupported index dimensionality.");
                    }
                indexGetDone:
                    break;
                }

                                         // ── ★ 索引写入 ──
                case OpCode::OP_INDEX_SET: {
                    uint8_t dims = readByte();
                    Value val = pop();

                    if (dims == 1) {
                        Value idx = pop();
                        Value& obj = peek(0);

                        // ── Dict ──
                        if (std::holds_alternative<Dict>(obj.data)) {
                            std::string key;
                            if (std::holds_alternative<std::string>(idx.data))
                                key = std::get<std::string>(idx.data);
                            else {
                                std::ostringstream oss; oss << idx;
                                key = oss.str();
                            }
                            std::get<Dict>(obj.data).set(key, std::make_any<Value>(val));
                            break;
                        }

                        int i = static_cast<int>(std::round(idx.asDouble()));

                        // ── RealMatrix ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            auto& m = std::get<RealMatrix>(obj.data);
                            // 复数值写入时自动升维
                            if (std::holds_alternative<Complex>(val.data)) {
                                ComplexMatrix cm = m.toComplexMatrix();
                                if (m.getRows() == 1) {
                                    if (i < 0) i = cm.getCols() + i;
                                    cm(0, i) = val.asComplex();
                                }
                                else if (m.getCols() == 1) {
                                    if (i < 0) i = cm.getRows() + i;
                                    cm(i, 0) = val.asComplex();
                                }
                                obj = Value(cm);
                            }
                            else {
                                if (m.getRows() == 1) {
                                    if (i < 0) i = m.getCols() + i;
                                    m(0, i) = val.asDouble();
                                }
                                else if (m.getCols() == 1) {
                                    if (i < 0) i = m.getRows() + i;
                                    m(i, 0) = val.asDouble();
                                }
                            }
                        }
                        // ── ComplexMatrix ──
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            auto& m = std::get<ComplexMatrix>(obj.data);
                            if (m.getRows() == 1) {
                                if (i < 0) i = m.getCols() + i;
                                m(0, i) = val.asComplex();
                            }
                            else if (m.getCols() == 1) {
                                if (i < 0) i = m.getRows() + i;
                                m(i, 0) = val.asComplex();
                            }
                        }
                        // ── StringMatrix ──
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            auto& m = std::get<StringMatrix>(obj.data);
                            std::string s;
                            if (std::holds_alternative<std::string>(val.data))
                                s = std::get<std::string>(val.data);
                            else {
                                std::ostringstream oss; oss << val;
                                s = oss.str();
                            }
                            if (m.getRows() == 1) {
                                if (i < 0) i = m.getCols() + i;
                                m(0, i) = s;
                            }
                            else if (m.getCols() == 1) {
                                if (i < 0) i = m.getRows() + i;
                                m(i, 0) = s;
                            }
                        }
                        // ── List ──
                        else if (std::holds_alternative<List>(obj.data)) {
                            std::get<List>(obj.data).set(i, std::make_any<Value>(val));
                        }
                        // ── String（单字符替换，构建新字符串）──
                        else if (std::holds_alternative<std::string>(obj.data)) {
                            auto& s = std::get<std::string>(obj.data);
                            if (i < 0) i = static_cast<int>(s.size()) + i;
                            if (i < 0 || i >= static_cast<int>(s.size()))
                                throw std::runtime_error("VM Error: String index out of bounds.");
                            if (!std::holds_alternative<std::string>(val.data) ||
                                std::get<std::string>(val.data).size() != 1)
                                throw std::runtime_error("VM Error: String element assignment requires a single character.");
                            s[i] = std::get<std::string>(val.data)[0];
                        }
                        else {
                            throw std::runtime_error("VM Error: Cannot assign index on this type.");
                        }
                    }
                    else if (dims == 2) {
                        Value col = pop();
                        Value row = pop();
                        Value& obj = peek(0);
                        int r = static_cast<int>(std::round(row.asDouble()));
                        int c = static_cast<int>(std::round(col.asDouble()));

                        // ── RealMatrix ──
                        if (std::holds_alternative<RealMatrix>(obj.data)) {
                            // 复数值写入时自动升维
                            if (std::holds_alternative<Complex>(val.data)) {
                                ComplexMatrix cm = std::get<RealMatrix>(obj.data).toComplexMatrix();
                                if (r < 0) r = cm.getRows() + r;
                                if (c < 0) c = cm.getCols() + c;
                                cm(r, c) = val.asComplex();
                                obj = Value(cm);
                            }
                            else {
                                auto& m = std::get<RealMatrix>(obj.data);
                                if (r < 0) r = m.getRows() + r;
                                if (c < 0) c = m.getCols() + c;
                                m(r, c) = val.asDouble();
                            }
                        }
                        // ── ComplexMatrix ──
                        else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                            auto& m = std::get<ComplexMatrix>(obj.data);
                            if (r < 0) r = m.getRows() + r;
                            if (c < 0) c = m.getCols() + c;
                            m(r, c) = val.asComplex();
                        }
                        // ── StringMatrix ──
                        else if (std::holds_alternative<StringMatrix>(obj.data)) {
                            auto& m = std::get<StringMatrix>(obj.data);
                            if (r < 0) r = m.getRows() + r;
                            if (c < 0) c = m.getCols() + c;
                            if (std::holds_alternative<std::string>(val.data))
                                m(r, c) = std::get<std::string>(val.data);
                            else {
                                std::ostringstream oss; oss << val;
                                m(r, c) = oss.str();
                            }
                        }
                        else {
                            throw std::runtime_error("VM Error: 2D index assignment requires a matrix.");
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Unsupported index dimensionality for assignment.");
                    }
                    break;
                }

                                         // ── ★ 格式化字符串 ──
                case OpCode::OP_FORMAT_STRING: {
                    uint16_t specIdx = readShort();
                    std::string spec = std::get<std::string>(currentChunk().constants[specIdx].data);
                    Value val = pop();

                    char align = '\0';
                    int width = 0;
                    int precision = -1;
                    char type = '\0';
                    size_t si = 0;
                    if (si < spec.size() && (spec[si] == '<' || spec[si] == '>' || spec[si] == '^'))
                        align = spec[si++];
                    while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9')
                        width = width * 10 + (spec[si++] - '0');
                    if (si < spec.size() && spec[si] == '.') {
                        si++; precision = 0;
                        while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9')
                            precision = precision * 10 + (spec[si++] - '0');
                    }
                    if (si < spec.size()) type = spec[si++];

                    std::ostringstream oss;
                    if (type == 'f' || type == 'e') {
                        if (precision >= 0) oss << std::fixed << std::setprecision(precision);
                        if (type == 'e') oss << std::scientific;
                        oss << val.asDouble();
                    }
                    else if (type == 'd') { oss << static_cast<int64_t>(std::round(val.asDouble())); }
                    else if (type == 'x') { oss << std::hex << static_cast<int64_t>(std::round(val.asDouble())); }
                    else { oss << val; }

                    std::string result = oss.str();
                    if (width > 0 && static_cast<int>(result.size()) < width) {
                        int pad = width - static_cast<int>(result.size());
                        if (align == '<') result += std::string(pad, ' ');
                        else if (align == '^') {
                            int l = pad / 2, r = pad - l;
                            result = std::string(l, ' ') + result + std::string(r, ' ');
                        }
                        else result = std::string(pad, ' ') + result;
                    }
                    push(Value(result));
                    break;
                }

                                             // ── ★ List 初始化 ──
                case OpCode::OP_LIST_INIT: {
                    push(Value(List()));
                    break;
                }

                                         // ── ★ List 追加 ──
                case OpCode::OP_LIST_APPEND: {
                    uint16_t depth = readShort();
                    Value elem = pop();
                    // ★ ResultList 在 stack[size - 1 - depth]
                    int listIdx = static_cast<int>(stack.size()) - 1 - static_cast<int>(depth);
                    if (listIdx >= 0 && std::holds_alternative<List>(stack[listIdx].data)) {
                        std::get<List>(stack[listIdx].data).push_back(std::make_any<Value>(elem));
                    }
                    else {
                        throw std::runtime_error("VM Error: LIST_APPEND target not found at depth " +
                            std::to_string(depth));
                    }
                    break;
                }

                                           // ── ★ Import ──
                case OpCode::OP_IMPORT: {
                    Value pathVal = pop();
                    if (!std::holds_alternative<std::string>(pathVal.data))
                        throw std::runtime_error("VM Error: import requires a string path.");
                    std::string name = std::get<std::string>(pathVal.data);

                    // ★ 防重复导入
                    if (importedModules.count(name)) {
                        push(Value::none());
                        break;
                    }

                    // ★ 查找原生模块
                    auto& modules = getNativeModules();
                    auto it = modules.find(name);
                    if (it == modules.end())
                        throw std::runtime_error("VM Error: Module '" + name +
                            "' not found. Only native modules (json, image, prob) "
                            "are supported in VM mode.");

                    // ★ 调用模块加载器，传入 VM 的 globals/builtins/arity
                    it->second.loader(globals, nativeBuiltins, builtinArity);
                    importedModules.insert(name);

                    std::cout << "[System] Native module '" << name
                        << "' loaded." << std::endl;

                    push(Value::none());
                    break;
                }

                                      // ── ★ Class ──
                case OpCode::OP_CLASS: {
                    uint16_t nameIdx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    auto cls = std::make_shared<ClassDefinition>();
                    cls->name = name;
                    push(Value(cls));
                    break;
                }

                                     // ── ★ Method ──
                case OpCode::OP_METHOD: {
                    uint16_t nameIdx = readShort();
                    std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value closureVal = pop();
                    Value& classVal = peek(0);

                    if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(classVal.data))
                        throw std::runtime_error("VM Error: OP_METHOD requires a class on stack.");

                    auto cls = std::get<std::shared_ptr<ClassDefinition>>(classVal.data);

                    // ★ 新格式：OP_CLOSURE 产生 FunctionClosure
                    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(closureVal.data)) {
                        auto fc = std::get<std::shared_ptr<FunctionClosure>>(closureVal.data);
                        cls->methods[methodName] = fc;
                        break;
                    }

                    // ★ 旧格式兼容：裸字符串 "__fn:N"（安全后备）
                    if (std::holds_alternative<std::string>(closureVal.data)) {
                        const std::string& tag = std::get<std::string>(closureVal.data);
                        auto fc = std::make_shared<FunctionClosure>(
                            std::vector<std::string>{},
                            std::vector<bool>{},
                            methodName,
                            nullptr
                        );
                        fc->nativeFn = std::make_any<std::string>(tag);
                        cls->methods[methodName] = fc;
                        break;
                    }

                    throw std::runtime_error("VM Error: Invalid closure type for method '" +
                        methodName + "'.");
                }

                                      // ── ★ Inherit ──
                case OpCode::OP_INHERIT: {
                    Value superClass = pop();
                    Value& subClass = peek(0);
                    if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(superClass.data) ||
                        !std::holds_alternative<std::shared_ptr<ClassDefinition>>(subClass.data))
                        throw std::runtime_error("VM Error: Inheritance requires two classes.");
                    auto sub = std::get<std::shared_ptr<ClassDefinition>>(subClass.data);
                    auto sup = std::get<std::shared_ptr<ClassDefinition>>(superClass.data);
                    sub->parent = sup;
                    // 复制父类方法（可被覆盖）
                    for (auto& [name, method] : sup->methods) {
                        if (sub->methods.find(name) == sub->methods.end())
                            sub->methods[name] = method;
                    }
                    pop(); // pop subClass
                    break;
                }

                                       // ── ★ Get Property ──
                case OpCode::OP_GET_PROPERTY: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value obj = pop();

                    if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);

                        // 1) 查字段
                        auto* fval = inst->fields.get(field);
                        if (fval) {
                            push(std::any_cast<Value>(*fval));
                            break;
                        }

                        // 2) 查方法 → 返回 FunctionClosure（已经是正确格式）
                        auto c = inst->classDef;
                        while (c) {
                            auto it = c->methods.find(field);
                            if (it != c->methods.end()) {
                                push(Value(it->second));  // ★ 直接返回 FunctionClosure
                                break;
                            }
                            c = c->parent;
                        }
                        if (!c) throw std::runtime_error("VM Error: No field/method '" + field + "'.");
                    }
                    else if (std::holds_alternative<Dict>(obj.data)) {
                        const auto& d = std::get<Dict>(obj.data);
                        const auto* v = d.get(field);
                        if (!v) throw std::runtime_error("VM Error: Key '" + field + "' not found.");
                        push(std::any_cast<Value>(*v));
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot access property on this type.");
                    }
                    break;
                }

                                            // ── ★ Set Property ──
                case OpCode::OP_SET_PROPERTY: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value val = pop();
                    Value obj = pop();

                    if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
                        inst->fields.set(field, std::make_any<Value>(val));
                        push(Value(inst));  // ★ 推送容器（同一 shared_ptr，写回无害）
                    }
                    else if (std::holds_alternative<Dict>(obj.data)) {
                        std::get<Dict>(obj.data).set(field, std::make_any<Value>(val));
                        push(obj);          // ★ 推送修改后的 Dict（值语义，需要写回）
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot set property on this type.");
                    }
                    break;
                }

                                            // ── ★ Method Invoke ──
                case OpCode::OP_INVOKE: {
                    uint16_t nameIdx = readShort();
                    uint8_t argc = readByte();
                    std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);

                    Value obj = stack[stack.size() - 1 - argc];

                    if (!std::holds_alternative<std::shared_ptr<Instance>>(obj.data))
                        throw std::runtime_error("VM Error: Cannot invoke method on non-instance.");

                    auto inst = std::get<std::shared_ptr<Instance>>(obj.data);

                    // 沿继承链查找方法，记录方法所属类
                    std::shared_ptr<FunctionClosure> method;
                    std::shared_ptr<ClassDefinition> owningClass;
                    auto c = inst->classDef;
                    while (c) {
                        auto it = c->methods.find(methodName);
                        if (it != c->methods.end()) {
                            method = it->second;
                            owningClass = c;
                            break;
                        }
                        c = c->parent;
                    }
                    if (!method)
                        throw std::runtime_error("VM Error: No method '" + methodName +
                            "' on '" + inst->classDef->name + "'.");

                    globals["self"] = Value(inst);
                    globals["__class__"] = Value(owningClass);  // ★ 记录方法所属类

                    if (method->nativeFn.has_value() &&
                        method->nativeFn.type() == typeid(NativeCallable)) {
                        std::vector<Value> args(argc);
                        for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                        pop(); // obj
                        auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
                        push(fn(args));
                        break;
                    }

                    throw std::runtime_error("VM Error: Method '" + methodName +
                        "' has no callable implementation.");
                }

                case OpCode::OP_PRINT: {
                    std::cout << pop() << std::endl;
                    push(Value::none());
                    break;
                }

                default:
                    throw std::runtime_error("VM Error: Unknown opcode " +
                        std::to_string(static_cast<int>(op)));
                }

            }
            catch (const ErrorSignal& sig) {
                if (!exceptionHandlers.empty() &&
                    exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1)
                        frames.pop_back();
                    stack.resize(handler.stackSize);
                    push(Value(sig.message));
                    frame().ip = handler.ip;
                    continue;
                }
                throw std::runtime_error(sig.message);
            }
            catch (const std::exception& ex) {
                if (!exceptionHandlers.empty() &&
                    exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1)
                        frames.pop_back();
                    stack.resize(handler.stackSize);
                    push(Value(std::string(ex.what())));
                    frame().ip = handler.ip;
                    continue;
                }
                throw;  // ★ 传播到外层 run()
            }
            catch (...) {
                if (!exceptionHandlers.empty() &&
                    exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1)
                        frames.pop_back();
                    stack.resize(handler.stackSize);
                    push(Value(std::string("Unknown error")));
                    frame().ip = handler.ip;
                    continue;
                }
                throw std::runtime_error("Unknown error");
            }
        }
    }

} // namespace jc
