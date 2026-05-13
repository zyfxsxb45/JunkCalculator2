//Vm.cpp
#include "VM.h"
#include "../modules/Module.h"
#include "BuiltinRegistry.h"
#include "../frontend/Highlight.h"
#include "../memory/GcHeap.h"
#include "EngineInterrupt.h"
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <filesystem>
#include <sstream>
namespace jc {

    // =======================================================
    // ★ 统一拦截与展开 Try-Catch 栈
    // =======================================================
    bool VM::handleExceptionUnwind(std::string& msg) {
        if (!exceptionHandlers.empty() && exceptionHandlers.back().frameIndex >= currentTargetFrameDepth) {
            auto handler = exceptionHandlers.back();
            exceptionHandlers.pop_back();

            // 剥除所有比 catch 更深的函数堆栈
            while (static_cast<int>(frames.size()) > handler.frameIndex + 1) frames.pop_back();

            // 回复当时的变量栈大小
            closeUpvalues(handler.stackSize);
            stack.resize(handler.stackSize);

            // 向前兼容清洗（万一由内部某处带上了 [Line，强行剥离保证纯净赋给 e 变量）
            if (msg.find("[Line ") == 0) {
                size_t c = msg.find("] ");
                if (c != std::string::npos) msg = msg.substr(c + 2);
            }

            push(Value(msg));
            frame().ip = handler.ip;
            return true; // 代表已经成功捕获，指示外层继续 run()
        }
        return false; // 无人捕获，通知外层构建 StackTrace 熔断！
    }
    // =======================================================
    // ★ 构建绚丽的 Traceback 调用栈回溯日志
    // =======================================================
    std::string VM::buildStackTrace(const std::string& errorMsg) {
        std::ostringstream oss;
        oss << errorMsg << "\n";

        // ★ 核心修复 1：不要使用 RESET 把外层的红洗没，使用专属于后续文本的颜色控制！
        oss << col(Ansi::GRAY) << "Traceback (most recent call last):\n";

        for (int i = static_cast<int>(frames.size()) - 1; i >= 0; --i) {
            const CallFrame& f = frames[i];

            int ip = f.ip - 1;
            if (ip < 0) ip = 0;

            const auto& lines = f.function->chunk.lines;
            int errLine = 0;
            if (!lines.empty()) {
                if (ip >= static_cast<int>(lines.size())) ip = static_cast<int>(lines.size()) - 1;
                errLine = lines[ip];
            }

            std::string fnName = f.function->name;
            if (fnName == "<script>" || fnName == "<eval>") {
                std::string sfile = f.function->sourceFile;
                if (sfile.empty()) sfile = "REPL";
                else {
                    try { sfile = std::filesystem::path(sfile).filename().string(); }
                    catch (...) {}
                }
                oss << "  at [Line " << errLine << "] in " << sfile << "\n";
            }
            else {
                oss << "  at [Line " << errLine << "] in " << fnName << "()\n";
            }
        }

        // ★ 核心修复 2：在所有堆栈打印完毕后，最后加一个 RESET 以确保后续无污染
        oss << col(Ansi::RESET);

        return oss.str();
    }

    std::string VM::getTypeName(const Value& val) {
        return val.typeName();
    }

    void VM::triggerParamTypeError(const Value& val, uint16_t typeIdx, uint16_t nameIdx) {
        const std::string& expectedType = currentChunk().constants[typeIdx].asString();
        const std::string& paramName = currentChunk().constants[nameIdx].asString();
        throw std::runtime_error("TypeError: Parameter '" + paramName +
            "' expected type '" + expectedType +
            "', got '" + getTypeName(val) + "'.");
    }
    void VM::triggerReturnTypeError(const Value& val, uint16_t typeIdx) {
        const std::string& expectedType = currentChunk().constants[typeIdx].asString();
        throw std::runtime_error("TypeError: Function '" + frame().function->name +
            "' expected to return '" + expectedType +
            "', but returned '" + getTypeName(val) + "'.");
    }

    bool VM::checkValueType(const Value& val, const std::string& typeStr) {
        if (typeStr == "any" || typeStr.empty()) return true;

        if (typeStr == "int") {
            if (val.isObjType(ObjType::BIGINT)) return true;
            if (val.isInt32()) return true;
            if (val.isDouble()) {
                double d = val.asDoubleRaw();
                return std::round(d) == d;
            }
            return false;
        }
        if (typeStr == "double" || typeStr == "float" || typeStr == "real" || typeStr == "number")
            return val.isNumber();
        if (typeStr == "string") return val.isString();
        if (typeStr == "matrix") return val.isObjType(ObjType::REAL_MATRIX) || val.isObjType(ObjType::COMPLEX_MATRIX) || val.isObjType(ObjType::STRING_MATRIX);
        if (typeStr == "list") return val.isObjType(ObjType::LIST);
        if (typeStr == "dict") return val.isObjType(ObjType::DICT);
        if (typeStr == "set") return val.isObjType(ObjType::SET);
        if (typeStr == "bool") return val.isBool();
        if (typeStr == "func" || typeStr == "function")
            return val.isFunctionClosure() || val.isString();
        if (typeStr == "complex") return val.isComplex();
        if (typeStr == "fraction") return val.isObjType(ObjType::FRACTION);
        if (typeStr == "class") return val.isClass();
        if (typeStr == "instance") return val.isInstance();
        if (typeStr == "symbolic"|| typeStr == "symbol" || typeStr == "expr") return val.isSymbolic();

        if (val.isInstance()) {
            auto inst = val.asInstance();
            auto c = inst->classDef;
            while (c) {
                if (c->name == typeStr) return true;
                c = c->parent;
            }
        }
        return false;
    }

