// VM.cpp
#include "VM.h"
#include "Module.h"
#include "BuiltinRegistry.h"
#include "Highlight.h"
#include "GcHeap.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <filesystem>

namespace jc {

    auto formatEscape = [](int errLine, const std::string& rawMsg, const std::string& sourceFile) {
        std::string m = rawMsg;
        if (errLine > 0 && m.find("[") != 0) {
            std::string fn = "Script";
            try { fn = std::filesystem::path(sourceFile).filename().string(); }
            catch (...) {}
            if (fn.empty()) fn = "Script";
            m = "[" + fn + " : " + std::to_string(errLine) + "] " + m;
        }
        return m;
        };

    int VM::currentLine() {
        if (frames.empty()) return 0;

        int errorIp = frame().ip - 1;
        if (errorIp < 0) errorIp = 0;

        const auto& lines = currentChunk().lines;
        for (int i = std::min(errorIp, static_cast<int>(lines.size()) - 1); i >= 0; --i) {
            if (lines[i] > 0) return lines[i];
        }
        return 0;
    }

    static bool vmValuesEqual(const Value& lhs, const Value& rhs) {
        static thread_local std::vector<std::pair<const void*, const void*>> comparingPairs;
        if (lhs.data.index() == rhs.data.index()) {
            if (std::holds_alternative<std::monostate>(lhs.data))
                return true;
            if (std::holds_alternative<double>(lhs.data))
                return Tol::isEq(std::get<double>(lhs.data), std::get<double>(rhs.data));
            if (std::holds_alternative<BigInt>(lhs.data))
                return std::get<BigInt>(lhs.data) == std::get<BigInt>(rhs.data);
            if (std::holds_alternative<Complex>(lhs.data))
                return std::get<Complex>(lhs.data) == std::get<Complex>(rhs.data);
            if (std::holds_alternative<Fraction>(lhs.data))
                return std::get<Fraction>(lhs.data) == std::get<Fraction>(rhs.data);
            if (std::holds_alternative<std::string>(lhs.data))
                return std::get<std::string>(lhs.data) == std::get<std::string>(rhs.data);
            if (std::holds_alternative<BaseNum>(lhs.data))
                return std::get<BaseNum>(lhs.data).getValue() ==
                std::get<BaseNum>(rhs.data).getValue();
            if (std::holds_alternative<RealMatrix>(lhs.data)) {
                const auto& a = std::get<RealMatrix>(lhs.data);
                const auto& b = std::get<RealMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!Tol::isEq(a(i, j), b(i, j))) return false;
                return true;
            }
            if (std::holds_alternative<ComplexMatrix>(lhs.data)) {
                const auto& a = std::get<ComplexMatrix>(lhs.data);
                const auto& b = std::get<ComplexMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (!(a(i, j) == b(i, j))) return false;
                return true;
            }
            if (std::holds_alternative<List>(lhs.data)) {
                const auto& a = std::get<List>(lhs.data);
                const auto& b = std::get<List>(rhs.data);

                if (a.id() == b.id()) return true;
                if (a.size() != b.size()) return false;
                auto pair = a.id() < b.id() ? std::make_pair(a.id(), b.id()) : std::make_pair(b.id(), a.id());
                if (std::find(comparingPairs.begin(), comparingPairs.end(), pair) != comparingPairs.end()) {
                    return true; 
                }
                comparingPairs.push_back(pair);
                bool eq = true;
                for (size_t i = 0; i < a.size(); ++i) {
                    try {
                        Value va = std::any_cast<Value>(a.raw()[i]);
                        Value vb = std::any_cast<Value>(b.raw()[i]);
                        if (!vmValuesEqual(va, vb)) { eq = false; break; }
                    }
                    catch (...) { eq = false; break; }
                }
                comparingPairs.pop_back(); 
                return eq;
            }
            if (std::holds_alternative<Dict>(lhs.data)) {
                const auto& a = std::get<Dict>(lhs.data);
                const auto& b = std::get<Dict>(rhs.data);
                if (a.id() == b.id()) return true;
                if (a.size() != b.size()) return false;
                auto pair = a.id() < b.id() ? std::make_pair(a.id(), b.id()) : std::make_pair(b.id(), a.id());
                if (std::find(comparingPairs.begin(), comparingPairs.end(), pair) != comparingPairs.end()) {
                    return true; 
                }
                comparingPairs.push_back(pair);
                bool eq = true;
                for (const auto& [key, val] : a.getEntries()) {
                    const auto* bval = b.get(key);
                    if (!bval) { eq = false; break; }
                    try {
                        Value va = std::any_cast<Value>(val);
                        Value vb = std::any_cast<Value>(*bval);
                        if (!vmValuesEqual(va, vb)) { eq = false; break; }
                    }
                    catch (...) { eq = false; break; }
                }
                comparingPairs.pop_back();
                return eq;
            }
            if (std::holds_alternative<Set>(lhs.data)) {
                const auto& a = std::get<Set>(lhs.data);
                const auto& b = std::get<Set>(rhs.data);
                if (a.id() == b.id()) return true;
                if (a.size() != b.size()) return false;
                for (const auto& [key, val] : a.raw()) {
                    if (!b.contains(key)) return false;
                }
                return true;
            }
            if (std::holds_alternative<StringMatrix>(lhs.data)) {
                const auto& a = std::get<StringMatrix>(lhs.data);
                const auto& b = std::get<StringMatrix>(rhs.data);
                if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                for (int i = 0; i < a.getRows(); ++i)
                    for (int j = 0; j < a.getCols(); ++j)
                        if (a(i, j) != b(i, j)) return false;
                return true;
            }
            if (std::holds_alternative<std::shared_ptr<Instance>>(lhs.data))
                return std::get<std::shared_ptr<Instance>>(lhs.data).get() ==
                std::get<std::shared_ptr<Instance>>(rhs.data).get();
            return false;
        }

        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data))
            return Fraction(std::get<BigInt>(lhs.data)) == std::get<Fraction>(rhs.data);
        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))
            return std::get<Fraction>(lhs.data) == Fraction(std::get<BigInt>(rhs.data));

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

        if (std::holds_alternative<std::monostate>(lhs.data) ||
            std::holds_alternative<std::monostate>(rhs.data))
            return false;

        try { return lhs.asComplex() == rhs.asComplex(); }
        catch (...) { return false; }
    }

    std::string VM::getTypeName(const Value& val) {
        if (std::holds_alternative<std::monostate>(val.data)) return "none";
        if (std::holds_alternative<double>(val.data)) return "double";
        if (std::holds_alternative<BigInt>(val.data)) return "BigInt";
        if (std::holds_alternative<Fraction>(val.data)) return "Fraction";
        if (std::holds_alternative<Complex>(val.data)) return "Complex";
        if (std::holds_alternative<std::string>(val.data)) return "string";
        if (std::holds_alternative<List>(val.data)) return "list";
        if (std::holds_alternative<Dict>(val.data)) return "dict";
        if (std::holds_alternative<Set>(val.data)) return "set";
        if (std::holds_alternative<RealMatrix>(val.data)) return "RealMatrix";
        if (std::holds_alternative<ComplexMatrix>(val.data)) return "ComplexMatrix";
        if (std::holds_alternative<StringMatrix>(val.data)) return "StringMatrix";
        if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(val.data)) return "func";
        if (std::holds_alternative<std::shared_ptr<Instance>>(val.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(val.data);
            return inst->classDef ? inst->classDef->name : "instance";
        }
        if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(val.data)) return "class";
        return "unknown";
    }

    void VM::triggerParamTypeError(const Value& val, uint16_t typeIdx, uint16_t nameIdx) {
        const std::string& expectedType = std::get<std::string>(currentChunk().constants[typeIdx].data);
        const std::string& paramName = std::get<std::string>(currentChunk().constants[nameIdx].data);
        throw std::runtime_error("TypeError: Parameter '" + paramName +
            "' expected type '" + expectedType +
            "', got '" + getTypeName(val) + "'.");
    }
    void VM::triggerReturnTypeError(const Value& val, uint16_t typeIdx) {
        const std::string& expectedType = std::get<std::string>(currentChunk().constants[typeIdx].data);
        throw std::runtime_error("TypeError: Function '" + frame().function->name +
            "' expected to return '" + expectedType +
            "', but returned '" + getTypeName(val) + "'.");
    }

    bool VM::checkValueType(const Value& val, const std::string& typeStr) {
        if (typeStr == "any" || typeStr.empty()) return true;

        if (typeStr == "int") {
            if (std::holds_alternative<BigInt>(val.data)) return true;
            if (std::holds_alternative<double>(val.data)) {
                double d = std::get<double>(val.data);
                return std::round(d) == d;
            }
            return false;
        }
        if (typeStr == "double" || typeStr == "float" || typeStr == "real" || typeStr == "number")
            return std::holds_alternative<double>(val.data);
        if (typeStr == "string") return std::holds_alternative<std::string>(val.data);
        if (typeStr == "matrix") return std::holds_alternative<RealMatrix>(val.data) || std::holds_alternative<ComplexMatrix>(val.data) || std::holds_alternative<StringMatrix>(val.data);
        if (typeStr == "list") return std::holds_alternative<List>(val.data);
        if (typeStr == "dict") return std::holds_alternative<Dict>(val.data);
        if (typeStr == "set") return std::holds_alternative<Set>(val.data);
        if (typeStr == "bool") {
            if (std::holds_alternative<double>(val.data)) {
                double d = std::get<double>(val.data);
                return d == 0.0 || d == 1.0;
            }
            return false;
        }
        if (typeStr == "func" || typeStr == "function")
            return std::holds_alternative<std::shared_ptr<FunctionClosure>>(val.data) ||
            std::holds_alternative<std::string>(val.data);
        if (typeStr == "complex") return std::holds_alternative<Complex>(val.data);
        if (typeStr == "fraction") return std::holds_alternative<Fraction>(val.data);
        if (typeStr == "class") return std::holds_alternative<std::shared_ptr<ClassDefinition>>(val.data);


        if (std::holds_alternative<std::shared_ptr<Instance>>(val.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(val.data);
            auto c = inst->classDef;
            while (c) {
                if (c->name == typeStr) return true;
                c = c->parent;
            }
        }
        return false;
    }

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

    Value VM::callDunder(const Value& obj, const std::string& name,
        const std::vector<Value>& args)
    {
        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
        auto method = findDunder(obj, name);
        if (!method) throw std::runtime_error("VM Error: No callable dunder '" + name + "'.");

        if (method->isNative() && !method->isBytecode()) {
            helpers::nativeSelfStack.push_back(Value(inst));
            helpers::nativeClassStack.push_back(Value(inst->classDef));
            Value result;
            try {
                auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
                result = fn(args);
            }
            catch (...) {
                helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                throw;
            }
            helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
            return result;
        }
        else if (method->isBytecode()) {
            std::shared_ptr<std::vector<Value>> captures = nullptr;
            if (method->hasCaptures())
                captures = std::any_cast<std::shared_ptr<std::vector<Value>>>(method->capturedEnv);
            // ★ 无污染传参：直接送入 VM CallFrame 的 boundSelf
            return callVMFunction(method->compiledFnIndex, args, captures, Value(inst), Value(inst->classDef));
        }
        else {
            throw std::runtime_error("VM Error: No callable dunder '" + name + "'.");
        }
    }

    VM::VM() {
        activeVM = this;

        // ★ 核心重定向器：C++ 层索要 "self" 时，直接打劫当前虚拟机的寄存器！
        helpers::getGlobalCallback = [this](const std::string& name) -> Value {
            // 1. 优先满足正在运行的 C++ 原生方法栈 (如 isArray 等内置方法内部调用)
            if (name == "self" && !helpers::nativeSelfStack.empty()) return helpers::nativeSelfStack.back();
            if (name == "__class__" && !helpers::nativeClassStack.empty()) return helpers::nativeClassStack.back();

            // 2. 然后满足 VM 字节码的 CallFrame 寄存器
            if (name == "self") {
                if (frames.empty() || frames.back().selfContext.isNone()) return Value::none();
                return frames.back().selfContext;
            }
            if (name == "__class__") {
                if (frames.empty() || frames.back().classContext.isNone()) return Value::none();
                return frames.back().classContext;
            }
            // 3. 最后才是普通的全局变量
            auto it = globals.find(name);
            return it != globals.end() ? it->second : Value::none();
            };

        globals["PI"] = Value(3.14159265358979323846);
        globals["E"] = Value(2.71828182845904523536);
        globals["i"] = Value(Complex(0.0, 1.0));
        globals["I"] = Value(Complex(0.0, 1.0));
        globals["true"] = Value(1.0);
        globals["false"] = Value(0.0);
        globals["none"] = Value::none();

        registerBuiltin("__vm_delete__", [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1 || !std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("VM Error: __vm_delete__ expects a string variable name.");
            const std::string& name = std::get<std::string>(args[0].data);

            // ★ 允许删除所有变量（包括 const 和系统常量）
            // 用户可通过 resetConst() 或 pi() 等函数恢复
            auto it = globals.find(name);
            if (it == globals.end())
                throw std::runtime_error("Runtime Error: Undefined variable '" + name + "'.");
            globals.erase(it);
            constGlobals.erase(name);  // 同步清除 const 标记
            return Value::none();
            }, { 1 });
    }

    void VM::registerBuiltin(const std::string& name, NativeCallable fn, std::set<int> arity) {
        nativeBuiltins[name] = std::move(fn);
        builtinArity[name] = std::move(arity);
    }

    void VM::setGlobal(const std::string& name, const Value& val) {
        globals[name] = val;
    }

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

    Value VM::execute(const Chunk& c) {
        activeVM = this;
        auto mainFn = std::make_shared<CompiledFunction>();
        mainFn->name = "<script>";
        mainFn->chunk = c;
        stack.clear();
        frames.clear();
        exceptionHandlers.clear();
        CallFrame mainFrame;
        mainFrame.function = mainFn.get();
        mainFrame.ip = 0;
        mainFrame.stackBase = 0;
        frames.push_back(mainFrame);
        return run(0);
    }

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
            std::string sfile = frames.empty() ? "" : frame().function->sourceFile;
            throw std::runtime_error(formatEscape(currentLine(), msg, sfile));
        }
    }

    Value VM::callVMFunction(int fnIdx, const std::vector<Value>& args,
        std::shared_ptr<std::vector<Value>> upvalues,
        Value boundSelf, Value boundClass) {
        if (fnIdx < 0 || fnIdx >= static_cast<int>(compiledFunctions.size()))
            throw std::runtime_error("VM Error: Invalid function index in callback.");
        auto& fn = compiledFunctions[fnIdx];
        int savedTargetFrameDepth = currentTargetFrameDepth;
        auto savedRefWritebacks = pendingRefWritebacks;
        pendingRefWritebacks.clear();
        int originalStackSize = static_cast<int>(stack.size());
        int originalFramesSize = static_cast<int>(frames.size());

        for (const auto& arg : args)
            push(arg);

        uint8_t argc = static_cast<uint8_t>(args.size());
        if (fn->hasRestParam) {
            int fixedMax = fn->maxArity - 1;
            if (static_cast<int>(argc) < fn->arity) {
                throw std::runtime_error("VM Error: '" + fn->name + "' requires at least " + std::to_string(fn->arity) + " arguments.");
            }

            List restList;
            if (static_cast<int>(argc) > fixedMax) {
                int restCount = static_cast<int>(argc) - fixedMax;
                std::vector<Value> tempValues(restCount);
                for (int j = 0; j < restCount; j++) {
                    tempValues[restCount - 1 - j] = pop();
                }
                for (int j = 0; j < restCount; j++) {
                    restList.push_back(std::make_any<Value>(tempValues[j]));
                }
                argc = static_cast<uint8_t>(fixedMax);
            }


            int padCount = fixedMax - static_cast<int>(argc);
            for (int j = 0; j < padCount; ++j) push(Value::none());
            push(Value(restList));
            argc = static_cast<uint8_t>(fn->maxArity);
        }
        else {
            if (static_cast<int>(argc) < fn->arity || static_cast<int>(argc) > fn->maxArity) {
                throw std::runtime_error("VM Error: '" + fn->name + "' expects " + std::to_string(fn->arity) + " to " + std::to_string(fn->maxArity) + " arguments, got " + std::to_string(argc) + ".");
            }
            int padCount = fn->maxArity - static_cast<int>(argc);
            for (int j = 0; j < padCount; ++j) push(Value::none());
            argc = static_cast<uint8_t>(fn->maxArity);
        }

        int reserveCount = fn->localCount - fn->maxArity;
        for (int j = 0; j < reserveCount; ++j) push(Value::none());

        CallFrame newFrame;
        newFrame.function = fn.get();
        newFrame.ip = 0;
        newFrame.stackBase = static_cast<int>(stack.size()) - fn->localCount;
        newFrame.upvalues = upvalues;
        // ★ 清爽下发！寄存器已就位：
        newFrame.selfContext = boundSelf;
        newFrame.classContext = boundClass;
        frames.push_back(newFrame);

        int boundary = static_cast<int>(frames.size()) - 1;

        Value result;
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
        if (profileMode) {
            start_time = std::chrono::high_resolution_clock::now();
        }
        try {
            result = run(boundary);
        }
        catch (...) {
            currentTargetFrameDepth = savedTargetFrameDepth;
            pendingRefWritebacks = savedRefWritebacks;
            frames.resize(originalFramesSize);
            stack.resize(originalStackSize);
            throw;
        }
        if (profileMode) {
            auto end_time = std::chrono::high_resolution_clock::now();
            double duration = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            auto& prof = funcProfiles[fn->name];
            prof.callCount++;
            prof.totalTimeMs += duration;
        }

        auto myRefWritebacks = pendingRefWritebacks;
        currentTargetFrameDepth = savedTargetFrameDepth;
        pendingRefWritebacks = savedRefWritebacks;

        if (!myRefWritebacks.empty()) {
            pendingRefWritebacks = myRefWritebacks;
        }
        return result;
    }

    Value VM::run(int targetFrameDepth) {
        currentTargetFrameDepth = targetFrameDepth;

		

        while (true) {
            
            // ═══ GC 自动触发探针 ═══
            if (++gcInstructionCounter_ >= 2048) {
                gcInstructionCounter_ = 0;
                if (GcHeap::get().shouldCollect()) {
                    collectGarbage();
                }
            }

            // =======================================================
            // ★ 调试器拦截探针 (Debugger Interceptor)
            // =======================================================
            if (debugMode) {
                int currentL = currentLine();
                if (currentL > 0) {
                    bool hitBreak = breakpoints.count(currentL) && currentL != lastDebugLine;
                    bool hitStep = stepNextLine && currentL != lastDebugLine;
                    if (hitBreak || hitStep) {
                        lastDebugLine = currentL;
                        stepNextLine = false;
                        debugPrompt();  // 挂起虚拟机，进入时间停止的四次元空间！
                    }
                }
            }

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
			// =======================================================
			// ★ Profiler 探针: 记录微观指令 (Instruction Tick)
			// =======================================================
			if (profileMode) {
				opCounts[op]++;
			}
            try {

                switch (op) {

                case OpCode::OP_CONSTANT: {
                    uint16_t idx = readShort();
                    push(currentChunk().constants[idx]);
                    break;
                }
                case OpCode::OP_NONE:  push(Value::none()); break;
                case OpCode::OP_TRUE:  push(Value(1.0)); break;
                case OpCode::OP_FALSE: push(Value(0.0)); break;
                case OpCode::OP_POP:   pop(); break;

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

                case OpCode::OP_BIT_AND: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__bitand__");
                    if (d) { push(callDunder(a, "__bitand__", { b })); break; }
                    push(a & b);
                    break;
                }
                case OpCode::OP_BIT_OR: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__bitor__");
                    if (d) { push(callDunder(a, "__bitor__", { b })); break; }
                    push(a | b);
                    break;
                }

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
                    else {
                        // ★ 同步 Evaluator 的智能容差系统
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value((da < db && !Tol::isEq(da, db)) ? 1.0 : 0.0));
                    }
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
                    else {
                        // ★ 同步 Evaluator 的智能容差系统
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value((da < db || Tol::isEq(da, db)) ? 1.0 : 0.0));
                    }
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
                    else {
                        // ★ 同步 Evaluator 的智能容差系统
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value((da > db && !Tol::isEq(da, db)) ? 1.0 : 0.0));
                    }
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
                    else {
                        // ★ 同步 Evaluator 的智能容差系统
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value((da > db || Tol::isEq(da, db)) ? 1.0 : 0.0));
                    }
                    break;
                }

                case OpCode::OP_ASSERT_PARAM_TYPE: {
                    uint16_t typeIdx = readShort();
                    uint16_t nameIdx = readShort();
                    Value val = pop();
                    execAssertParamType(val, typeIdx, nameIdx);
                    break;
                }

                case OpCode::OP_ASSERT_RETURN_TYPE: {
                    uint16_t typeIdx = readShort();
                    execAssertReturnType(peek(0), typeIdx);
                    break;
                }

                case OpCode::OP_GET_GLOBAL: {
                    uint16_t idx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[idx].data);

                    // ★ 虚拟机级别拦截：遇到 'self'，直接去它该在的物理寄存器里拿！
                    if (name == "self") {
                        if (frame().selfContext.isNone()) throw std::runtime_error("VM Error: 'self' accessed outside of context.");
                        push(frame().selfContext); break;
                    }
                    if (name == "__class__") {
                        if (frame().classContext.isNone()) throw std::runtime_error("VM Error: '__class__' accessed outside of context.");
                        push(frame().classContext); break;
                    }
                    auto it = globals.find(name);
                    if (it != globals.end()) {
                        push(it->second);
                    }
                    else if (nativeBuiltins.count(name)) {
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

                    // ★ 关键字保护：绝不许改写 self !
                    if (name == "self" || name == "__class__")
                        throw std::runtime_error("Syntax Error: cannot override context keyword '" + name + "'.");

                    if (constGlobals.count(name))
                        throw std::runtime_error("Runtime Error: Cannot modify const variable '" + name + "'.");

                    // ★ 检查是否与内建函数 arity 冲突
                    Value& val = peek(0);
                    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(val.data)) {
                        auto nit = nativeBuiltins.find(name);
                        if (nit != nativeBuiltins.end()) {
                            auto ait = builtinArity.find(name);
                            auto closure = std::get<std::shared_ptr<FunctionClosure>>(val.data);

                            if (ait == builtinArity.end() || ait->second.empty()) {
                                // 原生函数接受任意参数数量 → 完全禁止同名函数
                                throw std::runtime_error(
                                    "Runtime Error: Cannot redefine '" + name +
                                    "' — it is a variadic built-in function.");
                            }

                            // 检查用户函数的每个可接受参数数量是否与原生冲突
                            for (int a = closure->minArgs(); a <= closure->maxArgs(); ++a) {
                                if (ait->second.count(a)) {
                                    throw std::runtime_error(
                                        "Runtime Error: Cannot redefine '" + name + "' with " +
                                        std::to_string(a) + " parameter(s) — conflicts with built-in function. "
                                        "Use a different parameter count to create an overload.");
                                }
                            }
                        }
                    }

                    globals[name] = peek(0);
                    break;
                }
                case OpCode::OP_DEFINE_GLOBAL: {
                    uint16_t idx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[idx].data);
                    globals[name] = peek(0);
                    constGlobals.insert(name);
                    break;
                }

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

                case OpCode::OP_CLOSURE: {
                    uint16_t fnConstIdx = readShort();
                    int idx = static_cast<int>(std::round(
                        currentChunk().constants[fnConstIdx].asDouble()));
                    if (idx < 0 || idx >= static_cast<int>(compiledFunctions.size()))
                        throw std::runtime_error("VM Error: Invalid function index.");

                    auto& fn = compiledFunctions[idx];

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

                    auto closure = std::make_shared<FunctionClosure>(
                        std::vector<std::string>{},
                        std::vector<bool>{},
                        fn->name,
                        nullptr
                    );

                    closure->compiledFnIndex = idx;
                    if (captures) {
                        for (size_t i = 0; i < fn->upvalues.size(); ++i) {
                            if (fn->upvalues[i].name == fn->name) {
                                (*captures)[i] = Value(closure);
                            }
                        }
                        closure->capturedEnv = std::make_any<std::shared_ptr<std::vector<Value>>>(captures);
                    }

                    // ★ （为了保证从外界通过 C++ 获取到这个闭包也能强制执行，我们依然做一层薄薄的回调包装）
                    int capturedFnIdx = idx;
                    auto capturedUpvalues = captures;
                    VM* vm = this;

                    Value currentSelf = frame().selfContext;
                    Value currentClass = frame().classContext;
                    closure->nativeFn = std::make_any<NativeCallable>(
                        [vm, capturedFnIdx, capturedUpvalues, currentSelf, currentClass](const std::vector<Value>& args) -> Value {
                            // ★ 智能窃取：如果有 Dunder 方法等触发的原生调用，优先使用隔离栈里的运行态 Target
                            Value s = !helpers::nativeSelfStack.empty() ? helpers::nativeSelfStack.back() : currentSelf;
                            Value c = !helpers::nativeClassStack.empty() ? helpers::nativeClassStack.back() : currentClass;
                            return vm->callVMFunction(capturedFnIdx, args, capturedUpvalues, s, c);
                        }
                    );

                    for (int j = 0; j < fn->maxArity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                    }
                    closure->defaultValues.resize(fn->maxArity, Value::none());
                    closure->paramNames.clear();
                    closure->isRef.clear();
                    closure->defaultValues.clear();
                    // ========================================================
                    // ★ 修复：在给闭包构建占位数据时，严格划清“必填”、“默认值”和“变长”的界限
                    // ========================================================

                    // 1. 先把基础坑位全部挖好
                    for (int j = 0; j < fn->maxArity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                    }
                    closure->defaultValues.resize(fn->maxArity, Value::none());
                    closure->paramNames.clear();
                    closure->isRef.clear();
                    closure->defaultValues.clear();

                    // 2. 灌入必填参数
                    for (int j = 0; j < fn->arity; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                    }

                    // 3. 灌入真正的带默认值的参数（如果是变长，那最后一项就不是默认参数！）
                    int defaultLimit = fn->hasRestParam ? (fn->maxArity - 1) : fn->maxArity;
                    for (int j = fn->arity; j < defaultLimit; ++j) {
                        closure->paramNames.push_back("_" + std::to_string(j));
                        closure->isRef.push_back(false);
                        closure->defaultValues.push_back(Value::none()); // 真正的默认值占位
                    }

                    // 4. 灌入变长参数标识
                    if (fn->hasRestParam) {
                        closure->paramNames.push_back("...rest");
                        closure->isRef.push_back(false);
                        // 变长参数自身不需要压入 defaultValues 中！这保证了 C++ 反射获取的干净度。
                    }

                    // ★ 必须保留这个标志供 C++ 层 API 重用识别
                    closure->hasRestParam = fn->hasRestParam;
                    closure->boundSelf = frame().selfContext;
                    closure->boundClass = frame().classContext;
                    push(Value(closure));
                    break;
                }

                case OpCode::OP_CALL: {
                    uint8_t argc = readByte();
                    execCall(argc);
                    break;
                }

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
                    execSuperInvoke(nameIdx, argc);
                    break;
                }

                case OpCode::OP_GET_SUPER: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);

                    Value selfVal = pop();

                    if (!std::holds_alternative<std::shared_ptr<Instance>>(selfVal.data))
                        throw std::runtime_error("VM Error: 'super' requires an instance context.");

                    auto inst = std::get<std::shared_ptr<Instance>>(selfVal.data);

                    Value classVal = frame().classContext;
                    if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(classVal.data))
                        throw std::runtime_error("VM Error: 'super' requires class context.");

                    auto currentClass = std::get<std::shared_ptr<ClassDefinition>>(classVal.data);
                    auto parentClass = currentClass->parent;
                    if (!parentClass)
                        throw std::runtime_error("VM Error: No parent class.");

                    std::shared_ptr<FunctionClosure> rawMethod;
                    std::shared_ptr<ClassDefinition> ownerClass;
                    auto c = parentClass;
                    while (c) {
                        auto it = c->methods.find(field);
                        if (it != c->methods.end()) {
                            rawMethod = it->second;
                            ownerClass = c;
                            break;
                        }
                        c = c->parent;
                    }
                    if (!rawMethod)
                        throw std::runtime_error("VM Error: Parent class has no method '" + field + "'.");

                    // ★ FIX: 像 OP_GET_PROPERTY 一样，打包一个携带严格上下文的绑定方法（Bound Method）！
                    auto bound = std::make_shared<FunctionClosure>(
                        std::vector<std::string>{}, std::vector<bool>{},
                        field, nullptr
                    );

                    bound->paramNames = rawMethod->paramNames;
                    bound->isRef = rawMethod->isRef;
                    bound->defaultValues = rawMethod->defaultValues;
                    bound->hasRestParam = rawMethod->hasRestParam;

                    VM* vm = this;
                    auto capturedInst = inst;
                    auto capturedOwner = ownerClass;
                    auto capturedMethod = rawMethod;
                    auto capturedField = field;

                    bound->nativeFn = std::make_any<NativeCallable>(
                        [vm, capturedInst, capturedOwner, capturedMethod, capturedField]
                        (const std::vector<Value>& args) -> Value
                        {
                            Value result;
                            if (capturedMethod->isBytecode()) {
                                std::shared_ptr<std::vector<Value>> captures = nullptr;
                                if (capturedMethod->hasCaptures())
                                    captures = std::any_cast<std::shared_ptr<std::vector<Value>>>(
                                        capturedMethod->capturedEnv);
                                // 安全通道进针
                                result = vm->callVMFunction(
                                    capturedMethod->compiledFnIndex, args, captures, Value(capturedInst), Value(capturedOwner)
                                );
                            }
                            else if (capturedMethod->isNative()) {
                                helpers::nativeSelfStack.push_back(Value(capturedInst));
                                helpers::nativeClassStack.push_back(Value(capturedOwner));
                                try {
                                    auto& fn = std::any_cast<NativeCallable&>(capturedMethod->nativeFn);
                                    result = fn(args);
                                }
                                catch (...) {
                                    helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                                    throw;
                                }
                                helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                            }
                            else {
                                throw std::runtime_error("VM Error: Parent method '" + capturedField + "' has no callable implementation.");
                            }
                            return result;
                        }
                    );

                    push(Value(bound));
                    break;
                }

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

                        bool found = false;
                        Value modifiedVal;
                        for (auto& rw : pendingRefWritebacks) {
                            if (rw.argIndex == static_cast<int>(argIndex)) {
                                modifiedVal = rw.modifiedValue;
                                found = true;
                                break;
                            }
                        }

                        if (!found) continue;

                        switch (sourceType) {
                        case 1: {
                            std::string name = std::get<std::string>(
                                currentChunk().constants[sourceRef].data);
                            globals[name] = modifiedVal;
                            break;
                        }
                        case 2: {
                            int localIdx = frame().stackBase + sourceRef;
                            if (localIdx < static_cast<int>(stack.size()))
                                stack[localIdx] = modifiedVal;
                            break;
                        }
                        case 3: {
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

                case OpCode::OP_RETURN: {
                    bool shouldExit = false;
                    Value result = execReturn(shouldExit);
                    if (shouldExit) return result;
                    break;
                }

                case OpCode::OP_STRINGIFY: {
                    Value v = pop();
                    if (std::holds_alternative<std::string>(v.data)) {
                        push(v);
                    }
                    else {
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

                case OpCode::OP_CONCAT_STRINGS: {
                    uint16_t count = readShort();
                    std::string result;
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
                    execSliceGet(dims);
                    break;
                }

                case OpCode::OP_DESTRUCT: {
                    uint8_t count = readByte();
                    Value& rhs = peek(0);   // ★ 提取它的引用！不要剥夺它的栈地位！

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
                    else if (std::holds_alternative<StringMatrix>(rhs.data)) {
                        for (const auto& s : std::get<StringMatrix>(rhs.data).rawData())
                            elements.push_back(Value(s));
                    }
                    else if (std::holds_alternative<std::string>(rhs.data)) {
                        for (char c : std::get<std::string>(rhs.data))
                            elements.push_back(Value(std::string(1, c)));
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot destructure this type.");
                    }

                    if (static_cast<int>(elements.size()) != count)
                        throw std::runtime_error("VM Error: Destructuring size mismatch.");

                    // ★ 将拆解出的元素依次压在原主体的上面！
                    for (int j = 0; j < count; ++j) {
                        push(elements[j]);
                    }
                    break;
                }

                case OpCode::OP_TRY_BEGIN: {
                    uint16_t catchRelOffset = readShort();
                    uint16_t catchNameIdx = readShort();
                    (void)catchNameIdx;

                    ExceptionHandler handler;
                    handler.frameIndex = static_cast<int>(frames.size()) - 1;
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

                    if (!exceptionHandlers.empty() && exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                        auto handler = exceptionHandlers.back();
                        exceptionHandlers.pop_back();
                        while (static_cast<int>(frames.size()) > handler.frameIndex + 1) frames.pop_back();
                        stack.resize(handler.stackSize);

                        // ★ 原地捕获也顺手清洗，保证极致干净
                        if (msg.find("[Line ") == 0) {
                            size_t c = msg.find("] ");
                            if (c != std::string::npos) msg = msg.substr(c + 2);
                        }
                        push(Value(msg));
                        frame().ip = handler.ip;
                        continue; // 跳转到 catch 块
                    }

                    // ★ 逃出当前 run() 循环：立刻烙印最深处案发第一现场的真实行号！
                    int errLine = currentLine();
                    if (errLine > 0 && msg.find("[Line") == std::string::npos) {
                        msg = "[Line " + std::to_string(errLine) + "] " + msg;
                    }
                    throw std::runtime_error(msg);
                }

                case OpCode::OP_BUILD_DICT: {
                    uint16_t count = readShort();
                    Dict d;
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

                case OpCode::OP_DUP: {
                    push(peek(0));
                    break;
                }

                case OpCode::OP_ITER_INIT: {
                    uint8_t destructFlag = readByte();
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
                    else if (std::holds_alternative<StringMatrix>(iterable.data)) {
                        const auto& m = std::get<StringMatrix>(iterable.data);
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
                    else if (std::holds_alternative<ComplexMatrix>(iterable.data)) {
                        const auto& m = std::get<ComplexMatrix>(iterable.data);
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
                            for (const auto& [key, val] : d.getEntries()) {
                                List pair;
                                pair.push_back(std::make_any<Value>(Value(key)));
                                pair.push_back(std::make_any<Value>(std::any_cast<Value>(val)));
                                elements.push_back(std::make_any<Value>(Value(pair)));
                            }
                        }
                        else {
                            for (const auto& [key, val] : d.getEntries()) {
                                elements.push_back(std::make_any<Value>(Value(key)));
                            }
                        }
                    }
                    else if (std::holds_alternative<Set>(iterable.data)) {
                        const auto& s = std::get<Set>(iterable.data);
                        for (const auto& [key, val] : s.raw()) {
                            elements.push_back(val);  // 只暴露值，不暴露内部 key
                        }
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot iterate over this type.");
                    }
                    push(Value(elements));
                    push(Value(0.0));
                    break;
                }

                case OpCode::OP_ITER_NEXT: {
                    uint16_t offset = readShort();
                    double idx = peek(0).asDouble();
                    const auto& elems = std::get<List>(peek(1).data);
                    int i = static_cast<int>(idx);
                    if (i >= static_cast<int>(elems.size())) {
                        frame().ip += offset;
                    }
                    else {
                        Value elem = std::any_cast<Value>(elems.raw()[i]);
                        stack[stack.size() - 1] = Value(idx + 1);
                        push(elem);
                    }
                    break;
                }

                case OpCode::OP_BUILD_MATRIX: {
                    uint16_t rows = readShort();
                    uint16_t cols = readShort();
                    execBuildMatrix(rows, cols);
                    break;
                }

                case OpCode::OP_IN: {
                    execIn();
                    break;
                }

                case OpCode::OP_SLICE_SET: {
                    uint8_t dims = readByte();
                    execSliceSet(dims);
                    break;
                }

                case OpCode::OP_INDEX_GET: {
                    uint8_t dims = readByte();
                    execIndexGet(dims);
                    break;
                }

                case OpCode::OP_INDEX_SET: {
                    uint8_t dims = readByte();
                    execIndexSet(dims);
                    break;
                }

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

                case OpCode::OP_LIST_INIT: {
                    push(Value(List()));
                    break;
                }

                case OpCode::OP_LIST_APPEND: {
                    uint16_t depth = readShort();
                    Value elem = pop();
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

                case OpCode::OP_IMPORT: {
                    Value pathVal = pop();
                    if (!std::holds_alternative<std::string>(pathVal.data))
                        throw std::runtime_error("VM Error: import requires a string path.");
                    std::string name = std::get<std::string>(pathVal.data);

                    if (importedModules.count(name)) {
                        push(Value::none());
                        break;
                    }

                    auto& modules = getNativeModules();
                    auto it = modules.find(name);
                    if (it != modules.end()) {
                        it->second.loader(globals, nativeBuiltins, builtinArity);
                        importedModules.insert(name);
                        std::cout << "[System] Native module '" << name << "' loaded." << std::endl;
                        push(Value::none());
                        break;
                    }

                    // ★ 非原生模块，委托给 main.cpp 进行强健逐行加载
                    std::string resolved = helpers::safeResolvePath(name);
                    if (!std::filesystem::exists(resolved)) {
                        resolved = helpers::safeResolvePath(name + ".jc2");
                    }

                    if (!std::filesystem::exists(resolved)) {
                        throw std::runtime_error("VM Error: Cannot find module '" + name + "'.");
                    }

                    importedModules.insert(name);

                    if (helpers::runFileCallback) {
                        helpers::runFileCallback(resolved);
                    }

                    push(Value::none());
                    break;
                }

                case OpCode::OP_CLASS: {
                    uint16_t nameIdx = readShort();
                    std::string name = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    auto cls = std::make_shared<ClassDefinition>();
                    cls->name = name;
                    push(Value(cls));
                    break;
                }

                case OpCode::OP_METHOD: {
                    uint16_t nameIdx = readShort();
                    std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value closureVal = pop();
                    Value& classVal = peek(0);

                    if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(classVal.data))
                        throw std::runtime_error("VM Error: OP_METHOD requires a class on stack.");

                    auto cls = std::get<std::shared_ptr<ClassDefinition>>(classVal.data);

                    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(closureVal.data)) {
                        auto fc = std::get<std::shared_ptr<FunctionClosure>>(closureVal.data);
                        cls->methods[methodName] = fc;
                        break;
                    }

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

                case OpCode::OP_INHERIT: {
                    Value superClass = pop();
                    Value& subClass = peek(0);
                    if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(superClass.data) ||
                        !std::holds_alternative<std::shared_ptr<ClassDefinition>>(subClass.data))
                        throw std::runtime_error("VM Error: Inheritance requires two classes.");
                    auto sub = std::get<std::shared_ptr<ClassDefinition>>(subClass.data);
                    auto sup = std::get<std::shared_ptr<ClassDefinition>>(superClass.data);
                    sub->parent = sup;
                    for (auto& [name, method] : sup->methods) {
                        if (sub->methods.find(name) == sub->methods.end())
                            sub->methods[name] = method;
                    }
                    pop();
                    break;
                }

                case OpCode::OP_GET_PROPERTY: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value obj = pop();

                    if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);

                        // 1. 字段查找 — 不变
                        auto* fval = inst->fields.get(field);
                        if (fval) {
                            push(std::any_cast<Value>(*fval));
                            break;
                        }

                        // 2. 方法查找
                        std::shared_ptr<FunctionClosure> rawMethod;
                        std::shared_ptr<ClassDefinition> ownerClass;
                        auto c = inst->classDef;
                        while (c) {
                            auto it = c->methods.find(field);
                            if (it != c->methods.end()) {
                                rawMethod = it->second;
                                ownerClass = c;
                                break;
                            }
                            c = c->parent;
                        }
                        if (!c) throw std::runtime_error("VM Error: No field/method '" + field + "'.");

                        // ★ FIX: 创建绑定方法闭包，携带 receiver 和所属类
                        auto bound = std::make_shared<FunctionClosure>(
                            std::vector<std::string>{}, std::vector<bool>{},
                            field, nullptr
                        );

                        // 复制元数据（参数名、默认值、变长标志）供 execCall 进行参数校验
                        bound->paramNames = rawMethod->paramNames;
                        bound->isRef = rawMethod->isRef;
                        bound->defaultValues = rawMethod->defaultValues;
                        bound->hasRestParam = rawMethod->hasRestParam;

                        VM* vm = this;
                        auto capturedInst = inst;
                        auto capturedOwner = ownerClass;
                        auto capturedMethod = rawMethod;
                        auto capturedField = field;

                        bound->nativeFn = std::make_any<NativeCallable>(
                            [vm, capturedInst, capturedOwner, capturedMethod, capturedField]
                            (const std::vector<Value>& args) -> Value
                            {
                                Value result;
                                if (capturedMethod->isBytecode()) {
                                    std::shared_ptr<std::vector<Value>> captures = nullptr;
                                    if (capturedMethod->hasCaptures())
                                        captures = std::any_cast<std::shared_ptr<std::vector<Value>>>(
                                            capturedMethod->capturedEnv);
                                    // ★ 神迹：直接通过参数通道安全喂入 selfContext!
                                    result = vm->callVMFunction(
                                        capturedMethod->compiledFnIndex, args, captures, Value(capturedInst), Value(capturedOwner)
                                    );
                                }
                                else if (capturedMethod->isNative()) {
                                    // ★ Native 函数也是进入隔离堆栈
                                    helpers::nativeSelfStack.push_back(Value(capturedInst));
                                    helpers::nativeClassStack.push_back(Value(capturedOwner));
                                    try {
                                        auto& fn = std::any_cast<NativeCallable&>(capturedMethod->nativeFn);
                                        result = fn(args);
                                    }
                                    catch (...) {
                                        helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                                        throw;
                                    }
                                    helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                                }
                                else {
                                    throw std::runtime_error("VM Error: Method '" + capturedField + "' has no callable implementation.");
                                }
                                return result;
                            }
                        );

                        push(Value(bound));
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

                case OpCode::OP_SET_PROPERTY: {
                    uint16_t nameIdx = readShort();
                    std::string field = std::get<std::string>(currentChunk().constants[nameIdx].data);
                    Value val = pop();
                    Value obj = pop();

                    if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                        auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
                        inst->fields.set(field, std::make_any<Value>(val));
                        push(Value(inst));
                    }
                    else if (std::holds_alternative<Dict>(obj.data)) {
                        std::get<Dict>(obj.data).set(field, std::make_any<Value>(val));
                        push(obj);
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot set property on this type.");
                    }
                    break;
                }

                case OpCode::OP_INVOKE: {
                    uint16_t nameIdx = readShort();
                    uint8_t argc = readByte();
                    execInvoke(nameIdx, argc);
                    break;
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
            // =======================================================
            // ★ 异常捕获与行号处理区 (VM 内外边界防火墙)
            // =======================================================
            catch (const ErrorSignal& sig) {
                std::string msg = sig.message;
                if (!exceptionHandlers.empty() && exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1) frames.pop_back();
                    stack.resize(handler.stackSize);

                    // 原地捕获也顺手清洗，保证极致干净
                    if (msg.find("[") == 0) {
                        size_t c = msg.find("] ");
                        if (c != std::string::npos) msg = msg.substr(c + 2);
                    }
                    push(Value(msg));
                    frame().ip = handler.ip;
                    continue;
                }

                std::string sfile = frames.empty() ? "" : frame().function->sourceFile;
                throw std::runtime_error(formatEscape(currentLine(), sig.message, sfile));
            }
            catch (const std::exception& ex) {
                std::string msg = ex.what();
                if (!exceptionHandlers.empty() && exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1) frames.pop_back();
                    stack.resize(handler.stackSize);

                    if (msg.find("[") == 0) {
                        size_t c = msg.find("] ");
                        if (c != std::string::npos) msg = msg.substr(c + 2);
                    }
                    push(Value(msg));
                    frame().ip = handler.ip;
                    continue;
                }

                std::string sfile = frames.empty() ? "" : frame().function->sourceFile;
                throw std::runtime_error(formatEscape(currentLine(), ex.what(), sfile));
            }
            catch (...) {
                std::string msg = "Unknown VM Error";
                if (!exceptionHandlers.empty() && exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
                    auto handler = exceptionHandlers.back();
                    exceptionHandlers.pop_back();
                    while (static_cast<int>(frames.size()) > handler.frameIndex + 1) frames.pop_back();
                    stack.resize(handler.stackSize);
                    push(Value(msg));
                    frame().ip = handler.ip;
                    continue;
                }

                std::string sfile = frames.empty() ? "" : frame().function->sourceFile;
                throw std::runtime_error(formatEscape(currentLine(), "Unknown VM Error", sfile));
            }
        }
    }

    void VM::debugPrompt() {
        std::cout << "\n" << col(Ansi::BRIGHT_YELLOW)
            << ">>> [Debugger] Paused at Line " << currentLine()
            << " in " << frame().function->name
            << col(Ansi::RESET) << "\n";

        while (true) {
            std::cout << col(Ansi::BRIGHT_MAGENTA) << "(jc2-dbg) " << col(Ansi::RESET);
            std::string cmd;
            if (!std::getline(std::cin, cmd)) break;

            // 去除头尾空格
            size_t s = cmd.find_first_not_of(" \t");
            if (s != std::string::npos) cmd = cmd.substr(s);
            else continue;

            if (cmd == "c" || cmd == "continue") {
                break; // 恢复执行
            }
            else if (cmd == "s" || cmd == "step") {
                stepNextLine = true;
                break; // 走一步（即步入下一个不同的行号）
            }
            else if (cmd == "stack") {
                std::cout << "--- VM Stack (" << stack.size() << " elements) ---\n";
                // 打印栈内容（即局部变量与中间计算状态）
                for (size_t i = 0; i < stack.size(); i++) {
                    std::cout << " [" << i << "]  " << stack[i];
                    if (static_cast<int>(i) == frame().stackBase) std::cout << "  <-- Frame Base";
                    std::cout << "\n";
                }
                std::cout << "-----------------------------------\n";
            }
            else if (cmd.substr(0, 2) == "p ") {
                std::string varName = cmd.substr(2);
                size_t vs = varName.find_first_not_of(" \t");
                if (vs != std::string::npos) varName = varName.substr(vs);
                // 探查全局变量
                auto it = globals.find(varName);
                if (it != globals.end()) {
                    std::cout << varName << " = " << it->second << "\n";
                }
                else {
                    std::cout << "Variable '" << varName << "' not found in global scope.\n";
                }
            }
            else if (cmd.substr(0, 2) == "b ") {
                int l = std::stoi(cmd.substr(2));
                breakpoints.insert(l);
                std::cout << "Breakpoint set at Line " << l << "\n";
            }
            else if (cmd.substr(0, 3) == "rb ") {
                int l = std::stoi(cmd.substr(3));
                breakpoints.erase(l);
                std::cout << "Breakpoint removed at Line " << l << "\n";
            }
            else if (cmd == "q" || cmd == "quit") {
                throw std::runtime_error("Execution aborted by debugger.");
            }
            else {
                std::cout << "Available Commands:\n"
                    << "  c / continue  : Resume execution until next breakpoint\n"
                    << "  s / step      : Step to the next line of code\n"
                    << "  b <line>      : Set breakpoint at line\n"
                    << "  rb <line>     : Remove breakpoint at line\n"
                    << "  p <global>    : Print a global variable's value\n"
                    << "  stack         : View raw VM memory stack (inspect auto-locals)\n"
                    << "  q / quit      : Abort program\n";
            }
        }
    }

    void VM::printProfileReport() {
        std::cout << "\n" << col(Ansi::BRIGHT_CYAN)
            << "==================================================\n"
            << "               JC2 PROFILER REPORT                \n"
            << "=================================================="
            << col(Ansi::RESET) << "\n";

        // --- 1. 函数耗时排行榜 ---
        std::cout << col(Ansi::BRIGHT_YELLOW) << "\n[Top Functions by Time]\n" << col(Ansi::RESET);

        std::vector<std::pair<std::string, FuncProfile>> funcList(funcProfiles.begin(), funcProfiles.end());
        std::sort(funcList.begin(), funcList.end(), [](const auto& a, const auto& b) {
            return a.second.totalTimeMs > b.second.totalTimeMs;
            });

        int count = 1;
        for (const auto& f : funcList) {
            if (count > 10) break;
            std::cout << "  " << count << ". " << std::left << std::setw(20) << f.first
                << " | " << std::setw(10) << std::fixed << std::setprecision(4) << f.second.totalTimeMs << " ms"
                << " | " << f.second.callCount << " calls\n";
            count++;
        }

        // --- 2. 虚拟机操作码热点榜 ---
        std::cout << col(Ansi::BRIGHT_YELLOW) << "\n[Top 15 VM OpCodes Frequency]\n" << col(Ansi::RESET);

        uint64_t totalOps = 0;
        std::vector<std::pair<OpCode, uint64_t>> opList(opCounts.begin(), opCounts.end());
        for (const auto& op : opList) totalOps += op.second;

        std::sort(opList.begin(), opList.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
            });

        if (totalOps == 0) totalOps = 1;
        count = 1;
        for (const auto& op : opList) {
            if (count > 15) break;  // ★ 设为 15，因为有全量字典了，可以多看几个大头

            // ★ 调用公共字典！去掉前三个字符 "OP_" 让显示更好看
            std::string opName = opCodeToString(op.first).substr(3);

            double perm = (static_cast<double>(op.second) / totalOps) * 100.0;
            std::cout << "  " << std::right << std::setw(2) << count << ". "
                << std::left << std::setw(16) << opName
                << " | " << std::setw(8) << op.second << " times ("
                << std::fixed << std::setprecision(2) << perm << "%)\n";
            count++;
        }
        std::cout << "\n  Total Instructions Executed: " << totalOps << "\n";
        std::cout << col(Ansi::BRIGHT_CYAN) << "==================================================" << col(Ansi::RESET) << "\n\n";

        funcProfiles.clear();
        opCounts.clear();
    }

