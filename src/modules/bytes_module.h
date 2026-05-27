#ifndef JC2_MODULE_BYTES_H
#define JC2_MODULE_BYTES_H

#include "Module.h"
#include "../vm/BuiltinRegistry.h"
#include "../vm/VM.h"
#include <vector>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <string>

namespace jc {

    // 生成或获取统一的 Bytes 类模板
    inline ObjClass* getBytesClass() {
        static ObjClass* cls = nullptr;
        if (!cls) {
            cls = GcHeap::get().allocate<ObjClass>();
            cls->name = "Bytes";
        }
        return cls;
    }

    // 将 std::vector<uint8_t> 包装给 JC2 对象
    inline Value makeBytesInstance(std::vector<uint8_t> data) {
        auto inst = GcHeap::get().allocate<ObjInstance>();
        inst->classDef = getBytesClass();
        inst->nativeData = std::make_any<std::shared_ptr<std::vector<uint8_t>>>(
            std::make_shared<std::vector<uint8_t>>(std::move(data)));
        return Value(inst);
    }

    // 从 JC2 对象中解包到底层指针
    inline std::shared_ptr<std::vector<uint8_t>> getBuf(const Value& v, const std::string& fn) {
        if (v.isInstance()) {
            auto inst = v.asInstance();
            if (inst->nativeData.has_value() && inst->nativeData.type() == typeid(std::shared_ptr<std::vector<uint8_t>>)) {
                return std::any_cast<std::shared_ptr<std::vector<uint8_t>>>(inst->nativeData);
            }
        }
        throw std::runtime_error("Type Error: " + fn + " expects a Bytes instance.");
    }

