#ifndef JC2_FFI_MODULE_H
#define JC2_FFI_MODULE_H

#include "Module.h"
#include "../memory/Value.h"
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <any>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace jc {

enum class FFIType { INT, DOUBLE };

// 核心魔法：编译期递归生成所有可能的函数签名组合，完美适配底层 ABI 寄存器分配
template<typename Ret, typename... Args>
struct FFIDispatch {
    static Ret call(void* sym, const std::vector<FFIType>& types, const uint64_t* vals, size_t idx, Args... args) {
        if (idx == types.size()) {
            // 递归终点：此时 Args... 已经完美匹配了运行时的参数类型，编译器会自动处理寄存器分配
            return reinterpret_cast<Ret(*)(Args...)>(sym)(args...);
        }
        // 限制最大 6 个参数，防止模板实例化爆炸导致二进制体积膨胀
        if constexpr (sizeof...(Args) >= 6) {
            throw std::runtime_error("FFI Error: Maximum 6 arguments supported.");
        } else {
            if (types[idx] == FFIType::INT) {
                return FFIDispatch<Ret, Args..., uint64_t>::call(sym, types, vals, idx + 1, args..., vals[idx]);
            } else {
                double d; std::memcpy(&d, &vals[idx], sizeof(double));
                return FFIDispatch<Ret, Args..., double>::call(sym, types, vals, idx + 1, args..., d);
            }
        }
    }
};

JC2_MODULE(ffi) {
    ModuleReg R(env, builtins, arity);

    R.reg("load", {1}, [](const std::vector<Value>& args) -> Value {
        std::string path = args[0].asString();
        void* handle = nullptr;
#ifdef _WIN32
        handle = LoadLibraryA(path.c_str());
#else
        handle = dlopen(path.c_str(), RTLD_LAZY);
#endif
        if (!handle) {
            throw std::runtime_error("FFI Error: Failed to load library '" + path + "'");
        }

        auto libDict = GcHeap::get().allocate<ObjDict>();
        
        auto bindFn = [handle](const std::vector<Value>& bArgs) -> Value {
            std::string fnName = bArgs[0].asString();
            std::string retTypeStr = bArgs[1].asString();
            std::vector<std::string> parsedArgTypes;
            if (bArgs[2].isObjType(ObjType::LIST)) {
                ObjList* argList = static_cast<ObjList*>(bArgs[2].asObj());
                for (const auto& v : argList->vec) parsedArgTypes.push_back(v.asString());
            } else if (bArgs[2].isObjType(ObjType::STRING_MATRIX)) {
                ObjStringMatrix* argMat = static_cast<ObjStringMatrix*>(bArgs[2].asObj());
                for (const auto& s : argMat->mat.rawData()) parsedArgTypes.push_back(s);
            } else {
                throw std::runtime_error("FFI Error: Argument types must be a list or string matrix.");
            }
            
            void* sym = nullptr;
#ifdef _WIN32
            sym = (void*)GetProcAddress((HMODULE)handle, fnName.c_str());
#else
            sym = dlsym(handle, fnName.c_str());
#endif
            if (!sym) throw std::runtime_error("FFI Error: Symbol '" + fnName + "' not found.");

            size_t nargs = parsedArgTypes.size();
            if (nargs > 6) {
                throw std::runtime_error("FFI Error: Zero-dependency FFI supports a maximum of 6 arguments.");
            }

            std::vector<std::string> argTypeStrs;
            std::vector<FFIType> argTypes;
            for (size_t i = 0; i < nargs; ++i) {
                std::string t = parsedArgTypes[i];
                argTypeStrs.push_back(t);
                if (t == "f32") throw std::runtime_error("FFI Error: f32 is not supported. Use f64.");
                else if (t == "f64") argTypes.push_back(FFIType::DOUBLE);
                else argTypes.push_back(FFIType::INT);
            }

            auto callFn = [sym, retTypeStr, argTypeStrs, argTypes](const std::vector<Value>& cArgs) -> Value {
                size_t nargs = argTypes.size();
                if (cArgs.size() != nargs) {
                    throw std::runtime_error("FFI Error: Argument count mismatch.");
                }
                
                std::vector<std::string> strArgs(nargs);
                uint64_t args[6] = {0};
                
                for (size_t i = 0; i < nargs; ++i) {
                    const std::string& t = argTypeStrs[i];
                    if (t == "string") {
                        strArgs[i] = cArgs[i].asString();
                        args[i] = (uint64_t)strArgs[i].c_str();
                    } else if (t == "f64") {
                        double d = cArgs[i].asDouble();
                        std::memcpy(&args[i], &d, sizeof(double));
                    } else if (t == "pointer") {
                        if (cArgs[i].isInstance()) {
                            auto inst = cArgs[i].asInstance();
                            Value bytesVal = cArgs[i];
                            // 如果是 ByteBuffer 实例，自动提取其内部的 buf 字段
                            if (inst->classDef && inst->classDef->name == "ByteBuffer" && inst->fields) {
                                auto it = inst->fields->keyMap.find(Value("buf"));
                                if (it != inst->fields->keyMap.end()) {
                                    bytesVal = inst->fields->elements[it->second].second;
                                }
                            }
                            // 尝试从 Bytes 实例中提取底层 C++ 内存指针
                            if (bytesVal.isInstance()) {
                                auto bInst = bytesVal.asInstance();
                                if (bInst->nativeData.has_value() && bInst->nativeData.type() == typeid(std::shared_ptr<std::vector<uint8_t>>)) {
                                    auto vecPtr = std::any_cast<std::shared_ptr<std::vector<uint8_t>>>(bInst->nativeData);
                                    args[i] = (uint64_t)vecPtr->data();
                                    continue;
                                }
                            }
                        }
                        // 如果不是 Buffer，回退到普通的整数指针转换
                        args[i] = (uint64_t)cArgs[i].asBigInt().toInt64();
                    } else {
                        args[i] = (uint64_t)cArgs[i].asBigInt().toInt64();
                    }
                }

                if (retTypeStr == "void") {
                    FFIDispatch<void>::call(sym, argTypes, args, 0);
                    return Value::none();
                } else if (retTypeStr == "f32") {
                    throw std::runtime_error("FFI Error: f32 return type not supported.");
                } else if (retTypeStr == "f64") {
                    double ret = FFIDispatch<double>::call(sym, argTypes, args, 0);
                    return Value(ret);
                } else {
                    uint64_t ret = FFIDispatch<uint64_t>::call(sym, argTypes, args, 0);
                    if (retTypeStr == "i32") return Value::fromInt32((int32_t)ret);
                    if (retTypeStr == "u32") return Value(BigInt((int64_t)(uint32_t)ret));
                    if (retTypeStr == "string") return ret ? Value(std::string((const char*)ret)) : Value::none();
                    if (retTypeStr == "pointer") return Value(BigInt((int64_t)ret));
                    return Value(BigInt((int64_t)ret));
                }
            };

            auto callClosure = GcHeap::get().allocate<ObjClosure>(
                std::vector<std::string>(nargs, "arg"), 
                std::vector<bool>(nargs, false), 
                fnName, nullptr
            );
            callClosure->nativeFn = std::make_any<NativeCallable>(callFn);
            return Value(callClosure);
        };

        auto bindClosure = GcHeap::get().allocate<ObjClosure>(
            std::vector<std::string>{"name", "retType", "argTypes"}, 
            std::vector<bool>{false, false, false}, 
            "bind", nullptr
        );
        bindClosure->nativeFn = std::make_any<NativeCallable>(bindFn);
        
        libDict->set(Value("bind"), Value(bindClosure));
        return Value(libDict);
    });
}

} // namespace jc
#endif // JC2_FFI_MODULE_H