// =================================================================
// ★ 垃圾回收器实现 (Mark-and-Sweep Garbage Collector)
// =================================================================

    void VM::markClosure(const FunctionClosure& cl,
        std::unordered_set<const void*>& marked)
    {
        if (!cl.capturedEnv.has_value()) return;
        try {
            auto env = std::any_cast<std::shared_ptr<std::vector<Value>>>(cl.capturedEnv);
            if (!env) return;
            for (const auto& v : *env)
                markValue(v, marked);
        }
        catch (...) {}
    }

    void VM::markClassDef(const std::shared_ptr<ClassDefinition>& cls,
        std::unordered_set<const void*>& marked)
    {
        if (!cls) return;
        const void* id = cls.get();
        if (marked.count(id)) return;    // 防止继承链递归循环
        marked.insert(id);

        for (const auto& [name, method] : cls->methods) {
            if (method) markClosure(*method, marked);
        }
        markClassDef(cls->parent, marked);
    }

    void VM::markValue(const Value& val,
        std::unordered_set<const void*>& marked)
    {
        // ── List ──
        if (auto* p = std::get_if<List>(&val.data)) {
            const void* id = p->id();
            if (!id || marked.count(id)) return;
            marked.insert(id);
            for (const auto& elem : p->raw()) {
                try { markValue(std::any_cast<const Value&>(elem), marked); }
                catch (...) {}
            }
            return;
        }

        // ── Dict ──
        if (auto* p = std::get_if<Dict>(&val.data)) {
            const void* id = p->id();
            if (!id || marked.count(id)) return;
            marked.insert(id);
            for (const auto& [key, anyVal] : p->getEntries()) {
                try { markValue(std::any_cast<const Value&>(anyVal), marked); }
                catch (...) {}
            }
            return;
        }

        // ── Set ──
        if (auto* p = std::get_if<Set>(&val.data)) {
            const void* id = p->id();
            if (!id || marked.count(id)) return;
            marked.insert(id);
            for (const auto& [key, elem] : p->raw()) {
                try { markValue(std::any_cast<const Value&>(elem), marked); }
                catch (...) {}
            }
            return;
        }

        // ── Instance ──
        if (auto* p = std::get_if<std::shared_ptr<Instance>>(&val.data)) {
            const void* id = p->get();
            if (!id || marked.count(id)) return;
            marked.insert(id);
            marked.insert((*p)->fields.id());
            for (const auto& [key, anyVal] : (*p)->fields.getEntries()) {
                try { markValue(std::any_cast<const Value&>(anyVal), marked); }
                catch (...) {}
            }
            markClassDef((*p)->classDef, marked);
            return;
        }

               // ── FunctionClosure ──
        if (auto* p = std::get_if<std::shared_ptr<FunctionClosure>>(&val.data)) {
            if (*p) {
                // ★ FIX: 闭包递归阻断锁！防止函数自己调用自己产生的死循环！
                const void* id = p->get();
                if (marked.count(id)) return;
                marked.insert(id);
                markClosure(**p, marked);
            }
            return;
        }

        // ── ClassDefinition ──
        if (auto* p = std::get_if<std::shared_ptr<ClassDefinition>>(&val.data)) {
            markClassDef(*p, marked);
            return;
        }
        
        // ── SuperProxy ──
        if (auto* p = std::get_if<SuperProxyPtr>(&val.data)) {
            if (*p) {
                // ★ FIX: 代理阻断锁
                const void* id = p->get();
                if (marked.count(id)) return;
                marked.insert(id);
                markValue(Value((*p)->instance), marked);
                markClassDef((*p)->parentClass, marked);
            }
            return;
        }

        // 所有其他类型 (double, BigInt, string, Matrix 等) 都是叶子节点，无需追踪
    }

    void VM::collectGarbage() {
        // ═══ Phase 1: MARK ═══
        std::unordered_set<const void*> marked;

        // 根集合 1: 全局变量
        for (const auto& [name, val] : globals)
            markValue(val, marked);

        // 根集合 2: 虚拟机求值栈
        for (const auto& val : stack)
            markValue(val, marked);

        // 根集合 3: 所有调用帧的闭包上值，以及存活帧的上下文引擎！
        for (const auto& f : frames) {
            if (f.upvalues) {
                for (const auto& val : *f.upvalues)
                    markValue(val, marked);
            }
            // ★ 世纪补漏：必须追踪目前存活函数的上下文环境！
            markValue(f.selfContext, marked);
            markValue(f.classContext, marked);
        }

        // 根集合 4: 常量池 (编译后的函数里缓存的字面量)
        for (const auto& fn : compiledFunctions) {
            for (const auto& c : fn->chunk.constants)
                markValue(c, marked);
        }

        // 根集合 5: C++ 层当前正在执行跨界调用的原生对象栈！
        for (const auto& val : helpers::nativeSelfStack) markValue(val, marked);
        for (const auto& val : helpers::nativeClassStack) markValue(val, marked);

        // ═══ Phase 2: SWEEP ═══
        GcHeap::get().sweep(marked);
    }

    int VM::runGC() {
        std::unordered_set<const void*> marked;
        for (const auto& [name, val] : globals)  markValue(val, marked);
        for (const auto& val : stack)            markValue(val, marked);
        for (const auto& f : frames) {
            if (f.upvalues)
                for (const auto& val : *f.upvalues) markValue(val, marked);
            // ★ 防止手动 gc() 触发对象丢失
            markValue(f.selfContext, marked);
            markValue(f.classContext, marked);
        }
        for (const auto& fn : compiledFunctions)
            for (const auto& c : fn->chunk.constants) markValue(c, marked);

        // ★ C++ 原生堆栈手动同步
        for (const auto& val : helpers::nativeSelfStack) markValue(val, marked);
        for (const auto& val : helpers::nativeClassStack) markValue(val, marked);

        return GcHeap::get().sweep(marked);
    }

    void VM::execCall(uint8_t argc) {
        Value callee = stack[stack.size() - 1 - argc];
        pendingRefWritebacks.clear();

        // ======== [1] 字符串动态调用 (晚绑定) ========
        if (std::holds_alternative<std::string>(callee.data)) {
            const std::string& tag = std::get<std::string>(callee.data);
            if (tag.size() >= 5 && tag.substr(0, 5) == "__fn:") {
                int fnIdx = std::stoi(tag.substr(5));
                auto& fn = compiledFunctions[fnIdx];
                if (static_cast<int>(argc) < fn->arity || static_cast<int>(argc) > fn->maxArity)
                    throw std::runtime_error("VM Error: '" + fn->name + "' expects args mismatch.");
                int padCount = fn->maxArity - static_cast<int>(argc);
                for (int j = 0; j < padCount; ++j) push(Value::none());
                int reserveCount = fn->localCount - fn->maxArity;
                for (int j = 0; j < reserveCount; ++j) push(Value::none());
                CallFrame newFrame; newFrame.function = fn.get(); newFrame.ip = 0;
                newFrame.stackBase = static_cast<int>(stack.size()) - fn->localCount;
                stack.erase(stack.begin() + newFrame.stackBase - 1); newFrame.stackBase--;
                frames.push_back(newFrame); return;
            }
            if (tag.size() >= 10 && tag.substr(0, 10) == "__builtin:") {
                std::string fnName = tag.substr(10); std::vector<Value> args(argc);
                for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                pop(); push(nativeBuiltins.find(fnName)->second(args)); return;
            }
            auto nIt = nativeBuiltins.find(tag);
            if (nIt != nativeBuiltins.end()) {
                auto arityIt = builtinArity.find(tag);
                bool arityMatched = false;
                if (arityIt != builtinArity.end() && !arityIt->second.empty()) {
                    if (arityIt->second.count(argc)) arityMatched = true;
                }
                else {
                    arityMatched = true;
                }

                if (arityMatched) {
                    std::vector<Value> args(argc);
                    for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                    pop();
                    push(nIt->second(args));
                    return;
                }
            }

            auto it = globals.find(tag);
            if (it != globals.end()) {
                callee = it->second;
                stack[stack.size() - 1 - argc] = callee;
            }
            else {
                if (nIt != nativeBuiltins.end()) {
                    auto arityIt = builtinArity.find(tag);
                    std::string expected;
                    for (auto aIt = arityIt->second.begin(); aIt != arityIt->second.end(); ++aIt) {
                        if (aIt != arityIt->second.begin()) expected += " or ";
                        expected += std::to_string(*aIt);
                    }
                    throw std::runtime_error("Runtime Error: " + tag + "() expects " +
                        expected + " arguments, got " + std::to_string(argc) + ".");
                }
                throw std::runtime_error("Runtime Error: Unknown function or not callable '" + tag + "()'.");
            }
        } // 结束 if (holds_string)

        // ======== [2] 类实例化 ========
        if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(callee.data)) {
            auto cls = std::get<std::shared_ptr<ClassDefinition>>(callee.data);
            auto instance = std::make_shared<Instance>();
            instance->classDef = cls;

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

            if (initMethod) {
                if (initMethod->isBytecode()) {
                    CallFrame newFrame;
                    // ★ NEW: 直接将新建的 instance 注入帧寄存器！绝不弄脏 globals
                    newFrame.selfContext = Value(instance);
                    newFrame.classContext = Value(initOwner);

                    auto& fnDef = compiledFunctions[initMethod->compiledFnIndex];
                    int padCount = fnDef->maxArity - static_cast<int>(argc);
                    for (int j = 0; j < padCount; ++j) push(Value::none());
                    int reserveCount = fnDef->localCount - fnDef->maxArity;
                    for (int j = 0; j < reserveCount; ++j) push(Value::none());

                    newFrame.function = fnDef.get();
                    newFrame.ip = 0;
                    newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;
                    if (initMethod->hasCaptures()) {
                        newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<Value>>>(
                            initMethod->capturedEnv);
                    }
                    stack.erase(stack.begin() + newFrame.stackBase - 1);
                    newFrame.stackBase--;
                    frames.push_back(newFrame);
                    return;
                }
                else if (initMethod->isNative()) {
                    // ★ NEW: C++ 原生构造器，直接压入专属隔离栈！
                    helpers::nativeSelfStack.push_back(Value(instance));
                    helpers::nativeClassStack.push_back(Value(initOwner));

                    std::vector<Value> args(argc);
                    for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                    pop();

                    try {
                        auto& fn = std::any_cast<NativeCallable&>(initMethod->nativeFn);
                        fn(args);
                    }
                    catch (...) {
                        helpers::nativeSelfStack.pop_back();
                        helpers::nativeClassStack.pop_back();
                        throw;
                    }
                    helpers::nativeSelfStack.pop_back();
                    helpers::nativeClassStack.pop_back();

                    push(Value(instance));
                }
            }
            else if (!initMethod) {
                if (argc > 0) {
                    throw std::runtime_error(
                        "TypeError: Class '" + cls->name +
                        "' takes no arguments directly (no 'init' method defined).");
                }

                // 如果是无参调用（合法），则弹出 Callee 并推入空壳 Instance
                for (int j = 0; j < argc; ++j) pop();
                pop();
                push(Value(instance));
            }
            else {
                throw std::runtime_error("VM Error: init has no callable implementation.");
            }
            return;
        } // 结束 if (holds_class)

        // ======== [3] 闭包执行 ========
        if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(callee.data)) {
            auto closure = std::get<std::shared_ptr<FunctionClosure>>(callee.data);

            if (closure->isBytecode()) {
                auto& fnDef = compiledFunctions[closure->compiledFnIndex];

                if (fnDef->hasRestParam) {
                    int fixedMax = fnDef->maxArity - 1;
                    if (static_cast<int>(argc) < fnDef->arity) {
                        throw std::runtime_error("VM Error: '" + fnDef->name + "' requires at least " + std::to_string(fnDef->arity) + " arguments.");
                    }

                    List restList;
                    if (static_cast<int>(argc) > fixedMax) {
                        int restCount = static_cast<int>(argc) - fixedMax;
                        std::vector<Value> tempValues(restCount);
                        for (int j = 0; j < restCount; j++) {
                            tempValues[restCount - 1 - j] = pop();
                        }
                        for (int j = 0; j < restCount; j++) {
                            restList.push_back(std::make_any<Value>(tempValues[j]));
                        }
                        argc = static_cast<uint8_t>(fixedMax);
                    }

                    int padCount = fixedMax - static_cast<int>(argc);
                    for (int j = 0; j < padCount; ++j) push(Value::none());
                    push(Value(restList));
                }
                else {
                    if (static_cast<int>(argc) < fnDef->arity || static_cast<int>(argc) > fnDef->maxArity)
                        throw std::runtime_error("VM Error: '" + fnDef->name + "' expects " + std::to_string(fnDef->arity) + " to " + std::to_string(fnDef->maxArity) + " arguments, got " + std::to_string(argc) + ".");
                    int padCount = fnDef->maxArity - static_cast<int>(argc);
                    for (int j = 0; j < padCount; ++j) push(Value::none());
                }

                int reserveCount = fnDef->localCount - fnDef->maxArity;
                for (int j = 0; j < reserveCount; ++j) push(Value::none());

                CallFrame newFrame;
                newFrame.function = fnDef.get();
                newFrame.ip = 0;
                newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;

                if (closure->hasCaptures()) {
                    newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<Value>>>(closure->capturedEnv);
                }

                // ★ NEW：将该闭包出生时带的 self 塞进新帧的心房！
                newFrame.selfContext = closure->boundSelf;
                newFrame.classContext = closure->boundClass;

                stack.erase(stack.begin() + newFrame.stackBase - 1);
                newFrame.stackBase--;

                frames.push_back(newFrame);
                return;
            }
            else if (closure->isNative()) {
                std::vector<Value> args(argc);
                for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                pop();

                // ★ NEW：C++ 原生闭包也进隔离池
                helpers::nativeSelfStack.push_back(closure->boundSelf);
                helpers::nativeClassStack.push_back(closure->boundClass);

                auto& fn = std::any_cast<NativeCallable&>(closure->nativeFn);
                Value result;
                try { result = fn(args); }
                catch (...) {
                    helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                    throw;
                }
                helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();

                push(result);
                return;
            }
            throw std::runtime_error("VM Error: Invalid closure.");
        } // 结束 if (holds_function)

        // ======== [4] String Tag fallback ========
        if (std::holds_alternative<std::string>(callee.data)) {
            const std::string& tag = std::get<std::string>(callee.data);

            if (tag.size() >= 5 && tag.substr(0, 5) == "__fn:") {
                int fnIdx = std::stoi(tag.substr(5));
                auto& fn = compiledFunctions[fnIdx];

                if (fn->hasRestParam) {
                    int fixedMax = fn->maxArity - 1;
                    if (static_cast<int>(argc) < fn->arity) {
                        throw std::runtime_error("VM Error: '" + fn->name + "' requires at least " + std::to_string(fn->arity) + " arguments.");
                    }

                    List restList;
                    if (static_cast<int>(argc) > fixedMax) {
                        int restCount = static_cast<int>(argc) - fixedMax;
                        std::vector<Value> tempValues(restCount);
                        for (int j = 0; j < restCount; j++) {
                            tempValues[restCount - 1 - j] = pop();
                        }
                        for (int j = 0; j < restCount; j++) {
                            restList.push_back(std::make_any<Value>(tempValues[j]));
                        }
                        argc = static_cast<uint8_t>(fixedMax);
                    }

                    int padCount = fixedMax - static_cast<int>(argc);
                    for (int j = 0; j < padCount; ++j) push(Value::none());
                    push(Value(restList));
                }
                else {
                    if (static_cast<int>(argc) < fn->arity || static_cast<int>(argc) > fn->maxArity)
                        throw std::runtime_error("VM Error: '" + fn->name + "' expects " + std::to_string(fn->arity) + " to " + std::to_string(fn->maxArity) + " arguments, got " + std::to_string(argc) + ".");
                    int padCount = fn->maxArity - static_cast<int>(argc);
                    for (int j = 0; j < padCount; ++j) push(Value::none());
                }

                int reserveCount = fn->localCount - fn->maxArity;
                for (int j = 0; j < reserveCount; ++j) push(Value::none());

                CallFrame newFrame; newFrame.function = fn.get(); newFrame.ip = 0;
                newFrame.stackBase = static_cast<int>(stack.size()) - fn->localCount;
                stack.erase(stack.begin() + newFrame.stackBase - 1); newFrame.stackBase--;
                frames.push_back(newFrame); return;
            }

            if (tag.size() >= 10 && tag.substr(0, 10) == "__builtin:") {
                std::string fnName = tag.substr(10);
                std::vector<Value> args(argc);
                for (int j = argc - 1; j >= 0; --j) args[j] = pop();
                pop();
                auto nit = nativeBuiltins.find(fnName);
                if (nit == nativeBuiltins.end())
                    throw std::runtime_error("VM Error: Unknown builtin '" + fnName + "'.");
                push(nit->second(args));
                return;
            }
        } // 结束 if (fallback string tag)

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
    } // 结束 execCall

    void VM::execIndexGet(uint8_t dims) {
        if (dims == 1) {
            Value idx = pop();
            Value obj = pop();

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
                return;
            }

            int i = static_cast<int>(std::round(idx.asDouble()));

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
            else if (std::holds_alternative<List>(obj.data)) {
                push(std::any_cast<Value>(std::get<List>(obj.data).at(i)));
            }
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
                std::shared_ptr<FunctionClosure> getitemMethod;
                while (c) {
                    auto it = c->methods.find("__getitem__");
                    if (it != c->methods.end()) {
                        getitemMethod = it->second;
                        break;
                    }
                    c = c->parent;
                }
                if (getitemMethod) {
                    // ★ 不再使用 globals["self"] = Value(inst); 
                    if (getitemMethod->isBytecode()) {
                        auto& fnDef = compiledFunctions[getitemMethod->compiledFnIndex];

                        push(idx); // 将参数重新压回栈

                        // ★ 补齐缺省的默认参数
                        int padCount = fnDef->maxArity - 1;
                        for (int j = 0; j < padCount; ++j) push(Value::none());

                        // ★ 为该方法的 Auto-local 预留局部变量栈槽
                        int reserveCount = fnDef->localCount - fnDef->maxArity;
                        for (int j = 0; j < reserveCount; ++j) push(Value::none());

                        CallFrame newFrame;
                        newFrame.function = fnDef.get();
                        newFrame.ip = 0;
                        // 基址对齐至参数(idx)起始处
                        newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;

                        if (getitemMethod->hasCaptures()) {
                            newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<Value>>>(getitemMethod->capturedEnv);
                        }

                        // ★ NEW：直接注入专属上下文帧！
                        newFrame.selfContext = Value(inst);
                        newFrame.classContext = Value(inst->classDef);

                        frames.push_back(newFrame);
                        return;
                    }
                    else if (getitemMethod->isNative()) {
                        // ★ NEW：如果是 C++ 层，压入原生保护栈
                        helpers::nativeSelfStack.push_back(Value(inst));
                        helpers::nativeClassStack.push_back(Value(inst->classDef));
                        Value result;
                        try {
                            auto& fn = std::any_cast<NativeCallable&>(getitemMethod->nativeFn);
                            result = fn({ idx });
                        }
                        catch (...) {
                            helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                            throw;
                        }
                        helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                        push(result);
                    }
                    else {
                        throw std::runtime_error("VM Error: __getitem__ has no callable implementation.");
                    }
                }
                else {
                    throw std::runtime_error("VM Error: Cannot index this instance (no __getitem__).");
                }
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

            if (std::holds_alternative<RealMatrix>(obj.data)) {
                const auto& m = std::get<RealMatrix>(obj.data);
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                push(Value(m(r, c)));
            }
            else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                const auto& m = std::get<ComplexMatrix>(obj.data);
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                push(Value(m(r, c)));
            }
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
        return;
    }

    void VM::execIndexSet(uint8_t dims) {
        Value val = pop();

        if (dims == 1) {
            Value idx = pop();
            Value& obj = peek(0);

            if (std::holds_alternative<Dict>(obj.data)) {
                std::string key;
                if (std::holds_alternative<std::string>(idx.data))
                    key = std::get<std::string>(idx.data);
                else {
                    std::ostringstream oss; oss << idx;
                    key = oss.str();
                }
                std::get<Dict>(obj.data).set(key, std::make_any<Value>(val));
                return;
            }

            int i = static_cast<int>(std::round(idx.asDouble()));

            if (std::holds_alternative<RealMatrix>(obj.data)) {
                auto& m = std::get<RealMatrix>(obj.data);
                if (std::holds_alternative<Complex>(val.data) || std::holds_alternative<ComplexMatrix>(val.data)) {
                    ComplexMatrix cm = m.toComplexMatrix();
                    if (cm.getRows() == 1) {
                        if (i < 0) i = cm.getCols() + i;
                        cm(0, i) = val.asComplex();
                    }
                    else if (cm.getCols() == 1) {
                        if (i < 0) i = cm.getRows() + i;
                        cm(i, 0) = val.asComplex();
                    }
                    else {
                        if (i < 0) i = cm.getRows() + i;
                        if (i < 0 || i >= cm.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");
                        if (std::holds_alternative<ComplexMatrix>(val.data)) {
                            auto srcFlat = std::get<ComplexMatrix>(val.data).rawData();
                            if (static_cast<int>(srcFlat.size()) != cm.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                            for (int j = 0; j < cm.getCols(); ++j) cm(i, j) = srcFlat[j];
                        }
                        else {
                            Complex cv = val.asComplex();
                            for (int j = 0; j < cm.getCols(); ++j) cm(i, j) = cv;
                        }
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
                    else {
                        // ★ 2D 矩阵整行赋值 / 广播
                        if (i < 0) i = m.getRows() + i;
                        if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");
                        if (std::holds_alternative<RealMatrix>(val.data)) {
                            const auto& src = std::get<RealMatrix>(val.data);
                            auto srcFlat = src.rawData();
                            if (static_cast<int>(srcFlat.size()) != m.getCols())
                                throw std::runtime_error("VM Error: Row assignment size mismatch.");
                            for (int j = 0; j < m.getCols(); ++j) m(i, j) = srcFlat[j];
                        }
                        else {
                            double dVal = val.asDouble();
                            for (int j = 0; j < m.getCols(); ++j) m(i, j) = dVal;
                        }
                    }
                }
            }
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
                else {
                    // ★ 2D 复数矩阵整行赋值 / 广播
                    if (i < 0) i = m.getRows() + i;
                    if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");

                    if (std::holds_alternative<ComplexMatrix>(val.data)) {
                        auto srcFlat = std::get<ComplexMatrix>(val.data).rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = srcFlat[j];
                    }
                    // ★ 补足支持对复数矩阵写入实值行向量：
                    else if (std::holds_alternative<RealMatrix>(val.data)) {
                        auto srcFlat = std::get<RealMatrix>(val.data).rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = Complex(srcFlat[j], 0.0);
                    }
                    else {
                        // 标量广播
                        Complex cv = val.asComplex();
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = cv;
                    }
                }
            }
            else if (std::holds_alternative<StringMatrix>(obj.data)) {
                auto& m = std::get<StringMatrix>(obj.data);

                if (m.getRows() == 1) {
                    if (i < 0) i = m.getCols() + i;
                    if (std::holds_alternative<std::string>(val.data)) m(0, i) = std::get<std::string>(val.data);
                    else { std::ostringstream oss; oss << val; m(0, i) = oss.str(); }
                }
                else if (m.getCols() == 1) {
                    if (i < 0) i = m.getRows() + i;
                    if (std::holds_alternative<std::string>(val.data)) m(i, 0) = std::get<std::string>(val.data);
                    else { std::ostringstream oss; oss << val; m(i, 0) = oss.str(); }
                }
                else {
                    // ★ 2D 字符串矩阵整行赋值 / 广播
                    if (i < 0) i = m.getRows() + i;
                    if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");

                    if (std::holds_alternative<StringMatrix>(val.data)) {
                        auto srcFlat = std::get<StringMatrix>(val.data).rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = srcFlat[j];
                    }
                    // ★ 将任何实数矩阵映射到字符串行中
                    else if (std::holds_alternative<RealMatrix>(val.data)) {
                        auto srcFlat = std::get<RealMatrix>(val.data).rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) { std::ostringstream oss; oss << Value(srcFlat[j]); m(i, j) = oss.str(); }
                    }
                    // ★ 将任何复数矩阵映射到字符串行中
                    else if (std::holds_alternative<ComplexMatrix>(val.data)) {
                        auto srcFlat = std::get<ComplexMatrix>(val.data).rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) { std::ostringstream oss; oss << Value(srcFlat[j]); m(i, j) = oss.str(); }
                    }
                    else {
                        // 标量广播
                        std::string s;
                        if (std::holds_alternative<std::string>(val.data)) s = std::get<std::string>(val.data);
                        else { std::ostringstream oss; oss << val; s = oss.str(); }
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = s;
                    }
                }
            }
            else if (std::holds_alternative<List>(obj.data)) {
                std::get<List>(obj.data).set(i, std::make_any<Value>(val));
            }
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
            // ── Instance (__setitem__) ──
            else if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
                auto c = inst->classDef;
                std::shared_ptr<FunctionClosure> setitemMethod;
                while (c) {
                    auto it = c->methods.find("__setitem__");
                    if (it != c->methods.end()) {
                        setitemMethod = it->second;
                        break;
                    }
                    c = c->parent;
                }
                if (setitemMethod) {
                    // ★ 不再使用 globals["self"] = Value(inst); 
                    if (setitemMethod->isBytecode()) {
                        std::shared_ptr<std::vector<Value>> captures = nullptr;
                        if (setitemMethod->hasCaptures())
                            captures = std::any_cast<std::shared_ptr<std::vector<Value>>>(setitemMethod->capturedEnv);

                        // ★ NEW：向 callVMFunction 直接投喂 boundSelf 和 boundClass！
                        callVMFunction(setitemMethod->compiledFnIndex, { idx, val }, captures, Value(inst), Value(inst->classDef));
                    }
                    else if (setitemMethod->isNative()) {
                        helpers::nativeSelfStack.push_back(Value(inst));
                        helpers::nativeClassStack.push_back(Value(inst->classDef));
                        try {
                            auto& fn = std::any_cast<NativeCallable&>(setitemMethod->nativeFn);
                            fn({ idx, val });
                        }
                        catch (...) {
                            helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                            throw;
                        }
                        helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                    }
                    else {
                        throw std::runtime_error("VM Error: __setitem__ has no callable implementation.");
                    }
                }
                else {
                    throw std::runtime_error("VM Error: Cannot assign index on this instance (no __setitem__).");
                }
            }
        }
        else if (dims == 2) {
            Value col = pop();
            Value row = pop();
            Value& obj = peek(0);
            int r = static_cast<int>(std::round(row.asDouble()));
            int c = static_cast<int>(std::round(col.asDouble()));

            if (std::holds_alternative<RealMatrix>(obj.data)) {
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
            else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                auto& m = std::get<ComplexMatrix>(obj.data);
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                m(r, c) = val.asComplex();
            }
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
        return;
    }

    void VM::execSliceGet(uint8_t dims) {
        auto readOptionalInt = [this]() -> std::pair<bool, int> {
            Value v = pop();
            if (v.isNone()) return { false, 0 };
            return { true, static_cast<int>(std::round(v.asDouble())) };
            };

        auto buildSliceIndices = [](int dimSize, std::pair<bool, int> start,
            std::pair<bool, int> end,
            std::pair<bool, int> step) -> std::vector<int> {
                int sp = step.first ? step.second : 1;

                // ★ 点索引标记：step 被显式设置为 0
                if (step.first && sp == 0) {
                    int idx = start.first ? start.second : 0;
                    if (idx < 0) idx = dimSize + idx;
                    if (idx < 0 || idx >= dimSize)
                        throw std::out_of_range("VM Error: Index out of bounds.");
                    return { idx };
                }

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

            if (std::holds_alternative<std::string>(obj.data)) {
                const auto& s = std::get<std::string>(obj.data);
                auto ids = buildSliceIndices(static_cast<int>(s.size()), start, end, step);
                std::string result;
                for (int id : ids) result += s[id];
                push(Value(result));
                return;
            }

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
                    int rc = static_cast<int>(ids.size());
                    std::vector<double> flat;
                    for (int id : ids)
                        for (int j = 0; j < m.getCols(); ++j)
                            flat.push_back(m(id, j));
                    push(Value(RealMatrix(rc, m.getCols(), flat)));
                }
                return;
            }

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
                return;
            }

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
                return;
            }

            if (std::holds_alternative<List>(obj.data)) {
                const auto& L = std::get<List>(obj.data);
                auto ids = buildSliceIndices(static_cast<int>(L.size()), start, end, step);
                List result;
                for (int id : ids) result.push_back(L.raw()[id]);
                push(Value(result));
                return;
            }

            throw std::runtime_error("VM Error: Cannot slice this type.");
        }
        else if (dims == 2) {
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
        return;
    }

    void VM::execSliceSet(uint8_t dims) {

        auto readOptionalInt = [this]() -> std::pair<bool, int> {
            Value v = pop();
            if (v.isNone()) return { false, 0 };
            return { true, static_cast<int>(std::round(v.asDouble())) };
            };

        auto buildSliceIndices = [](int dimSize, std::pair<bool, int> start,
            std::pair<bool, int> end,
            std::pair<bool, int> step) -> std::vector<int> {
                int sp = step.first ? step.second : 1;

                // ★ 点索引标记：step 被显式设置为 0
                if (step.first && sp == 0) {
                    int idx = start.first ? start.second : 0;
                    if (idx < 0) idx = dimSize + idx;
                    if (idx < 0 || idx >= dimSize)
                        throw std::out_of_range("VM Error: Index out of bounds.");
                    return { idx };
                }

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
                if (sp > 0) {
                    for (int i = st; i < en; i += sp) ids.push_back(i);
                }
                else {
                    for (int i = st; i > en; i += sp) ids.push_back(i);
                }
                return ids;
            };

        if (dims == 1) {
            Value val = pop();
            auto step = readOptionalInt();
            auto end = readOptionalInt();
            auto start = readOptionalInt();
            Value& obj = peek(0);

            if (std::holds_alternative<RealMatrix>(obj.data)) {
                auto& m = std::get<RealMatrix>(obj.data);
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);

                if (std::holds_alternative<double>(val.data) ||
                    std::holds_alternative<BigInt>(val.data) ||
                    std::holds_alternative<Fraction>(val.data)) {
                    double v = val.asDouble();
                    if (m.getRows() == 1) {
                        for (int id : ids) m(0, id) = v;
                    }
                    else if (m.getCols() == 1) {
                        for (int id : ids) m(id, 0) = v;
                    }
                    else {
                        // ★ 广播到这几行的所有列！
                        for (int id : ids)
                            for (int j = 0; j < m.getCols(); ++j) m(id, j) = v;
                    }
                }
                else if (std::holds_alternative<RealMatrix>(val.data)) {
                    const auto& src = std::get<RealMatrix>(val.data);
                    auto srcFlat = src.rawData();

                    if (m.getRows() == 1 || m.getCols() == 1) {
                        // 纯向量赋值
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
                        // 2D 矩阵的单维整行赋值（M[0:1] 意味着替换第0行的所有列）
                        if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()) * m.getCols())
                            throw std::runtime_error("VM Error: Slice assignment size mismatch for matrix row.");
                        for (size_t k = 0; k < ids.size(); ++k) {
                            for (int j = 0; j < m.getCols(); ++j) {
                                m(ids[k], j) = srcFlat[k * m.getCols() + j];
                            }
                        }
                    }
                }
                else {
                    throw std::runtime_error("VM Error: Cannot assign this type to slice.");
                }
            }
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
                    for (int id : ids)
                        L.set(id, std::make_any<Value>(val));
                }
            }
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
            else if (std::holds_alternative<ComplexMatrix>(obj.data)) {
                auto& m = std::get<ComplexMatrix>(obj.data);
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);
                if (std::holds_alternative<ComplexMatrix>(val.data)) {
                    auto srcFlat = std::get<ComplexMatrix>(val.data).rawData();

                    if (m.getRows() == 1 || m.getCols() == 1) {
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
                        if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()) * m.getCols())
                            throw std::runtime_error("VM Error: Slice assignment size mismatch for matrix row.");
                        for (size_t k = 0; k < ids.size(); ++k) {
                            for (int j = 0; j < m.getCols(); ++j) {
                                m(ids[k], j) = srcFlat[k * m.getCols() + j];
                            }
                        }
                    }
                }
                else {
                    Complex cv = val.asComplex();
                    if (m.getRows() == 1) {
                        for (int id : ids) m(0, id) = cv;
                    }
                    else if (m.getCols() == 1) {
                        for (int id : ids) m(id, 0) = cv;
                    }
                    else {
                        for (int id : ids)
                            for (int j = 0; j < m.getCols(); ++j) m(id, j) = cv;
                    }
                }
            }
            else if (std::holds_alternative<StringMatrix>(obj.data)) {
                auto& m = std::get<StringMatrix>(obj.data);
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);
                if (std::holds_alternative<StringMatrix>(val.data)) {
                    auto srcFlat = std::get<StringMatrix>(val.data).rawData();

                    if (m.getRows() == 1 || m.getCols() == 1) {
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
                        if (static_cast<int>(srcFlat.size()) != static_cast<int>(ids.size()) * m.getCols())
                            throw std::runtime_error("VM Error: Slice assignment size mismatch for matrix row.");
                        for (size_t k = 0; k < ids.size(); ++k) {
                            for (int j = 0; j < m.getCols(); ++j) {
                                m(ids[k], j) = srcFlat[k * m.getCols() + j];
                            }
                        }
                    }
                }
                else {
                    std::string sv;
                    if (std::holds_alternative<std::string>(val.data)) sv = std::get<std::string>(val.data);
                    else { std::ostringstream oss; oss << val; sv = oss.str(); }

                    if (m.getRows() == 1) {
                        for (int id : ids) m(0, id) = sv;
                    }
                    else if (m.getCols() == 1) {
                        for (int id : ids) m(id, 0) = sv;
                    }
                    else {
                        for (int id : ids)
                            for (int j = 0; j < m.getCols(); ++j) m(id, j) = sv;
                    }
                }
            }
            else {
                throw std::runtime_error("VM Error: Cannot slice-assign this type.");
            }
        }
        else if (dims == 2) {
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

                using ElemType = std::decay_t<decltype(m(0, 0))>;

                // 检测右值是否为一个矩阵
                bool isRhsMat = std::holds_alternative<RealMatrix>(val.data) ||
                    std::holds_alternative<ComplexMatrix>(val.data) ||
                    std::holds_alternative<StringMatrix>(val.data);

                if (isRhsMat) {
                    int srcR = 0, srcC = 0;
                    if (std::holds_alternative<RealMatrix>(val.data)) {
                        srcR = std::get<RealMatrix>(val.data).getRows();
                        srcC = std::get<RealMatrix>(val.data).getCols();
                    }
                    else if (std::holds_alternative<ComplexMatrix>(val.data)) {
                        srcR = std::get<ComplexMatrix>(val.data).getRows();
                        srcC = std::get<ComplexMatrix>(val.data).getCols();
                    }
                    else {
                        srcR = std::get<StringMatrix>(val.data).getRows();
                        srcC = std::get<StringMatrix>(val.data).getCols();
                    }

                    if (srcR != dstR || srcC != dstC)
                        throw std::runtime_error("VM Error: Slice assignment size mismatch.");

                    for (int i = 0; i < dstR; ++i) {
                        for (int j = 0; j < dstC; ++j) {
                            if constexpr (std::is_same_v<ElemType, double>) {
                                if (std::holds_alternative<RealMatrix>(val.data))
                                    m(rIds[i], cIds[j]) = std::get<RealMatrix>(val.data)(i, j);
                                else
                                    throw std::runtime_error("VM Error: Cannot assign complex/string matrix to real matrix slice.");
                            }
                            else if constexpr (std::is_same_v<ElemType, Complex>) {
                                if (std::holds_alternative<ComplexMatrix>(val.data))
                                    m(rIds[i], cIds[j]) = std::get<ComplexMatrix>(val.data)(i, j);
                                else if (std::holds_alternative<RealMatrix>(val.data))
                                    m(rIds[i], cIds[j]) = Complex(std::get<RealMatrix>(val.data)(i, j));
                                else
                                    throw std::runtime_error("VM Error: Cannot assign string matrix to complex matrix slice.");
                            }
                            else if constexpr (std::is_same_v<ElemType, std::string>) {
                                std::ostringstream oss;
                                if (std::holds_alternative<StringMatrix>(val.data))
                                    oss << std::get<StringMatrix>(val.data)(i, j);
                                else if (std::holds_alternative<ComplexMatrix>(val.data))
                                    oss << Value(std::get<ComplexMatrix>(val.data)(i, j));
                                else
                                    oss << Value(std::get<RealMatrix>(val.data)(i, j));
                                m(rIds[i], cIds[j]) = oss.str();
                            }
                        }
                    }
                }
                else {
                    // 万能标量广播 (Scalar Broadcast)
                    ElemType scalarVal{};
                    if constexpr (std::is_same_v<ElemType, double>) {
                        scalarVal = val.asDouble(); // 可承接 int, double, fraction 等
                    }
                    else if constexpr (std::is_same_v<ElemType, Complex>) {
                        scalarVal = val.asComplex();
                    }
                    else if constexpr (std::is_same_v<ElemType, std::string>) {
                        if (std::holds_alternative<std::string>(val.data))
                            scalarVal = std::get<std::string>(val.data);
                        else {
                            std::ostringstream oss; oss << val; scalarVal = oss.str();
                        }
                    }

                    for (int ri : rIds)
                        for (int ci : cIds)
                            m(ri, ci) = scalarVal;
                }
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
        return;
    }

    void VM::execBuildMatrix(uint16_t rows, uint16_t cols) {
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

        if (hasOther) {
            if (rows == 1) {
                List L;
                for (int ii = 0; ii < total; ++ii)
                    L.push_back(std::make_any<Value>(stack[stack.size() - total + ii]));
                result = Value(L);
            }
            else {
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
        else {
            bool hasSubMatrix = false;
            for (int ii = 0; ii < total; ++ii) {
                const Value& v = stack[stack.size() - total + ii];
                if (std::holds_alternative<RealMatrix>(v.data) ||
                    std::holds_alternative<ComplexMatrix>(v.data) ||
                    std::holds_alternative<StringMatrix>(v.data))
                    hasSubMatrix = true;
            }

            if (hasSubMatrix) {
                auto extractCell = [&](Value& cell) {
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
        return;
    }

    void VM::execIn() {
        Value haystack = pop(), needle = pop();

        if (std::holds_alternative<std::string>(needle.data) &&
            std::holds_alternative<std::string>(haystack.data)) {
            bool found = std::get<std::string>(haystack.data).find(
                std::get<std::string>(needle.data)) != std::string::npos;
            push(Value(found ? 1.0 : 0.0));
            return;
        }
        if (std::holds_alternative<std::string>(haystack.data)) {
            throw std::runtime_error(
                "VM Error: 'in' on string requires a string on the left side.");
        }

        if (std::holds_alternative<RealMatrix>(haystack.data)) {
            const auto& m = std::get<RealMatrix>(haystack.data);
            double target;
            try { target = needle.asDouble(); }
            catch (...) { push(Value(0.0)); return; }
            for (const auto& v : m.rawData()) {
                if (Tol::isEq(v, target, 1e4)) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (std::holds_alternative<ComplexMatrix>(haystack.data)) {
            const auto& m = std::get<ComplexMatrix>(haystack.data);
            Complex target;
            try { target = needle.asComplex(); }
            catch (...) { push(Value(0.0)); return; }
            for (const auto& v : m.rawData()) {
                if (v == target) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (std::holds_alternative<StringMatrix>(haystack.data)) {
            if (!std::holds_alternative<std::string>(needle.data))
                throw std::runtime_error(
                    "VM Error: 'in' on StringMatrix requires a string needle.");
            const auto& m = std::get<StringMatrix>(haystack.data);
            const auto& target = std::get<std::string>(needle.data);
            for (const auto& v : m.rawData()) {
                if (v == target) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (std::holds_alternative<List>(haystack.data)) {
            const auto& L = std::get<List>(haystack.data);
            for (const auto& e : L.raw()) {
                try {
                    Value elem = std::any_cast<Value>(e);
                    if (vmValuesEqual(needle, elem)) {
                        push(Value(1.0));
                        return;
                    }
                }
                catch (...) {}
            }
            push(Value(0.0));
            return;
        }

        if (std::holds_alternative<Dict>(haystack.data)) {
            std::string key;
            if (std::holds_alternative<std::string>(needle.data))
                key = std::get<std::string>(needle.data);
            else {
                std::ostringstream oss; oss << needle;
                key = oss.str();
            }
            push(Value(std::get<Dict>(haystack.data).has(key) ? 1.0 : 0.0));
            return;
        }

        if (std::holds_alternative<Set>(haystack.data)) {
            push(Value(std::get<Set>(haystack.data).contains(setValueKey(needle)) ? 1.0 : 0.0));
            return;
        }

        if (std::holds_alternative<std::shared_ptr<Instance>>(haystack.data)) {
            auto method = findDunder(haystack, "__contains__");
            if (method) {
                push(Value(isTruthy(callDunder(haystack, "__contains__", { needle })) ? 1.0 : 0.0));
                return;
            }
        }
        throw std::runtime_error(
            "VM Error: 'in' requires an array, vector, matrix, string, list, dict, or instance.");
    }

    // VM.cpp 中的实现：
    Value VM::execReturn(bool& shouldExit) {
        shouldExit = false;
        Value result = pop();
        int base = frame().stackBase;
        std::string fnName = frame().function->name;

        // ★ 核心：记录下属于当前自身心跳的上下文
        Value activeSelf = frame().selfContext;

        // --- ref writeback ---
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

        while (!exceptionHandlers.empty() &&
            exceptionHandlers.back().frameIndex == static_cast<int>(frames.size()) - 1) {
            exceptionHandlers.pop_back();
        }

        frames.pop_back();

        // ★ 退出判定
        if (static_cast<int>(frames.size()) <= currentTargetFrameDepth) {
            if (currentTargetFrameDepth == 0) {
                stack.clear();
            }
            else {
                stack.resize(base);
            }
            shouldExit = true;  // 通知 run() 退出
            return result;
        }

        stack.resize(base);

        // ★ 唯独构造函数返回时做个特判：如果你调用了 init()，VM 会默默返回正在创建的对象
        if (fnName == "init") {
            push(activeSelf.isNone() ? result : activeSelf);
        }
        else {
            push(result);
        }
        return Value::none();
    }

    void VM::execInvoke(uint16_t nameIdx, uint8_t argc) {
        std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);
        Value obj = stack[stack.size() - 1 - argc];

        std::shared_ptr<FunctionClosure> method;
        std::shared_ptr<ClassDefinition> owningClass = nullptr;

        // ==============================================================
        // 1. 如果它是原生 Dict！我们要像对待对象一样去调用它内部的闭包
        // ==============================================================
        if (std::holds_alternative<Dict>(obj.data)) {
            const auto* v = std::get<Dict>(obj.data).get(methodName);
            if (v) {
                Value fv = std::any_cast<Value>(*v);
                if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(fv.data)) {
                    method = std::get<std::shared_ptr<FunctionClosure>>(fv.data);
                }
            }
            if (!method) {
                throw std::runtime_error("VM Error: No callable field '" + methodName + "' in Dict.");
            }
        }
        // ==============================================================
        // 2. 经典面向对象 Instance 的方法查询（优先类模板，后查原型挂载）
        // ==============================================================
        else if (std::holds_alternative<std::shared_ptr<Instance>>(obj.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(obj.data);
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

            if (!method) {
                auto* fieldVal = inst->fields.get(methodName);
                if (fieldVal) {
                    Value fv = std::any_cast<Value>(*fieldVal);
                    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(fv.data)) {
                        method = std::get<std::shared_ptr<FunctionClosure>>(fv.data);
                        owningClass = inst->classDef;
                    }
                }
            }

            if (!method) {
                throw std::runtime_error("VM Error: No method '" + methodName +
                    "' on instances of class '" + inst->classDef->name + "'.");
            }
        }
        else {
            throw std::runtime_error("VM Error: Cannot invoke method on this type.");
        }

        // ==============================================================
        // ★ 核心方法执行引擎：此时的 obj 不论是 Dict 还是 Instance，
        // 都会被公平地当做 `self` 注入环境！
        // ==============================================================
        if (method->isBytecode()) {
            CallFrame newFrame;
            // ★ Magic: 跨过 globals 的直接帧级注入！
            newFrame.selfContext = obj;
            newFrame.classContext = owningClass ? Value(owningClass) : Value::none();
            auto& fnDef = compiledFunctions[method->compiledFnIndex];

            if (fnDef->hasRestParam) {
                int fixedMax = fnDef->maxArity - 1;
                if (static_cast<int>(argc) < fnDef->arity) {
                    throw std::runtime_error("VM Error: '" + fnDef->name + "' requires at least " + std::to_string(fnDef->arity) + " arguments.");
                }

                List restList;
                if (static_cast<int>(argc) > fixedMax) {
                    int restCount = static_cast<int>(argc) - fixedMax;
                    std::vector<Value> tempValues(restCount);
                    for (int j = 0; j < restCount; j++) tempValues[restCount - 1 - j] = pop();
                    for (int j = 0; j < restCount; j++) restList.push_back(std::make_any<Value>(tempValues[j]));
                    argc = static_cast<uint8_t>(fixedMax);
                }

                int padCount = fixedMax - static_cast<int>(argc);
                for (int j = 0; j < padCount; ++j) push(Value::none());
                push(Value(restList));
            }
            else {
                if (static_cast<int>(argc) < fnDef->arity || static_cast<int>(argc) > fnDef->maxArity)
                    throw std::runtime_error("VM Error: '" + fnDef->name + "' expects " + std::to_string(fnDef->arity) + " to " + std::to_string(fnDef->maxArity) + " arguments, got " + std::to_string(argc) + ".");
                int padCount = fnDef->maxArity - static_cast<int>(argc);
                for (int j = 0; j < padCount; ++j) push(Value::none());
            }

            int reserveCount = fnDef->localCount - fnDef->maxArity;
            for (int j = 0; j < reserveCount; ++j) push(Value::none());
            newFrame.function = fnDef.get();
            newFrame.ip = 0;
            newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;
            if (method->hasCaptures()) {
                newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<Value>>>(
                    method->capturedEnv);
            }
            stack.erase(stack.begin() + newFrame.stackBase - 1);
            newFrame.stackBase--;
            frames.push_back(newFrame);
            return;
        }
        else if (method->isNative()) {
            // ★ C++ 原生函数直接进隔离池
            helpers::nativeSelfStack.push_back(obj);
            helpers::nativeClassStack.push_back(owningClass ? Value(owningClass) : Value::none());

            std::vector<Value> args(argc);
            for (int j = argc - 1; j >= 0; --j) args[j] = pop();
            pop();
            Value result;
            try {
                auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
                result = fn(args);
            }
            catch (...) {
                helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                throw;
            }
            helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
            push(result);
            return;
        }

        throw std::runtime_error("VM Error: Method '" + methodName +
            "' has no callable implementation.");
    }

    void VM::execSuperInvoke(uint16_t nameIdx, uint8_t argc) {
        std::string methodName = std::get<std::string>(currentChunk().constants[nameIdx].data);
        Value selfVal = stack[stack.size() - 1 - argc];
        if (!std::holds_alternative<std::shared_ptr<Instance>>(selfVal.data))
            throw std::runtime_error("VM Error: 'super' requires an instance context.");
        auto inst = std::get<std::shared_ptr<Instance>>(selfVal.data);
        // ★ FIX: 直接从当前函数的帧寄存器提取！
        Value classVal = frame().classContext;
        if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(classVal.data))
            throw std::runtime_error("VM Error: 'super' requires class context (__class__).");
        auto currentClass = std::get<std::shared_ptr<ClassDefinition>>(classVal.data);
        auto parentClass = currentClass->parent;
        if (!parentClass)
            throw std::runtime_error("VM Error: Class '" + currentClass->name +
                "' has no parent class.");

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

        // ★ 不在这里赋值！

        if (method->isBytecode()) {
            CallFrame newFrame;
            newFrame.selfContext = Value(inst);         
            newFrame.classContext = Value(owningClass); 

            auto& fnDef = compiledFunctions[method->compiledFnIndex];

            // =============================================================
            // ★ 核心变长参数打包引擎 (OOP SuperInvoke 端)
            // =============================================================
            if (fnDef->hasRestParam) {
                int fixedMax = fnDef->maxArity - 1;
                if (static_cast<int>(argc) < fnDef->arity) {
                    throw std::runtime_error("VM Error: '" + fnDef->name + "' requires at least " + std::to_string(fnDef->arity) + " arguments.");
                }

                List restList;
                if (static_cast<int>(argc) > fixedMax) {
                    int restCount = static_cast<int>(argc) - fixedMax;
                    std::vector<Value> tempValues(restCount);
                    for (int j = 0; j < restCount; j++) {
                        tempValues[restCount - 1 - j] = pop();
                    }
                    for (int j = 0; j < restCount; j++) {
                        restList.push_back(std::make_any<Value>(tempValues[j]));
                    }
                    argc = static_cast<uint8_t>(fixedMax);
                }

                int padCount = fixedMax - static_cast<int>(argc);
                for (int j = 0; j < padCount; ++j) push(Value::none());
                push(Value(restList));
            }
            else {
                if (static_cast<int>(argc) < fnDef->arity || static_cast<int>(argc) > fnDef->maxArity)
                    throw std::runtime_error("VM Error: '" + fnDef->name + "' expects " + std::to_string(fnDef->arity) + " to " + std::to_string(fnDef->maxArity) + " arguments, got " + std::to_string(argc) + ".");
                int padCount = fnDef->maxArity - static_cast<int>(argc);
                for (int j = 0; j < padCount; ++j) push(Value::none());
            }

            int reserveCount = fnDef->localCount - fnDef->maxArity;
            for (int j = 0; j < reserveCount; ++j) push(Value::none());

            newFrame.function = fnDef.get();
            newFrame.ip = 0;
            newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;
            if (method->hasCaptures()) {
                newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<Value>>>(
                    method->capturedEnv);
            }
            stack.erase(stack.begin() + newFrame.stackBase - 1);
            newFrame.stackBase--;
            frames.push_back(newFrame);
            return;
        }
        else if (method->isNative()) {
            // ★ 压入原生方法隔离池
            helpers::nativeSelfStack.push_back(Value(inst));
            helpers::nativeClassStack.push_back(Value(owningClass));

            std::vector<Value> args(argc);
            for (int j = argc - 1; j >= 0; --j) args[j] = pop();
            pop();
            Value result;
            try {
                auto& fn = std::any_cast<NativeCallable&>(method->nativeFn);
                result = fn(args);
            }
            catch (...) {
                helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
                throw;
            }
            helpers::nativeSelfStack.pop_back(); helpers::nativeClassStack.pop_back();
            push(result);
            return;
        }

        throw std::runtime_error("VM Error: Parent method '" + methodName +
            "' has no callable implementation.");
    }

    void VM::execAssertParamType(const Value& val, uint16_t typeIdx, uint16_t nameIdx) {
        const std::string& expectedType = std::get<std::string>(currentChunk().constants[typeIdx].data);

        if (!checkValueType(val, expectedType)) {
            const std::string& paramName = std::get<std::string>(currentChunk().constants[nameIdx].data);
            throw std::runtime_error("TypeError: Parameter '" + paramName +
                "' expected type '" + expectedType +
                "', got '" + getTypeName(val) + "'.");
        }
    }

    void VM::execAssertReturnType(const Value& val, uint16_t typeIdx) {
        const std::string& expectedType = std::get<std::string>(currentChunk().constants[typeIdx].data);

        if (!checkValueType(val, expectedType)) {
            throw std::runtime_error("TypeError: Function '" + frame().function->name +
                "' expected to return '" + expectedType +
                "', but returned '" + getTypeName(val) + "'.");
        }
    }

} // namespace jc