    static ObjClosure* findDunder(
        const Value& val, const std::string& name)
    {
        if (!val.isInstance())
            return nullptr;
        auto inst = val.asInstance();
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
        auto inst = obj.asInstance();
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
            std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> captures = nullptr;
            if (method->hasCaptures())
                captures = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(method->capturedEnv);
            // ★ 无污染传参：直接送入 VM CallFrame 的 boundSelf
            return callVMFunction(method->compiledFnIndex, args, captures, Value(inst), Value(inst->classDef));
        }
        else {
            throw std::runtime_error("VM Error: No callable dunder '" + name + "'.");
        }
    }

    void VM::closeUpvalues(int lastStackIndex) {
        for (auto it = openUpvalues.begin(); it != openUpvalues.end(); ) {
            if ((*it)->stackIndex >= lastStackIndex) {
                (*it)->closed = *((*it)->location);
                (*it)->location = &(*it)->closed;
                it = openUpvalues.erase(it);
            } else {
                ++it;
            }
        }
    }

    VM::VM() {
        activeVM = this;
        stack.reserve(MAX_STACK); // ★ 保证栈指针绝对稳定，支撑 Upvalue 机制

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

        registerBuiltin("__vm_delete__", [this](const std::vector<Value>& args) -> Value {
            if (args.size() != 1 || !args[0].isString())
                throw std::runtime_error("VM Error: __vm_delete__ expects a string variable name.");
            const std::string& name = args[0].asString();

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

    Value VM::execute(const Chunk& c) {
        activeVM = this;
        auto mainFn = std::make_shared<CompiledFunction>();
        mainFn->name = "<script>";
        mainFn->chunk = c;
        closeUpvalues(0);
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

    Value VM::callVMFunction(int fnIdx, const std::vector<Value>& args,
        std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> upvalues,
        Value boundSelf, Value boundClass) {
        if (fnIdx < 0 || fnIdx >= static_cast<int>(compiledFunctions.size()))
            throw std::runtime_error("VM Error: Invalid function index in callback.");
        auto& fn = compiledFunctions[fnIdx];
        int savedTargetFrameDepth = currentTargetFrameDepth;
        auto savedRefWritebacks = pendingRefWritebacks;
        pendingRefWritebacks.clear();

        for (const auto& arg : args)
            push(arg);

        uint8_t argc = static_cast<uint8_t>(args.size());
        if (fn->hasRestParam) {
            int fixedMax = fn->maxArity - 1;
            if (static_cast<int>(argc) < fn->arity) {
                throw std::runtime_error("VM Error: '" + fn->name + "' requires at least " + std::to_string(fn->arity) + " arguments.");
            }

            ObjList* restList = GcHeap::get().allocate<ObjList>();
            if (static_cast<int>(argc) > fixedMax) {
                int restCount = static_cast<int>(argc) - fixedMax;
                std::vector<Value> tempValues(restCount);
                for (int j = 0; j < restCount; j++) {
                    tempValues[restCount - 1 - j] = pop();
                }
                for (int j = 0; j < restCount; j++) {
                    restList->vec.push_back(tempValues[j]);
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
        catch (const StackTracedException&) {
            currentTargetFrameDepth = savedTargetFrameDepth;
            pendingRefWritebacks = savedRefWritebacks;
            frames.resize(boundary);
            closeUpvalues(newFrame.stackBase);
            stack.resize(newFrame.stackBase);
            throw;
        }
        catch (...) {
            currentTargetFrameDepth = savedTargetFrameDepth;
            pendingRefWritebacks = savedRefWritebacks;
            frames.resize(boundary);
            closeUpvalues(newFrame.stackBase);
            stack.resize(newFrame.stackBase);
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

    // =======================================================
    // 返回当前执行帧指令对应的源码行号（仅用于断点调试显示）
    // =======================================================
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

    Value VM::run(int targetFrameDepth) {
        currentTargetFrameDepth = targetFrameDepth;

        while (true) {
            
            // ═══ GC 自动触发探针与中断探针 ═══
            if (++gcInstructionCounter_ >= 2048) {
                gcInstructionCounter_ = 0;
                jc::checkInterrupt();
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
                case OpCode::OP_TRUE:  push(Value(true)); break;
                case OpCode::OP_FALSE: push(Value(false)); break;
                case OpCode::OP_POP:   pop(); break;

                case OpCode::OP_GET_SELF: {
                    if (frame().selfContext.isNone()) throw std::runtime_error("VM Error: 'self' accessed outside of context.");
                    push(frame().selfContext);
                    break;
                }

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
                case OpCode::OP_NOT: { push(Value(!pop().truthy())); break; }

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
                    if (d) { push(Value(callDunder(a, "__eq__", { b }).truthy())); break; }
                    push(Value(Value::equals(a, b)));
                    break;
                }
                case OpCode::OP_NOT_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__neq__");
                    if (d) { push(Value(callDunder(a, "__neq__", { b }).truthy())); break; }
                    d = findDunder(a, "__eq__");
                    if (d) { push(Value(!callDunder(a, "__eq__", { b }).truthy())); break; }
                    push(Value(!Value::equals(a, b)));
                    break;
                }
                case OpCode::OP_LESS: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__lt__");
                    if (d) { push(Value(callDunder(a, "__lt__", { b }).truthy())); break; }
                    if ((a.isBigInt() || a.isInt32()) && (b.isBigInt() || b.isInt32()))
                        push(Value(a.asBigInt() < b.asBigInt()));
                    else if (a.isObjType(ObjType::FRACTION) && b.isObjType(ObjType::FRACTION))
                        push(Value(static_cast<ObjFraction*>(a.asObj())->frac < static_cast<ObjFraction*>(b.asObj())->frac));
                    else if (a.isString() && b.isString())
                        push(Value(a.asString() < b.asString()));
                    else {
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value(da < db));
                    }
                    break;
                }
                case OpCode::OP_LESS_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__le__");
                    if (d) { push(Value(callDunder(a, "__le__", { b }).truthy())); break; }
                    if ((a.isBigInt() || a.isInt32()) && (b.isBigInt() || b.isInt32()))
                        push(Value(a.asBigInt() <= b.asBigInt()));
                    else if (a.isObjType(ObjType::FRACTION) && b.isObjType(ObjType::FRACTION))
                        push(Value(static_cast<ObjFraction*>(a.asObj())->frac <= static_cast<ObjFraction*>(b.asObj())->frac));
                    else if (a.isString() && b.isString())
                        push(Value(a.asString() <= b.asString()));
                    else {
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value(da <= db));
                    }
                    break;
                }
                case OpCode::OP_GREATER: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__gt__");
                    if (d) { push(Value(callDunder(a, "__gt__", { b }).truthy())); break; }
                    if ((a.isBigInt() || a.isInt32()) && (b.isBigInt() || b.isInt32()))
                        push(Value(a.asBigInt() > b.asBigInt()));
                    else if (a.isObjType(ObjType::FRACTION) && b.isObjType(ObjType::FRACTION))
                        push(Value(static_cast<ObjFraction*>(a.asObj())->frac > static_cast<ObjFraction*>(b.asObj())->frac));
                    else if (a.isString() && b.isString())
                        push(Value(a.asString() > b.asString()));
                    else {
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value(da > db));
                    }
                    break;
                }
                case OpCode::OP_GREATER_EQUAL: {
                    Value b = pop(), a = pop();
                    auto d = findDunder(a, "__ge__");
                    if (d) { push(Value(callDunder(a, "__ge__", { b }).truthy())); break; }
                    if ((a.isBigInt() || a.isInt32()) && (b.isBigInt() || b.isInt32()))
                        push(Value(a.asBigInt() >= b.asBigInt()));
                    else if (a.isObjType(ObjType::FRACTION) && b.isObjType(ObjType::FRACTION))
                        push(Value(static_cast<ObjFraction*>(a.asObj())->frac >= static_cast<ObjFraction*>(b.asObj())->frac));
                    else if (a.isString() && b.isString())
                        push(Value(a.asString() >= b.asString()));
                    else {
                        double da = a.asDouble(), db = b.asDouble();
                        push(Value(da >= db));
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
                    std::string name = currentChunk().constants[idx].asString();

                    // ★ 虚拟机级别拦截：遇到 '__class__'，直接去它该在的物理寄存器里拿！
                    if (name == "__class__") {
                        if (frame().classContext.isNone()) throw std::runtime_error("VM Error: '__class__' accessed outside of context.");
                        push(frame().classContext);
                        break;
                    }

                    auto it = globals.find(name);
                    if (it != globals.end()) {
                        push(it->second);
                    }
                    else if (nativeBuiltins.count(name)) {
                        auto closure = GcHeap::get().allocate<ObjClosure>(
                            std::vector<std::string>{},
                            std::vector<bool>{},
                            name,
                            nullptr
                        );
                        closure->nativeFn = std::make_any<NativeCallable>(nativeBuiltins[name]);

                        // ★ 补上参数元数据，让 map/filter/reduce 等高阶函数能正确识别
                        auto ait = builtinArity.find(name);
                        if (ait != builtinArity.end() && !ait->second.empty()) {
                            int maxA = *ait->second.rbegin();
                            int minA = *ait->second.begin();
                            for (int j = 0; j < maxA; ++j) {
                                closure->paramNames.push_back("_" + std::to_string(j));
                                closure->isRef.push_back(false);
                            }
                            // 超出最小参数数量的部分视为有默认值
                            for (int j = minA; j < maxA; ++j) {
                                closure->defaultValues.push_back(Value::none());
                            }
                        }

                        push(Value(closure));
                    }
                    else {
                        throw std::runtime_error("VM Error: Undefined variable '" + name + "'.");
                    }
                    break;
                }
                case OpCode::OP_SET_GLOBAL:
                case OpCode::OP_SET_GLOBAL_REF: {
                    uint16_t idx = readShort();
                    std::string name = currentChunk().constants[idx].asString();

                    // ★ 关键字保护：绝不许改写上下文关键字 !
                    if (name == "__class__")
                        throw std::runtime_error("Syntax Error: cannot override context keyword '" + name + "'.");

                    if (constGlobals.count(name))
                        throw std::runtime_error("Runtime Error: Cannot modify const variable '" + name + "'.");

                    if (op == OpCode::OP_SET_GLOBAL_REF) {
                        if (globals.find(name) == globals.end() && nativeBuiltins.find(name) == nativeBuiltins.end()) {
                            throw std::runtime_error("Runtime Error: Undefined variable '" + name + "'.");
                        }
                    }

                    // ★ 检查是否与内建函数 arity 冲突
                    Value& val = peek(0);
                    if (val.isFunctionClosure()) {
                        auto nit = nativeBuiltins.find(name);
                        if (nit != nativeBuiltins.end()) {
                            auto ait = builtinArity.find(name);
                            auto closure = val.asFunction();

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
                    std::string name = currentChunk().constants[idx].asString();
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
                    if (!peek(0).truthy()) frame().ip += offset;
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

                    std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> captures;
                    if (!fn->upvalues.empty()) {
                        captures = std::make_shared<std::vector<std::shared_ptr<UpVal>>>();
                        for (auto& uv : fn->upvalues) {
                            if (uv.isRef) {
                                // ★ 按引用捕获 (Open Upvalue)
                                if (uv.isLocal) {
                                    int captureIdx = frame().stackBase + uv.index;
                                    std::shared_ptr<UpVal> upval = nullptr;
                                    for (auto& openUv : openUpvalues) {
                                        if (openUv->stackIndex == captureIdx) {
                                            upval = openUv;
                                            break;
                                        }
                                    }
                                    if (!upval) {
                                        upval = std::make_shared<UpVal>();
                                        upval->location = &stack[captureIdx];
                                        upval->stackIndex = captureIdx;
                                        openUpvalues.push_back(upval);
                                    }
                                    captures->push_back(upval);
                                }
                                else {
                                    if (frame().upvalues && uv.index < static_cast<int>(frame().upvalues->size()))
                                        captures->push_back((*frame().upvalues)[uv.index]);
                                    else {
                                        auto dummy = std::make_shared<UpVal>();
                                        dummy->closed = Value::none();
                                        dummy->location = &dummy->closed;
                                        captures->push_back(dummy);
                                    }
                                }
                            } else {
                                // ★ 默认按值捕获 (立即 Closed Upvalue)
                                auto dummy = std::make_shared<UpVal>();
                                if (uv.isGlobal) {
                                    auto it = globals.find(uv.name);
                                    if (it != globals.end()) dummy->closed = it->second;
                                    else dummy->closed = Value::none();
                                } else if (uv.isLocal) {
                                    int captureIdx = frame().stackBase + uv.index;
                                    dummy->closed = stack[captureIdx];
                                } else {
                                    if (frame().upvalues && uv.index < static_cast<int>(frame().upvalues->size()))
                                        dummy->closed = *((*frame().upvalues)[uv.index]->location);
                                    else
                                        dummy->closed = Value::none();
                                }
                                dummy->location = &dummy->closed;
                                captures->push_back(dummy);
                            }
                        }
                    }

                    auto closure = GcHeap::get().allocate<ObjClosure>(
                        std::vector<std::string>{},
                        std::vector<bool>{},
                        fn->name,
                        nullptr
                    );

                    closure->compiledFnIndex = idx;
                    if (captures) {
                        closure->capturedEnv = std::make_any<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(captures);
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
                    push(*((*frame().upvalues)[idx]->location));
                    break;
                }

                case OpCode::OP_SET_UPVALUE: {
                    uint16_t idx = readShort();
                    if (!frame().upvalues || idx >= frame().upvalues->size())
                        throw std::runtime_error("VM Error: Invalid upvalue index " +
                            std::to_string(idx) + ".");
                    *((*frame().upvalues)[idx]->location) = peek(0);
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
                    std::string field = currentChunk().constants[nameIdx].asString();

                    Value selfVal = pop();

                    if (!selfVal.isInstance())
                        throw std::runtime_error("VM Error: 'super' requires an instance context.");

                    auto inst = selfVal.asInstance();

                    Value classVal = frame().classContext;
                    if (!classVal.isClass())
                        throw std::runtime_error("VM Error: 'super' requires class context.");

                    auto currentClass = static_cast<ObjClass*>(classVal.asObj());
                    auto parentClass = currentClass->parent;
                    if (!parentClass)
                        throw std::runtime_error("VM Error: No parent class.");

                    ObjClosure* rawMethod = nullptr;
                    ObjClass* ownerClass = nullptr;
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
                    auto bound = GcHeap::get().allocate<ObjClosure>(
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
                                std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> captures = nullptr;
                                if (capturedMethod->hasCaptures())
                                    captures = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(
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
                            std::string name = currentChunk().constants[sourceRef].asString();
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
                                *((*frame().upvalues)[sourceRef]->location) = modifiedVal;
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
                    if (v.isString()) {
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
                        if (v.isString())
                            parts[j] = v.asString();
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
                    if (rhs.isObjType(ObjType::REAL_MATRIX)) {
                        for (double d : static_cast<ObjRealMatrix*>(rhs.asObj())->mat.rawData())
                            elements.push_back(Value(d));
                    }
                    else if (rhs.isObjType(ObjType::COMPLEX_MATRIX)) {
                        for (const auto& c : static_cast<ObjComplexMatrix*>(rhs.asObj())->mat.rawData())
                            elements.push_back(Value(c));
                    }
                    else if (rhs.isObjType(ObjType::LIST)) {
                        for (const auto& e : static_cast<ObjList*>(rhs.asObj())->vec)
                            elements.push_back(e);
                    }
                    else if (rhs.isObjType(ObjType::STRING_MATRIX)) {
                        for (const auto& s : static_cast<ObjStringMatrix*>(rhs.asObj())->mat.rawData())
                            elements.push_back(Value(s));
                    }
                    else if (rhs.isString()) {
                        for (char c : rhs.asString())
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
                    if (errVal.isString())
                        msg = errVal.asString();
                    else {
                        std::ostringstream oss; oss << errVal;
                        msg = oss.str();
                    }
                    throw ErrorSignal(msg);
                } 

                case OpCode::OP_BUILD_DICT: {
                    uint16_t count = readShort();
                    ObjDict* d = GcHeap::get().allocate<ObjDict>();
                    std::vector<std::pair<Value, Value>> pairs(count);
                    for (int j = count - 1; j >= 0; --j) {
                        Value val = pop();
                        Value key = pop();
                        pairs[j] = { key, val };
                    }
                    for (auto& [k, v] : pairs) {
                        auto it = d->keyMap.find(k);
                        if (it != d->keyMap.end()) {
                            d->elements[it->second].second = v;
                        } else {
                            d->keyMap[k] = d->elements.size();
                            d->elements.push_back({k, v});
                        }
                    }
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
                    ObjList* elements = GcHeap::get().allocate<ObjList>();
                    if (iterable.isObjType(ObjType::REAL_MATRIX)) {
                        const auto& m = static_cast<ObjRealMatrix*>(iterable.asObj())->mat;
                        if (m.getRows() == 1) {
                            for (int j = 0; j < m.getCols(); ++j)
                                elements->vec.push_back(Value(m(0, j)));
                        }
                        else if (m.getCols() == 1) {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m(ii, 0)));
                        }
                        else {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m.getRow(ii)));
                        }
                    }
                    else if (iterable.isObjType(ObjType::STRING_MATRIX)) {
                        const auto& m = static_cast<ObjStringMatrix*>(iterable.asObj())->mat;
                        if (m.getRows() == 1) {
                            for (int j = 0; j < m.getCols(); ++j)
                                elements->vec.push_back(Value(m(0, j)));
                        }
                        else if (m.getCols() == 1) {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m(ii, 0)));
                        }
                        else {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m.getRow(ii)));
                        }
                    }
                    else if (iterable.isObjType(ObjType::COMPLEX_MATRIX)) {
                        const auto& m = static_cast<ObjComplexMatrix*>(iterable.asObj())->mat;
                        if (m.getRows() == 1) {
                            for (int j = 0; j < m.getCols(); ++j)
                                elements->vec.push_back(Value(m(0, j)));
                        }
                        else if (m.getCols() == 1) {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m(ii, 0)));
                        }
                        else {
                            for (int ii = 0; ii < m.getRows(); ++ii)
                                elements->vec.push_back(Value(m.getRow(ii)));
                        }
                    }
                    else if (iterable.isObjType(ObjType::LIST)) {
                        elements->vec = static_cast<ObjList*>(iterable.asObj())->vec;
                    }
                    else if (iterable.isString()) {
                        for (char c : iterable.asString())
                            elements->vec.push_back(Value(std::string(1, c)));
                    }
                    else if (iterable.isObjType(ObjType::DICT)) {
                        const auto* d = static_cast<ObjDict*>(iterable.asObj());
                        if (destructFlag) {
                            for (const auto& [key, val] : d->elements) {
                                ObjList* pair = GcHeap::get().allocate<ObjList>();
                                pair->vec.push_back(key);
                                pair->vec.push_back(val);
                                pair->is_frozen = true;
                                elements->vec.push_back(Value(pair));
                            }
                        }
                        else {
                            for (const auto& [key, val] : d->elements) {
                                elements->vec.push_back(key);
                            }
                        }
                    }
                    else if (iterable.isObjType(ObjType::SET)) {
                        const auto* s = static_cast<ObjSet*>(iterable.asObj());
                        for (const auto& val : s->elements) {
                            elements->vec.push_back(val);
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
                    const auto& elems = static_cast<ObjList*>(peek(1).asObj())->vec;
                    int i = static_cast<int>(idx);
                    if (i >= static_cast<int>(elems.size())) {
                        frame().ip += offset;
                    }
                    else {
                        Value elem = elems[i];
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
                    std::string spec = currentChunk().constants[specIdx].asString();
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
                    push(Value(GcHeap::get().allocate<ObjList>()));
                    break;
                }

                case OpCode::OP_LIST_APPEND: {
                    uint16_t depth = readShort();
                    Value elem = pop();
                    int listIdx = static_cast<int>(stack.size()) - 1 - static_cast<int>(depth);
                    if (listIdx >= 0 && stack[listIdx].isObjType(ObjType::LIST)) {
                        static_cast<ObjList*>(stack[listIdx].asObj())->vec.push_back(elem);
                    }
                    else {
                        throw std::runtime_error("VM Error: LIST_APPEND target not found at depth " +
                            std::to_string(depth));
                    }
                    break;
                }

                case OpCode::OP_LIST_COMP_END: {
                    Value arg = pop();
                    if (!arg.isObjType(ObjType::LIST)) {
                        push(arg);
                        break;
                    }
                    auto l = static_cast<ObjList*>(arg.asObj());
                    if (l->vec.empty()) {
                        push(Value(RealMatrix(1, 0)));
                        break;
                    }
                    
                    bool hasComplex = false;
                    bool hasString = false;
                    bool hasOther = false;
                    bool hasSubMatrix = false;

                    auto canBeMatrixElement = [](const Value& v) -> bool {
                        return v.isNumber() ||
                            v.isObjType(ObjType::BIGINT) ||
                            v.isObjType(ObjType::FRACTION) ||
                            v.isObjType(ObjType::BASENUM) ||
                            v.isObjType(ObjType::COMPLEX) ||
                            v.isString() ||
                            v.isObjType(ObjType::REAL_MATRIX) ||
                            v.isObjType(ObjType::COMPLEX_MATRIX) ||
                            v.isObjType(ObjType::STRING_MATRIX);
                    };

                    for (const auto& v : l->vec) {
                        if (v.isObjType(ObjType::COMPLEX) || v.isObjType(ObjType::COMPLEX_MATRIX)) hasComplex = true;
                        if (v.isString() || v.isObjType(ObjType::STRING_MATRIX)) hasString = true;
                        if (v.isObjType(ObjType::REAL_MATRIX) || v.isObjType(ObjType::COMPLEX_MATRIX) || v.isObjType(ObjType::STRING_MATRIX)) hasSubMatrix = true;
                        if (!canBeMatrixElement(v)) hasOther = true;
                    }

                    if (hasOther) {
                        push(arg); // 保持为 List
                        break;
                    }

                    int total = static_cast<int>(l->vec.size());

                    if (hasSubMatrix) {
                        auto extractCell = [&](Value& cell) {
                            if (!cell.isObjType(ObjType::REAL_MATRIX) &&
                                !cell.isObjType(ObjType::COMPLEX_MATRIX) &&
                                !cell.isObjType(ObjType::STRING_MATRIX)) {
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
                                if (cell.isObjType(ObjType::REAL_MATRIX)) {
                                    const auto& m = static_cast<ObjRealMatrix*>(cell.asObj())->mat;
                                    std::vector<std::string> flat;
                                    for (int i = 0; i < m.getRows(); ++i)
                                        for (int j = 0; j < m.getCols(); ++j) {
                                            std::ostringstream oss; oss << Value(m(i, j));
                                            flat.push_back(oss.str());
                                        }
                                    cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                                }
                                else if (cell.isObjType(ObjType::COMPLEX_MATRIX)) {
                                    const auto& m = static_cast<ObjComplexMatrix*>(cell.asObj())->mat;
                                    std::vector<std::string> flat;
                                    for (int i = 0; i < m.getRows(); ++i)
                                        for (int j = 0; j < m.getCols(); ++j) {
                                            std::ostringstream oss; oss << Value(m(i, j));
                                            flat.push_back(oss.str());
                                        }
                                    cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                                }
                            }
                            else if (hasComplex && cell.isObjType(ObjType::REAL_MATRIX)) {
                                cell = Value(cell.asComplexMatrix());
                            }
                        };

                        try {
                            Value rowResult = Value::none();
                            for (int j = 0; j < total; ++j) {
                                Value cell = l->vec[j];
                                extractCell(cell);
                                if (rowResult.isNone()) {
                                    rowResult = cell;
                                }
                                else {
                                    if (hasString)
                                        rowResult = Value(static_cast<ObjStringMatrix*>(rowResult.asObj())->mat
                                            .integR(static_cast<ObjStringMatrix*>(cell.asObj())->mat));
                                    else if (hasComplex)
                                        rowResult = Value(static_cast<ObjComplexMatrix*>(rowResult.asObj())->mat
                                            .integR(static_cast<ObjComplexMatrix*>(cell.asObj())->mat));
                                    else
                                        rowResult = Value(static_cast<ObjRealMatrix*>(rowResult.asObj())->mat
                                            .integR(static_cast<ObjRealMatrix*>(cell.asObj())->mat));
                                }
                            }
                            push(rowResult);
                        }
                        catch (...) {
                            throw std::runtime_error("VM Error: Dimension mismatch during list comprehension matrix concatenation.");
                        }
                    }
                    else if (hasString) {
                        std::vector<std::string> flat(total);
                        for (int ii = 0; ii < total; ++ii) {
                            const Value& v = l->vec[ii];
                            if (v.isString()) flat[ii] = v.asString();
                            else { std::ostringstream oss; oss << v; flat[ii] = oss.str(); }
                        }
                        push(Value(StringMatrix(1, total, flat)));
                    }
                    else if (hasComplex) {
                        std::vector<Complex> flat(total);
                        for (int ii = 0; ii < total; ++ii) flat[ii] = l->vec[ii].asComplex();
                        push(Value(ComplexMatrix(1, total, flat)));
                    }
                    else {
                        std::vector<double> flat(total);
                        for (int ii = 0; ii < total; ++ii) flat[ii] = l->vec[ii].asDouble();
                        push(Value(RealMatrix(1, total, flat)));
                    }
                    break;
                }

                case OpCode::OP_IMPORT: {
                    Value pathVal = pop();
                    if (!pathVal.isString())
                        throw std::runtime_error("VM Error: import requires a string path.");
                    std::string name = pathVal.asString();

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
                    std::string name = currentChunk().constants[nameIdx].asString();
                    auto cls = GcHeap::get().allocate<ObjClass>();
                    cls->name = name;
                    push(Value(cls));
                    break;
                }

                case OpCode::OP_METHOD: {
                    uint16_t nameIdx = readShort();
                    std::string methodName = currentChunk().constants[nameIdx].asString();
                    Value closureVal = pop();
                    Value& classVal = peek(0);

                    if (!classVal.isClass())
                        throw std::runtime_error("VM Error: OP_METHOD requires a class on stack.");

                    auto cls = static_cast<ObjClass*>(classVal.asObj());

                    if (closureVal.isFunctionClosure()) {
                        auto fc = closureVal.asFunction();
                        cls->methods[methodName] = fc;
                        break;
                    }

                    if (closureVal.isString()) {
                        const std::string& tag = closureVal.asString();
                        auto fc = GcHeap::get().allocate<ObjClosure>(
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
                    if (!superClass.isClass() || !subClass.isClass())
                        throw std::runtime_error("VM Error: Inheritance requires two classes.");
                    auto sub = static_cast<ObjClass*>(subClass.asObj());
                    auto sup = static_cast<ObjClass*>(superClass.asObj());
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
                    std::string field = currentChunk().constants[nameIdx].asString();
                    Value obj = pop();

                    if (obj.isInstance()) {
                        auto inst = obj.asInstance();

                        // 1. 字段查找 — 不变
                        if (inst->fields) {
                            auto it = inst->fields->keyMap.find(Value(field));
                            if (it != inst->fields->keyMap.end()) {
                                push(inst->fields->elements[it->second].second);
                                break;
                            }
                        }

                        // 2. 方法查找
                        ObjClosure* rawMethod = nullptr;
                        ObjClass* ownerClass = nullptr;
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
                        auto bound = GcHeap::get().allocate<ObjClosure>(
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
                                    std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> captures = nullptr;
                                    if (capturedMethod->hasCaptures())
                                        captures = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(
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
                    else if (obj.isObjType(ObjType::DICT)) {
                        auto d = static_cast<ObjDict*>(obj.asObj());
                        auto it = d->keyMap.find(Value(field));
                        if (it == d->keyMap.end()) throw std::runtime_error("VM Error: Key '" + field + "' not found.");
                        push(d->elements[it->second].second);
                    }
                    else {
                        throw std::runtime_error("VM Error: Cannot access property on this type.");
                    }
                    break;
                }

                case OpCode::OP_SET_PROPERTY: {
                    uint16_t nameIdx = readShort();
                    std::string field = currentChunk().constants[nameIdx].asString();
                    Value val = pop();
                    Value obj = pop();

                    if (obj.isInstance()) {
                        auto inst = obj.asInstance();
                        if (!inst->fields) inst->fields = GcHeap::get().allocate<ObjDict>();
                        Value key(field);
                        auto it = inst->fields->keyMap.find(key);
                        if (it != inst->fields->keyMap.end()) {
                            inst->fields->elements[it->second].second = val;
                        } else {
                            inst->fields->keyMap[key] = inst->fields->elements.size();
                            inst->fields->elements.push_back({key, val});
                        }
                        push(val);
                        push(obj);
                    }
                    else if (obj.isObjType(ObjType::DICT)) {
                        auto d = static_cast<ObjDict*>(obj.asObj());
                        Value key(field);
                        auto it = d->keyMap.find(key);
                        if (it != d->keyMap.end()) {
                            d->elements[it->second].second = val;
                        } else {
                            d->keyMap[key] = d->elements.size();
                            d->elements.push_back({key, val});
                        }
                        push(val);
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
            // ★ 异常捕获与 Traceback 调用栈回溯 (Stack Tracing)
            // =======================================================
            catch (const EngineInterruptError&) {
                throw; // 强行中断，无视 try-catch 拦截，不生成 Traceback
            }
            catch (const StackTracedException& ex) {
                std::string msg = ex.rawMessage;
                if (handleExceptionUnwind(msg)) continue;
                throw;
            }
            catch (const ErrorSignal& sig) {
                std::string msg = sig.message;
                if (handleExceptionUnwind(msg)) continue;
                throw StackTracedException(msg, buildStackTrace(msg));
            }
            catch (const std::exception& ex) {
                std::string msg = ex.what();
                if (handleExceptionUnwind(msg)) continue;
                throw StackTracedException(msg, buildStackTrace(msg));
            }
            catch (...) {
                std::string msg = "Unknown VM Error";
                if (handleExceptionUnwind(msg)) continue;
                throw StackTracedException(msg, buildStackTrace(msg));
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

    void VM::markClosure(const ObjClosure* cl,
        std::unordered_set<const void*>& marked)
    {
        if (!cl) return;
        const_cast<ObjClosure*>(cl)->isMarked = true;
        if (!cl->capturedEnv.has_value()) return;
        try {
            auto env = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(cl->capturedEnv);
            if (!env) return;
            for (const auto& uv : *env) {
                if (uv && uv->location) {
                    markValue(*(uv->location), marked);
                }
            }
        }
        catch (...) {}
    }

    void VM::markClassDef(const ObjClass* cls,
        std::unordered_set<const void*>& marked)
    {
        if (!cls) return;
        const void* id = cls;
        if (marked.count(id)) return;    // 防止继承链递归循环
        marked.insert(id);
        const_cast<ObjClass*>(cls)->isMarked = true;

        for (const auto& [name, method] : cls->methods) {
            if (method) markClosure(method, marked);
        }
        markClassDef(cls->parent, marked);
    }

    void VM::markValue(const Value& val,
        std::unordered_set<const void*>& marked)
    {
        if (!val.isObj()) return;
        Obj* obj = val.asObj();
        if (marked.count(obj)) return;
        marked.insert(obj);
        obj->isMarked = true;

        switch (obj->type) {
            case ObjType::LIST: {
                for (const auto& elem : static_cast<ObjList*>(obj)->vec) markValue(elem, marked);
                break;
            }
            case ObjType::DICT: {
                for (const auto& [key, v] : static_cast<ObjDict*>(obj)->elements) {
                    markValue(key, marked); markValue(v, marked);
                }
                break;
            }
            case ObjType::SET: {
                for (const auto& elem : static_cast<ObjSet*>(obj)->elements) markValue(elem, marked);
                break;
            }
            case ObjType::INSTANCE: {
                auto inst = static_cast<ObjInstance*>(obj);
                if (inst->fields) markValue(Value(inst->fields), marked);
                if (inst->classDef) markValue(Value(inst->classDef), marked);
                break;
            }
            case ObjType::CLOSURE: {
                markClosure(static_cast<ObjClosure*>(obj), marked);
                break;
            }
            case ObjType::CLASS: {
                markClassDef(static_cast<ObjClass*>(obj), marked);
                break;
            }
            case ObjType::SUPER_PROXY: {
                auto sp = static_cast<ObjSuper*>(obj);
                if (sp->instance) markValue(Value(sp->instance), marked);
                if (sp->parentClass) markValue(Value(sp->parentClass), marked);
                break;
            }
            default: break;
        }
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
                for (const auto& uv : *f.upvalues) {
                    if (uv && uv->location) markValue(*(uv->location), marked);
                }
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
        GcHeap::get().sweep();
    }

    int VM::runGC() {
        std::unordered_set<const void*> marked;
        for (const auto& [name, val] : globals)  markValue(val, marked);
        for (const auto& val : stack)            markValue(val, marked);
        for (const auto& f : frames) {
            if (f.upvalues) {
                for (const auto& uv : *f.upvalues) {
                    if (uv && uv->location) markValue(*(uv->location), marked);
                }
            }
            // ★ 防止手动 gc() 触发对象丢失
            markValue(f.selfContext, marked);
            markValue(f.classContext, marked);
        }
        for (const auto& fn : compiledFunctions)
            for (const auto& c : fn->chunk.constants) markValue(c, marked);

        // ★ C++ 原生堆栈手动同步
        for (const auto& val : helpers::nativeSelfStack) markValue(val, marked);
        for (const auto& val : helpers::nativeClassStack) markValue(val, marked);

        return GcHeap::get().sweep();
    }

    void VM::execCall(uint8_t argc) {
        Value callee = stack[stack.size() - 1 - argc];
        pendingRefWritebacks.clear();

        // ======== [1] 字符串动态调用 (晚绑定) ========
        if (callee.isString()) {
            const std::string& tag = callee.asString();
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
        if (callee.isClass()) {
            auto cls = static_cast<ObjClass*>(callee.asObj());
            auto instance = GcHeap::get().allocate<ObjInstance>();
            instance->classDef = cls;

            ObjClosure* initMethod = nullptr;
            ObjClass* initOwner = nullptr;
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
                        newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(
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
        if (callee.isFunctionClosure()) {
            auto closure = callee.asFunction();

            if (closure->isBytecode()) {
                auto& fnDef = compiledFunctions[closure->compiledFnIndex];

                if (fnDef->hasRestParam) {
                    int fixedMax = fnDef->maxArity - 1;
                    if (static_cast<int>(argc) < fnDef->arity) {
                        throw std::runtime_error("VM Error: '" + fnDef->name + "' requires at least " + std::to_string(fnDef->arity) + " arguments.");
                    }

                    ObjList* restList = GcHeap::get().allocate<ObjList>();
                    if (static_cast<int>(argc) > fixedMax) {
                        int restCount = static_cast<int>(argc) - fixedMax;
                        std::vector<Value> tempValues(restCount);
                        for (int j = 0; j < restCount; j++) {
                            tempValues[restCount - 1 - j] = pop();
                        }
                        for (int j = 0; j < restCount; j++) {
                            restList->vec.push_back(tempValues[j]);
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
                    newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(closure->capturedEnv);
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
        if (callee.isString()) {
            const std::string& tag = callee.asString();

            if (tag.size() >= 5 && tag.substr(0, 5) == "__fn:") {
                int fnIdx = std::stoi(tag.substr(5));
                auto& fn = compiledFunctions[fnIdx];

                if (fn->hasRestParam) {
                    int fixedMax = fn->maxArity - 1;
                    if (static_cast<int>(argc) < fn->arity) {
                        throw std::runtime_error("VM Error: '" + fn->name + "' requires at least " + std::to_string(fn->arity) + " arguments.");
                    }

                    ObjList* restList = GcHeap::get().allocate<ObjList>();
                    if (static_cast<int>(argc) > fixedMax) {
                        int restCount = static_cast<int>(argc) - fixedMax;
                        std::vector<Value> tempValues(restCount);
                        for (int j = 0; j < restCount; j++) {
                            tempValues[restCount - 1 - j] = pop();
                        }
                        for (int j = 0; j < restCount; j++) {
                            restList->vec.push_back(tempValues[j]);
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
            if (calleeVal.isString())
                desc = calleeVal.asString();
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

            if (obj.isObjType(ObjType::DICT)) {
                auto dict = static_cast<ObjDict*>(obj.asObj());
                auto it = dict->keyMap.find(idx);
                if (it == dict->keyMap.end()) {
                    std::string keyStr;
                    if (idx.isString()) keyStr = idx.asString();
                    else { std::ostringstream oss; oss << idx; keyStr = oss.str(); }
                    throw std::runtime_error("VM Error: Key '" + keyStr + "' not found.");
                }
                push(dict->elements[it->second].second);
                return;
            }

            // ── Instance (__getitem__) ──
            if (obj.isInstance()) {
                auto inst = obj.asInstance();
                auto c = inst->classDef;
                ObjClosure* getitemMethod = nullptr;
                while (c) {
                    auto it = c->methods.find("__getitem__");
                    if (it != c->methods.end()) {
                        getitemMethod = it->second;
                        break;
                    }
                    c = c->parent;
                }
                if (getitemMethod) {
                    if (getitemMethod->isBytecode()) {
                        auto& fnDef = compiledFunctions[getitemMethod->compiledFnIndex];
                        push(idx);
                        int padCount = fnDef->maxArity - 1;
                        for (int j = 0; j < padCount; ++j) push(Value::none());
                        int reserveCount = fnDef->localCount - fnDef->maxArity;
                        for (int j = 0; j < reserveCount; ++j) push(Value::none());

                        CallFrame newFrame;
                        newFrame.function = fnDef.get();
                        newFrame.ip = 0;
                        newFrame.stackBase = static_cast<int>(stack.size()) - fnDef->localCount;

                        if (getitemMethod->hasCaptures()) {
                            newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(getitemMethod->capturedEnv);
                        }
                        newFrame.selfContext = Value(inst);
                        newFrame.classContext = Value(inst->classDef);
                        frames.push_back(newFrame);
                        return; // ★ 绝对返回防线
                    }
                    else if (getitemMethod->isNative()) {
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
                        return; // ★ 绝对返回防线
                    }
                    else {
                        throw std::runtime_error("VM Error: __getitem__ has no callable implementation.");
                    }
                }
                else {
                    throw std::runtime_error("VM Error: Cannot index this instance (no __getitem__).");
                }
            }

            // ==========================================================
            // ★ 高级容器阻断防线 (放在 Instance 检查下面！)
            // ==========================================================
            if (!obj.isObjType(ObjType::REAL_MATRIX) &&
                !obj.isObjType(ObjType::COMPLEX_MATRIX) &&
                !obj.isObjType(ObjType::STRING_MATRIX) &&
                !obj.isObjType(ObjType::LIST) &&
                !obj.isString()) {
                throw std::runtime_error("TypeError: Cannot index into a value of type '" + getTypeName(obj) + "'.");
            }

            int i = 0;
            try {
                i = static_cast<int>(std::round(idx.asDouble()));
            }
            catch (...) {
                throw std::runtime_error("TypeError: Array or List index must be a number, got '" + getTypeName(idx) + "'.");
            }

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                const auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
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
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                const auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
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
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                const auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
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
            else if (obj.isObjType(ObjType::LIST)) {
                auto list = static_cast<ObjList*>(obj.asObj());
                int n = static_cast<int>(list->vec.size());
                if (i < 0) i = n + i;
                if (i < 0 || i >= n) throw std::out_of_range("List Error: Index out of bounds.");
                push(list->vec[i]);
            }
            else if (obj.isString()) {
                const auto& s = obj.asString();
                if (i < 0) i = static_cast<int>(s.size()) + i;
                if (i < 0 || i >= static_cast<int>(s.size()))
                    throw std::runtime_error("VM Error: String index out of bounds.");
                push(Value(std::string(1, s[i])));
            }
        }
        else if (dims == 2) {
            Value col = pop();
            Value row = pop();
            Value obj = pop();
            int r = static_cast<int>(std::round(row.asDouble()));
            int c = static_cast<int>(std::round(col.asDouble()));

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                const auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                push(Value(m(r, c)));
            }
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                const auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                push(Value(m(r, c)));
            }
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                const auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
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
    }

    void VM::execIndexSet(uint8_t dims) {
        Value val = pop();

        if (dims == 1) {
            Value idx = pop();
            Value obj = pop();

            if (obj.isObjType(ObjType::DICT)) {
                auto d = static_cast<ObjDict*>(obj.asObj());
                auto it = d->keyMap.find(idx);
                if (it != d->keyMap.end()) {
                    d->elements[it->second].second = val;
                } else {
                    d->keyMap[idx] = d->elements.size();
                    d->elements.push_back({idx, val});
                }
                push(val); push(obj); return;
            }

            // ── Instance (__setitem__) ──
            if (obj.isInstance()) {
                auto inst = obj.asInstance();
                auto c = inst->classDef;
                ObjClosure* setitemMethod = nullptr;
                while (c) {
                    auto it = c->methods.find("__setitem__");
                    if (it != c->methods.end()) {
                        setitemMethod = it->second;
                        break;
                    }
                    c = c->parent;
                }
                if (setitemMethod) {
                    if (setitemMethod->isBytecode()) {
                        std::shared_ptr<std::vector<std::shared_ptr<UpVal>>> captures = nullptr;
                        if (setitemMethod->hasCaptures())
                            captures = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(setitemMethod->capturedEnv);

                        callVMFunction(setitemMethod->compiledFnIndex, { idx, val }, captures, Value(inst), Value(inst->classDef));
                        push(val); push(obj); return; // ★ 绝对返回防线！
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
                        push(val); push(obj); return; // ★ 绝对返回防线！
                    }
                    else {
                        throw std::runtime_error("VM Error: __setitem__ has no callable implementation.");
                    }
                }
                else {
                    throw std::runtime_error("VM Error: Cannot assign index on this instance (no __setitem__).");
                }
            }

            // ==========================================================
            // ★ 高级容器阻断防线 (放在 Instance 检查下面！)
            // ==========================================================
            if (!obj.isObjType(ObjType::REAL_MATRIX) &&
                !obj.isObjType(ObjType::COMPLEX_MATRIX) &&
                !obj.isObjType(ObjType::STRING_MATRIX) &&
                !obj.isObjType(ObjType::LIST) &&
                !obj.isString()) {
                throw std::runtime_error("TypeError: Cannot index into a value of type '" + getTypeName(obj) + "'.");
            }

            int i = 0;
            try {
                i = static_cast<int>(std::round(idx.asDouble()));
            }
            catch (...) {
                throw std::runtime_error("TypeError: Array or List index must be a number, got '" + getTypeName(idx) + "'.");
            }

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(RealMatrix(static_cast<ObjRealMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
                if (val.isComplex() || val.isObjType(ObjType::COMPLEX_MATRIX)) {
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
                        if (val.isObjType(ObjType::COMPLEX_MATRIX)) {
                            auto srcFlat = static_cast<ObjComplexMatrix*>(val.asObj())->mat.rawData();
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
                        // 2D 矩阵整行赋值 / 广播
                        if (i < 0) i = m.getRows() + i;
                        if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");
                        if (val.isObjType(ObjType::REAL_MATRIX)) {
                            const auto& src = static_cast<ObjRealMatrix*>(val.asObj())->mat;
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
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(ComplexMatrix(static_cast<ObjComplexMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
                if (m.getRows() == 1) {
                    if (i < 0) i = m.getCols() + i;
                    m(0, i) = val.asComplex();
                }
                else if (m.getCols() == 1) {
                    if (i < 0) i = m.getRows() + i;
                    m(i, 0) = val.asComplex();
                }
                else {
                    if (i < 0) i = m.getRows() + i;
                    if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");
                    if (val.isObjType(ObjType::COMPLEX_MATRIX)) {
                        auto srcFlat = static_cast<ObjComplexMatrix*>(val.asObj())->mat.rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = srcFlat[j];
                    }
                    else if (val.isObjType(ObjType::REAL_MATRIX)) {
                        auto srcFlat = static_cast<ObjRealMatrix*>(val.asObj())->mat.rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = Complex(srcFlat[j], 0.0);
                    }
                    else {
                        Complex cv = val.asComplex();
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = cv;
                    }
                }
            }
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(StringMatrix(static_cast<ObjStringMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
                if (m.getRows() == 1) {
                    if (i < 0) i = m.getCols() + i;
                    if (val.isString()) m(0, i) = val.asString();
                    else { std::ostringstream oss; oss << val; m(0, i) = oss.str(); }
                }
                else if (m.getCols() == 1) {
                    if (i < 0) i = m.getRows() + i;
                    if (val.isString()) m(i, 0) = val.asString();
                    else { std::ostringstream oss; oss << val; m(i, 0) = oss.str(); }
                }
                else {
                    if (i < 0) i = m.getRows() + i;
                    if (i < 0 || i >= m.getRows()) throw std::out_of_range("VM Error: Row index out of bounds.");
                    if (val.isObjType(ObjType::STRING_MATRIX)) {
                        auto srcFlat = static_cast<ObjStringMatrix*>(val.asObj())->mat.rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = srcFlat[j];
                    }
                    else if (val.isObjType(ObjType::REAL_MATRIX)) {
                        auto srcFlat = static_cast<ObjRealMatrix*>(val.asObj())->mat.rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) { std::ostringstream oss; oss << Value(srcFlat[j]); m(i, j) = oss.str(); }
                    }
                    else if (val.isObjType(ObjType::COMPLEX_MATRIX)) {
                        auto srcFlat = static_cast<ObjComplexMatrix*>(val.asObj())->mat.rawData();
                        if (static_cast<int>(srcFlat.size()) != m.getCols()) throw std::runtime_error("VM Error: Row assignment size mismatch.");
                        for (int j = 0; j < m.getCols(); ++j) { std::ostringstream oss; oss << Value(srcFlat[j]); m(i, j) = oss.str(); }
                    }
                    else {
                        std::string s;
                        if (val.isString()) s = val.asString();
                        else { std::ostringstream oss; oss << val; s = oss.str(); }
                        for (int j = 0; j < m.getCols(); ++j) m(i, j) = s;
                    }
                }
            }
            else if (obj.isObjType(ObjType::LIST)) {
                auto list = static_cast<ObjList*>(obj.asObj());
                int n = static_cast<int>(list->vec.size());
                if (i < 0) i = n + i;
                if (i < 0 || i >= n) throw std::out_of_range("List Error: Index out of bounds.");
                list->vec[i] = val;
            }
            else if (obj.isString()) {
                if (obj.asObj()->refCount > 2) obj = Value(static_cast<ObjString*>(obj.asObj())->str);
                auto& s = static_cast<ObjString*>(obj.asObj())->str;
                if (i < 0) i = static_cast<int>(s.size()) + i;
                if (i < 0 || i >= static_cast<int>(s.size()))
                    throw std::runtime_error("VM Error: String index out of bounds.");
                if (!val.isString() || val.asString().size() != 1)
                    throw std::runtime_error("VM Error: String element assignment requires a single character.");
                s[i] = val.asString()[0];
            }
            push(val);
            push(obj);
        }
        else if (dims == 2) {
            Value col = pop();
            Value row = pop();
            Value obj = pop();
            int r = static_cast<int>(std::round(row.asDouble()));
            int c = static_cast<int>(std::round(col.asDouble()));

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                if (val.isComplex()) {
                    ComplexMatrix cm = static_cast<ObjRealMatrix*>(obj.asObj())->mat.toComplexMatrix();
                    if (r < 0) r = cm.getRows() + r;
                    if (c < 0) c = cm.getCols() + c;
                    cm(r, c) = val.asComplex();
                    obj = Value(cm);
                }
                else {
                    if (obj.asObj()->refCount > 2) obj = Value(RealMatrix(static_cast<ObjRealMatrix*>(obj.asObj())->mat));
                    auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
                    if (r < 0) r = m.getRows() + r;
                    if (c < 0) c = m.getCols() + c;
                    m(r, c) = val.asDouble();
                }
            }
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(ComplexMatrix(static_cast<ObjComplexMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                m(r, c) = val.asComplex();
            }
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(StringMatrix(static_cast<ObjStringMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
                if (r < 0) r = m.getRows() + r;
                if (c < 0) c = m.getCols() + c;
                if (val.isString())
                    m(r, c) = val.asString();
                else {
                    std::ostringstream oss; oss << val;
                    m(r, c) = oss.str();
                }
            }
            else {
                throw std::runtime_error("VM Error: 2D index assignment requires a matrix.");
            }
            push(val);
            push(obj);
        }
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

            if (obj.isString()) {
                const auto& s = obj.asString();
                auto ids = buildSliceIndices(static_cast<int>(s.size()), start, end, step);
                std::string result;
                for (int id : ids) result += s[id];
                push(Value(result));
                return;
            }

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                const auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
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

            if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                const auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
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

            if (obj.isObjType(ObjType::STRING_MATRIX)) {
                const auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
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

            if (obj.isObjType(ObjType::LIST)) {
                const auto& L = static_cast<ObjList*>(obj.asObj())->vec;
                auto ids = buildSliceIndices(static_cast<int>(L.size()), start, end, step);
                ObjList* result = GcHeap::get().allocate<ObjList>();
                for (int id : ids) result->vec.push_back(L[id]);
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

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                processMatSlice(static_cast<ObjRealMatrix*>(obj.asObj())->mat);
            }
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                processMatSlice(static_cast<ObjComplexMatrix*>(obj.asObj())->mat);
            }
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                processMatSlice(static_cast<ObjStringMatrix*>(obj.asObj())->mat);
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
            Value obj = pop();

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(RealMatrix(static_cast<ObjRealMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjRealMatrix*>(obj.asObj())->mat;
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);

                if (val.isNumber() || val.isObjType(ObjType::BIGINT) || val.isObjType(ObjType::FRACTION)) {
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
                else if (val.isObjType(ObjType::REAL_MATRIX)) {
                    const auto& src = static_cast<ObjRealMatrix*>(val.asObj())->mat;
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
            else if (obj.isObjType(ObjType::LIST)) {
                auto list = static_cast<ObjList*>(obj.asObj());
                auto ids = buildSliceIndices(static_cast<int>(list->vec.size()), start, end, step);
                if (val.isObjType(ObjType::LIST)) {
                    const auto& srcL = static_cast<ObjList*>(val.asObj())->vec;
                    if (srcL.size() != ids.size())
                        throw std::runtime_error("VM Error: Slice assignment size mismatch.");
                    for (size_t k = 0; k < ids.size(); ++k)
                        list->vec[ids[k]] = srcL[k];
                }
                else {
                    for (int id : ids)
                        list->vec[id] = val;
                }
            }
            else if (obj.isString()) {
                if (obj.asObj()->refCount > 2) obj = Value(static_cast<ObjString*>(obj.asObj())->str);
                auto& s = static_cast<ObjString*>(obj.asObj())->str;
                auto ids = buildSliceIndices(static_cast<int>(s.size()), start, end, step);
                if (!val.isString())
                    throw std::runtime_error("VM Error: String slice assignment requires a string.");
                const auto& src = val.asString();
                if (static_cast<int>(src.size()) != static_cast<int>(ids.size()))
                    throw std::runtime_error("VM Error: String slice assignment size mismatch.");
                for (size_t k = 0; k < ids.size(); ++k) s[ids[k]] = src[k];
            }
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(ComplexMatrix(static_cast<ObjComplexMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjComplexMatrix*>(obj.asObj())->mat;
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);
                if (val.isObjType(ObjType::COMPLEX_MATRIX)) {
                    auto srcFlat = static_cast<ObjComplexMatrix*>(val.asObj())->mat.rawData();

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
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(StringMatrix(static_cast<ObjStringMatrix*>(obj.asObj())->mat));
                auto& m = static_cast<ObjStringMatrix*>(obj.asObj())->mat;
                int n = (m.getRows() == 1) ? m.getCols() : m.getRows();
                auto ids = buildSliceIndices(n, start, end, step);
                if (val.isObjType(ObjType::STRING_MATRIX)) {
                    auto srcFlat = static_cast<ObjStringMatrix*>(val.asObj())->mat.rawData();

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
                    if (val.isString()) sv = val.asString();
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
            push(val);
            push(obj);
        }
        else if (dims == 2) {
            Value val = pop();
            auto cStep = readOptionalInt();
            auto cEnd = readOptionalInt();
            auto cStart = readOptionalInt();
            auto rStep = readOptionalInt();
            auto rEnd = readOptionalInt();
            auto rStart = readOptionalInt();
            Value obj = pop();

            auto processMatSliceSet = [&](auto& m) {
                auto rIds = buildSliceIndices(m.getRows(), rStart, rEnd, rStep);
                auto cIds = buildSliceIndices(m.getCols(), cStart, cEnd, cStep);
                int dstR = static_cast<int>(rIds.size());
                int dstC = static_cast<int>(cIds.size());

                using ElemType = std::decay_t<decltype(m(0, 0))>;

                // 检测右值是否为一个矩阵
                bool isRhsMat = val.isObjType(ObjType::REAL_MATRIX) ||
                    val.isObjType(ObjType::COMPLEX_MATRIX) ||
                    val.isObjType(ObjType::STRING_MATRIX);

                if (isRhsMat) {
                    int srcR = 0, srcC = 0;
                    if (val.isObjType(ObjType::REAL_MATRIX)) {
                        srcR = static_cast<ObjRealMatrix*>(val.asObj())->mat.getRows();
                        srcC = static_cast<ObjRealMatrix*>(val.asObj())->mat.getCols();
                    }
                    else if (val.isObjType(ObjType::COMPLEX_MATRIX)) {
                        srcR = static_cast<ObjComplexMatrix*>(val.asObj())->mat.getRows();
                        srcC = static_cast<ObjComplexMatrix*>(val.asObj())->mat.getCols();
                    }
                    else {
                        srcR = static_cast<ObjStringMatrix*>(val.asObj())->mat.getRows();
                        srcC = static_cast<ObjStringMatrix*>(val.asObj())->mat.getCols();
                    }

                    if (srcR != dstR || srcC != dstC)
                        throw std::runtime_error("VM Error: Slice assignment size mismatch.");

                    for (int i = 0; i < dstR; ++i) {
                        for (int j = 0; j < dstC; ++j) {
                            if constexpr (std::is_same_v<ElemType, double>) {
                                if (val.isObjType(ObjType::REAL_MATRIX))
                                    m(rIds[i], cIds[j]) = static_cast<ObjRealMatrix*>(val.asObj())->mat(i, j);
                                else
                                    throw std::runtime_error("VM Error: Cannot assign complex/string matrix to real matrix slice.");
                            }
                            else if constexpr (std::is_same_v<ElemType, Complex>) {
                                if (val.isObjType(ObjType::COMPLEX_MATRIX))
                                    m(rIds[i], cIds[j]) = static_cast<ObjComplexMatrix*>(val.asObj())->mat(i, j);
                                else if (val.isObjType(ObjType::REAL_MATRIX))
                                    m(rIds[i], cIds[j]) = Complex(static_cast<ObjRealMatrix*>(val.asObj())->mat(i, j));
                                else
                                    throw std::runtime_error("VM Error: Cannot assign string matrix to complex matrix slice.");
                            }
                            else if constexpr (std::is_same_v<ElemType, std::string>) {
                                std::ostringstream oss;
                                if (val.isObjType(ObjType::STRING_MATRIX))
                                    oss << static_cast<ObjStringMatrix*>(val.asObj())->mat(i, j);
                                else if (val.isObjType(ObjType::COMPLEX_MATRIX))
                                    oss << Value(static_cast<ObjComplexMatrix*>(val.asObj())->mat(i, j));
                                else
                                    oss << Value(static_cast<ObjRealMatrix*>(val.asObj())->mat(i, j));
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
                        if (val.isString())
                            scalarVal = val.asString();
                        else {
                            std::ostringstream oss; oss << val; scalarVal = oss.str();
                        }
                    }

                    for (int ri : rIds)
                        for (int ci : cIds)
                            m(ri, ci) = scalarVal;
                }
                };

            if (obj.isObjType(ObjType::REAL_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(RealMatrix(static_cast<ObjRealMatrix*>(obj.asObj())->mat));
                processMatSliceSet(static_cast<ObjRealMatrix*>(obj.asObj())->mat);
            }
            else if (obj.isObjType(ObjType::COMPLEX_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(ComplexMatrix(static_cast<ObjComplexMatrix*>(obj.asObj())->mat));
                processMatSliceSet(static_cast<ObjComplexMatrix*>(obj.asObj())->mat);
            }
            else if (obj.isObjType(ObjType::STRING_MATRIX)) {
                if (obj.asObj()->refCount > 2) obj = Value(StringMatrix(static_cast<ObjStringMatrix*>(obj.asObj())->mat));
                processMatSliceSet(static_cast<ObjStringMatrix*>(obj.asObj())->mat);
            }
            else {
                throw std::runtime_error("VM Error: 2D slice assignment requires a matrix.");
            }
            push(val);
            push(obj);
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
            return v.isNumber() ||
                v.isObjType(ObjType::BIGINT) ||
                v.isObjType(ObjType::FRACTION) ||
                v.isObjType(ObjType::BASENUM) ||
                v.isObjType(ObjType::COMPLEX) ||
                v.isString() ||
                v.isObjType(ObjType::REAL_MATRIX) ||
                v.isObjType(ObjType::COMPLEX_MATRIX) ||
                v.isObjType(ObjType::STRING_MATRIX);
            };

        for (int ii = 0; ii < total; ++ii) {
            const Value& v = stack[stack.size() - total + ii];
            if (v.isObjType(ObjType::COMPLEX) ||
                v.isObjType(ObjType::COMPLEX_MATRIX))
                hasComplex = true;
            if (v.isString() ||
                v.isObjType(ObjType::STRING_MATRIX))
                hasString = true;
            if (!canBeMatrixElement(v))
                hasOther = true;
        }

        Value result;

        if (hasOther) {
            if (rows == 1) {
                ObjList* L = GcHeap::get().allocate<ObjList>();
                for (int ii = 0; ii < total; ++ii)
                    L->vec.push_back(stack[stack.size() - total + ii]);
                result = Value(L);
            }
            else {
                ObjList* outer = GcHeap::get().allocate<ObjList>();
                for (int i = 0; i < rows; ++i) {
                    ObjList* inner = GcHeap::get().allocate<ObjList>();
                    for (int j = 0; j < cols; ++j)
                        inner->vec.push_back(stack[stack.size() - total + i * cols + j]);
                    inner->is_frozen = true;
                    outer->vec.push_back(Value(inner));
                }
                result = Value(outer);
            }
        }
        else {
            bool hasSubMatrix = false;
            for (int ii = 0; ii < total; ++ii) {
                const Value& v = stack[stack.size() - total + ii];
                if (v.isObjType(ObjType::REAL_MATRIX) ||
                    v.isObjType(ObjType::COMPLEX_MATRIX) ||
                    v.isObjType(ObjType::STRING_MATRIX))
                    hasSubMatrix = true;
            }

            if (hasSubMatrix) {
                auto extractCell = [&](Value& cell) {
                    if (!cell.isObjType(ObjType::REAL_MATRIX) &&
                        !cell.isObjType(ObjType::COMPLEX_MATRIX) &&
                        !cell.isObjType(ObjType::STRING_MATRIX)) {
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
                        if (cell.isObjType(ObjType::REAL_MATRIX)) {
                            const auto& m = static_cast<ObjRealMatrix*>(cell.asObj())->mat;
                            std::vector<std::string> flat;
                            for (int i = 0; i < m.getRows(); ++i)
                                for (int j = 0; j < m.getCols(); ++j) {
                                    std::ostringstream oss; oss << Value(m(i, j));
                                    flat.push_back(oss.str());
                                }
                            cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                        }
                        else if (cell.isObjType(ObjType::COMPLEX_MATRIX)) {
                            const auto& m = static_cast<ObjComplexMatrix*>(cell.asObj())->mat;
                            std::vector<std::string> flat;
                            for (int i = 0; i < m.getRows(); ++i)
                                for (int j = 0; j < m.getCols(); ++j) {
                                    std::ostringstream oss; oss << Value(m(i, j));
                                    flat.push_back(oss.str());
                                }
                            cell = Value(StringMatrix(m.getRows(), m.getCols(), flat));
                        }
                    }
                    else if (hasComplex && cell.isObjType(ObjType::REAL_MATRIX)) {
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
                                    rowResult = Value(static_cast<ObjStringMatrix*>(rowResult.asObj())->mat
                                        .integR(static_cast<ObjStringMatrix*>(cell.asObj())->mat));
                                else if (hasComplex)
                                    rowResult = Value(static_cast<ObjComplexMatrix*>(rowResult.asObj())->mat
                                        .integR(static_cast<ObjComplexMatrix*>(cell.asObj())->mat));
                                else
                                    rowResult = Value(static_cast<ObjRealMatrix*>(rowResult.asObj())->mat
                                        .integR(static_cast<ObjRealMatrix*>(cell.asObj())->mat));
                            }
                        }
                        if (matResult.isNone()) {
                            matResult = rowResult;
                        }
                        else {
                            if (hasString)
                                matResult = Value(static_cast<ObjStringMatrix*>(matResult.asObj())->mat
                                    .integC(static_cast<ObjStringMatrix*>(rowResult.asObj())->mat));
                            else if (hasComplex)
                                matResult = Value(static_cast<ObjComplexMatrix*>(matResult.asObj())->mat
                                    .integC(static_cast<ObjComplexMatrix*>(rowResult.asObj())->mat));
                            else
                                matResult = Value(static_cast<ObjRealMatrix*>(matResult.asObj())->mat
                                    .integC(static_cast<ObjRealMatrix*>(rowResult.asObj())->mat));
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
                    if (v.isString())
                        flat[ii] = v.asString();
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

        if (needle.isString() && haystack.isString()) {
            bool found = haystack.asString().find(needle.asString()) != std::string::npos;
            push(Value(found ? 1.0 : 0.0));
            return;
        }
        if (haystack.isString()) {
            throw std::runtime_error(
                "VM Error: 'in' on string requires a string on the left side.");
        }

        if (haystack.isObjType(ObjType::REAL_MATRIX)) {
            const auto& m = static_cast<ObjRealMatrix*>(haystack.asObj())->mat;
            double target;
            try { target = needle.asDouble(); }
            catch (...) { push(Value(0.0)); return; }
            for (const auto& v : m.rawData()) {
                if (v == target) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (haystack.isObjType(ObjType::COMPLEX_MATRIX)) {
            const auto& m = static_cast<ObjComplexMatrix*>(haystack.asObj())->mat;
            Complex target;
            try { target = needle.asComplex(); }
            catch (...) { push(Value(0.0)); return; }
            for (const auto& v : m.rawData()) {
                if (v == target) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (haystack.isObjType(ObjType::STRING_MATRIX)) {
            if (!needle.isString())
                throw std::runtime_error(
                    "VM Error: 'in' on StringMatrix requires a string needle.");
            const auto& m = static_cast<ObjStringMatrix*>(haystack.asObj())->mat;
            const auto& target = needle.asString();
            for (const auto& v : m.rawData()) {
                if (v == target) { push(Value(1.0)); return; }
            }
            push(Value(0.0));
            return;
        }

        if (haystack.isObjType(ObjType::LIST)) {
            const auto& L = static_cast<ObjList*>(haystack.asObj())->vec;
            for (const auto& e : L) {
                try {
                    if (Value::equals(needle, e)) {
                        push(Value(1.0));
                        return;
                    }
                }
                catch (...) {}
            }
            push(Value(0.0));
            return;
        }

        if (haystack.isObjType(ObjType::DICT)) {
            auto d = static_cast<ObjDict*>(haystack.asObj());
            push(Value(d->keyMap.find(needle) != d->keyMap.end() ? 1.0 : 0.0));
            return;
        }

        if (haystack.isObjType(ObjType::SET)) {
            auto s = static_cast<ObjSet*>(haystack.asObj());
            push(Value(s->keys.find(needle) != s->keys.end() ? 1.0 : 0.0));
            return;
        }

        if (haystack.isInstance()) {
            auto method = findDunder(haystack, "__contains__");
            if (method) {
                push(Value(callDunder(haystack, "__contains__", { needle }).truthy() ? 1.0 : 0.0));
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
                closeUpvalues(0);
                stack.clear();
            }
            else {
                closeUpvalues(base);
                stack.resize(base);
            }
            shouldExit = true;  // 通知 run() 退出
            return result;
        }

        closeUpvalues(base);
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
        std::string methodName = currentChunk().constants[nameIdx].asString();
        Value obj = stack[stack.size() - 1 - argc];

        ObjClosure* method = nullptr;
        ObjClass* owningClass = nullptr;

        // ==============================================================
        // 1. 如果它是原生 Dict！我们要像对待对象一样去调用它内部的闭包
        // ==============================================================
        if (obj.isObjType(ObjType::DICT)) {
            auto d = static_cast<ObjDict*>(obj.asObj());
            auto it = d->keyMap.find(Value(methodName));
            if (it != d->keyMap.end()) {
                Value fv = d->elements[it->second].second;
                if (fv.isFunctionClosure()) {
                    method = fv.asFunction();
                }
            }
            if (!method) {
                throw std::runtime_error("VM Error: No callable field '" + methodName + "' in Dict.");
            }
        }
        // ==============================================================
        // 2. 经典面向对象 Instance 的方法查询（优先类模板，后查原型挂载）
        // ==============================================================
        else if (obj.isInstance()) {
            auto inst = obj.asInstance();
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

            if (!method && inst->fields) {
                auto it = inst->fields->keyMap.find(Value(methodName));
                if (it != inst->fields->keyMap.end()) {
                    Value fv = inst->fields->elements[it->second].second;
                    if (fv.isFunctionClosure()) {
                        method = fv.asFunction();
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

                ObjList* restList = GcHeap::get().allocate<ObjList>();
                if (static_cast<int>(argc) > fixedMax) {
                    int restCount = static_cast<int>(argc) - fixedMax;
                    std::vector<Value> tempValues(restCount);
                    for (int j = 0; j < restCount; j++) tempValues[restCount - 1 - j] = pop();
                    for (int j = 0; j < restCount; j++) restList->vec.push_back(tempValues[j]);
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
                newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(
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
        std::string methodName = currentChunk().constants[nameIdx].asString();
        Value selfVal = stack[stack.size() - 1 - argc];
        if (!selfVal.isInstance())
            throw std::runtime_error("VM Error: 'super' requires an instance context.");
        auto inst = selfVal.asInstance();
        // ★ FIX: 直接从当前函数的帧寄存器提取！
        Value classVal = frame().classContext;
        if (!classVal.isClass())
            throw std::runtime_error("VM Error: 'super' requires class context (__class__).");
        auto currentClass = static_cast<ObjClass*>(classVal.asObj());
        auto parentClass = currentClass->parent;
        if (!parentClass)
            throw std::runtime_error("VM Error: Class '" + currentClass->name +
                "' has no parent class.");

        ObjClosure* method = nullptr;
        ObjClass* owningClass = nullptr;
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

                ObjList* restList = GcHeap::get().allocate<ObjList>();
                if (static_cast<int>(argc) > fixedMax) {
                    int restCount = static_cast<int>(argc) - fixedMax;
                    std::vector<Value> tempValues(restCount);
                    for (int j = 0; j < restCount; j++) {
                        tempValues[restCount - 1 - j] = pop();
                    }
                    for (int j = 0; j < restCount; j++) {
                        restList->vec.push_back(tempValues[j]);
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
                newFrame.upvalues = std::any_cast<std::shared_ptr<std::vector<std::shared_ptr<UpVal>>>>(
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
        const std::string& expectedType = currentChunk().constants[typeIdx].asString();

        if (!checkValueType(val, expectedType)) {
            const std::string& paramName = currentChunk().constants[nameIdx].asString();
            throw std::runtime_error("TypeError: Parameter '" + paramName +
                "' expected type '" + expectedType +
                "', got '" + getTypeName(val) + "'.");
        }
    }

    void VM::execAssertReturnType(const Value& val, uint16_t typeIdx) {
        const std::string& expectedType = currentChunk().constants[typeIdx].asString();

        if (!checkValueType(val, expectedType)) {
            throw std::runtime_error("TypeError: Function '" + frame().function->name +
                "' expected to return '" + expectedType +
                "', but returned '" + getTypeName(val) + "'.");
        }
    }

} // namespace jc