    JC2_MODULE(bytes) {
        jc::ModuleReg R(env, builtins, arity);

        ObjClass* bytesClass = getBytesClass();
        jc::Value bytesClassVal(bytesClass);

        R.set("Bytes", bytesClassVal);

        auto addBytesMethod = [&](const std::string& name, jc::NativeCallable fn) {
            auto fc = GcHeap::get().allocate<ObjClosure>(std::vector<std::string>{}, std::vector<bool>{}, name, nullptr);
            fc->nativeFn = jc::VM::makeNativeFn(fn);
            bytesClass->methods[name] = fc;
        };

        // 1. 创建空 Buffer: alloc(size)
        R.reg("alloc", { 1 }, [](const std::vector<Value>& args) -> Value {
            int size = static_cast<int>(std::round(args[0].asDouble()));
            if (size < 0) throw std::runtime_error("Math Error: buffer size cannot be negative.");
            return makeBytesInstance(std::vector<uint8_t>(size, 0));
            });

        // 2. 将数组转为 Buffer: pack(array)
        R.reg("pack", { 1 }, [](const std::vector<Value>& args) -> Value {
            auto arr = helpers::toVecHelper(args[0], "pack");
            std::vector<uint8_t> buf(arr.size());
            // 替换原有的 std::max/std::min 用法，确保类型一致（double/double）
            for (size_t i = 0; i < arr.size(); ++i) {
                double v = arr[i];
                if (v < 0.0) v = 0.0;
                if (v > 255.0) v = 255.0;
                buf[i] = static_cast<uint8_t>(v);
            }
            return makeBytesInstance(std::move(buf));
            });

        // 3. 读文件到 Buffer: readFile(path)
        R.reg("readFile", { 1 }, [](const std::vector<Value>& args) -> Value {
            if (!args[0].isString()) throw std::runtime_error("Type Error: expects string path.");
            std::string path = helpers::safeResolvePath(args[0].asString());
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (!f) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
            std::streamsize size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(static_cast<size_t>(size));
            if (f.read(reinterpret_cast<char*>(buffer.data()), size)) {
                return makeBytesInstance(std::move(buffer));
            }
            throw std::runtime_error("IO Error: Failed to read file.");
            });

        // 4. 写 Buffer 到文件: writeFile(path)
        addBytesMethod("writeFile", [](const std::vector<Value>& args) -> Value {
            if (args.empty() || !args[0].isString()) throw std::runtime_error("Type Error: expects string path.");
            std::string path = helpers::safeResolvePath(args[0].asString());
            auto buf = getBuf(jc::helpers::getGlobalCallback("self"), "writeFile");
            std::ofstream f(path, std::ios::binary);
            if (!f) throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
            f.write(reinterpret_cast<const char*>(buf->data()), buf->size());
            return Value::none();
            });

        // 5. 核心万能写入器: set(offset, value, type)
        // type可以为 "u8", "i16", "u32", "f32", "f64", "str"
        addBytesMethod("set", [](const std::vector<Value>& args) -> Value {
            auto buf = getBuf(jc::helpers::getGlobalCallback("self"), "set");
            size_t offset = static_cast<size_t>(std::max(0.0, args[0].asDouble()));
            if (!args[2].isString()) throw std::runtime_error("Type Error: type must be string.");
            std::string type = args[2].asString();

            auto writeMem = [&](const void* data, size_t size) {
                if (offset + size > buf->size()) throw std::runtime_error("Buffer Error: Write out of bounds.");
                std::memcpy(buf->data() + offset, data, size);
                };

            if (type == "str") {
                std::string s = args[1].isString() ? args[1].asString() : args[1].toString();
                writeMem(s.data(), s.size());
            }
            else if (type == "i64" || type == "u64") {
                int64_t v = args[1].asBigInt().toInt64();
                writeMem(&v, 8);
            }
            else {
                double val = args[1].asDouble();
                if (type == "u8") { uint8_t  v = static_cast<uint8_t>(val);  writeMem(&v, 1); }
                else if (type == "i8") { int8_t   v = static_cast<int8_t>(val);   writeMem(&v, 1); }
                else if (type == "u16") { uint16_t v = static_cast<uint16_t>(val); writeMem(&v, 2); }
                else if (type == "i16") { int16_t  v = static_cast<int16_t>(val);  writeMem(&v, 2); }
                else if (type == "u32") { uint32_t v = static_cast<uint32_t>(val); writeMem(&v, 4); }
                else if (type == "i32") { int32_t  v = static_cast<int32_t>(val);  writeMem(&v, 4); }
                else if (type == "f32") { float    v = static_cast<float>(val);    writeMem(&v, 4); }
                else if (type == "f64") { double   v = val;                        writeMem(&v, 8); }
                else throw std::runtime_error("Buffer Error: Unknown format type '" + type + "'.");
            }
            return Value::none();
            });

        addBytesMethod("write_arr", [](const std::vector<Value>& args) -> Value {
            auto buf = getBuf(jc::helpers::getGlobalCallback("self"), "write_arr");
            size_t offset = static_cast<size_t>(std::max(0.0, args[0].asDouble()));
            auto arr = helpers::toVecHelper(args[1], "write_arr"); // C++ 层瞬间提取 15 万个浮点数
            std::string type = args[2].asString();
            if (type == "i16") {
                if (offset + arr.size() * 2 > buf->size()) throw std::runtime_error("Buffer out of bounds.");
                int16_t* ptr = reinterpret_cast<int16_t*>(buf->data() + offset);
                // 纯 C++ 级别的极速循环！
                for (size_t i = 0; i < arr.size(); ++i) {
                    double val = std::max(-1.0, std::min(1.0, arr[i])); // 防爆音裁剪
                    ptr[i] = static_cast<int16_t>(val * 32767.0);
                }
            }
            else if (type == "f64") {
                if (offset + arr.size() * 8 > buf->size()) throw std::runtime_error("Buffer out of bounds.");
                std::memcpy(buf->data() + offset, arr.data(), arr.size() * sizeof(double));
            }
            else {
                throw std::runtime_error("b_write_arr currently supports 'i16' and 'f64'");
            }
            return Value::none();
            });

        // 6. 核心万能读取器: get(offset, type) 或 get(offset, "str", len)
        addBytesMethod("get", [](const std::vector<Value>& args) -> Value {
            auto buf = getBuf(jc::helpers::getGlobalCallback("self"), "get");
            size_t offset = static_cast<size_t>(std::max(0.0, args[0].asDouble()));
            if (!args[1].isString()) throw std::runtime_error("Type Error: type must be string.");
            std::string type = args[1].asString();

            auto readMem = [&](void* data, size_t size) {
                if (offset + size > buf->size()) throw std::runtime_error("Buffer Error: Read out of bounds at offset " + std::to_string(offset));
                std::memcpy(data, buf->data() + offset, size);
                };

            if (type == "str") {
                if (args.size() != 3) throw std::runtime_error("Buffer Error: String read requires length.");
                size_t len = static_cast<size_t>(std::max(0.0, args[2].asDouble()));
                if (offset + len > buf->size()) throw std::runtime_error("Buffer Error: Read out of bounds.");
                return Value(std::string(reinterpret_cast<char*>(buf->data() + offset), len));
            }

            if (args.size() != 2) throw std::runtime_error("Buffer Error: Incorrect argument count.");
            if (type == "u8") { uint8_t  v; readMem(&v, 1); return Value(static_cast<double>(v)); }
            else if (type == "i8") { int8_t   v; readMem(&v, 1); return Value(static_cast<double>(v)); }
            else if (type == "u16") { uint16_t v; readMem(&v, 2); return Value(static_cast<double>(v)); }
            else if (type == "i16") { int16_t  v; readMem(&v, 2); return Value(static_cast<double>(v)); }
            else if (type == "u32") { uint32_t v; readMem(&v, 4); return Value(static_cast<double>(v)); }
            else if (type == "i32") { int32_t  v; readMem(&v, 4); return Value(static_cast<double>(v)); }
            else if (type == "i64") { int64_t  v; readMem(&v, 8); return Value(BigInt(v)); }
            else if (type == "u64") { uint64_t v; readMem(&v, 8); return Value(BigInt(std::to_string(v))); }
            else if (type == "f32") { float    v; readMem(&v, 4); return Value(static_cast<double>(v)); }
            else if (type == "f64") { double   v; readMem(&v, 8); return Value(v); }
            else throw std::runtime_error("Buffer Error: Unknown format type '" + type + "'.");
            });

        addBytesMethod("len", [](const std::vector<Value>&) -> Value {
            return Value(static_cast<double>(getBuf(jc::helpers::getGlobalCallback("self"), "len")->size()));
            });
    }
} // namespace jc
#endif // JC2_MODULE_BYTES_H
