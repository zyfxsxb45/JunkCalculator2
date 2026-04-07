#ifndef JC2_BYTECODE_H
#define JC2_BYTECODE_H

#include "Value.h"
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace jc {

    // ═══════════════════════════════════════════
    // 操作码 (Opcode)
    // ═══════════════════════════════════════════
    enum class OpCode : uint8_t {
        // 栈操作
        OP_CONSTANT,        // 压入常量: [idx:16bit]
        OP_CONSTANT_LONG,   // 压入常量（长地址）: [idx:24bit]
        OP_NONE,            // 压入 none
        OP_TRUE,            // 压入 1.0
        OP_FALSE,           // 压入 0.0
        OP_POP,             // 弹出栈顶

        // 算术
        OP_ADD,
        OP_SUBTRACT,
        OP_MULTIPLY,
        OP_DIVIDE,
        OP_MODULO,
        OP_POWER,
        OP_NEGATE,          // 一元取负
        OP_NOT,             // 逻辑取反

        // 比较
        OP_EQUAL,
        OP_NOT_EQUAL,
        OP_LESS,
        OP_LESS_EQUAL,
        OP_GREATER,
        OP_GREATER_EQUAL,

        // 全局变量
        OP_GET_GLOBAL,      // [name_idx:16bit]
        OP_SET_GLOBAL,      // [name_idx:16bit]
        OP_DEFINE_GLOBAL,   // [name_idx:16bit]

        // 局部变量
        OP_GET_LOCAL,       // [slot:16bit]
        OP_SET_LOCAL,       // [slot:16bit]

        // 跳转
        OP_JUMP,            // 无条件跳转 [offset:16bit]
        OP_JUMP_IF_FALSE,   // 条件跳转 [offset:16bit]
        OP_LOOP,            // 回跳 [offset:16bit]

        // 函数
        OP_CALL,            // [arg_count:8bit]
        OP_RETURN,

        // 内建函数
        OP_CALL_BUILTIN,    // [name_idx:16bit, arg_count:8bit]

        // 输出（临时，调试用）
        OP_PRINT,

        // 索引
        OP_INDEX_GET,       // [dim_count:8bit]
        OP_INDEX_SET,       // [dim_count:8bit]

        // 字符串/矩阵
        OP_BUILD_LIST,      // [count:16bit]
        OP_BUILD_MATRIX,    // [rows:16bit, cols:16bit]

        // 函数（扩展）
        OP_CLOSURE,         // 创建闭包: [func_idx:16bit]
        OP_CALL_USER,       // 用户函数调用: [arg_count:8bit]

        // 复合赋值辅助
        OP_DUP,             // 复制栈顶

        // break/continue
        OP_BREAK,           // 占位，编译时会被替换为 OP_JUMP
        OP_CONTINUE,        // 占位，编译时会被替换为 OP_LOOP

        // for-in
        OP_ITER_INIT,       // 将可迭代对象转换为内部迭代器
        OP_ITER_NEXT,       // 取下一个元素，如果结束则跳转 [offset:16bit]
        OP_ITER_VAR,        // 绑定当前迭代元素到变量
        OP_IN,

        // 字符串
        OP_STRINGIFY,       // 将栈顶转为字符串
        OP_CONCAT_STRINGS,  // [count:16bit] 拼接 N 个字符串

        // 解构
        OP_DESTRUCT,        // [count:8bit] 解构栈顶为 N 个值

        // 异常处理
        OP_TRY_BEGIN,       // [catch_offset:16bit] 设置异常处理器
        OP_TRY_END,         // 移除异常处理器
        OP_THROW,           // 抛出异常

        // 字典
        OP_BUILD_DICT,      // [count:16bit] 从 2N 个值构建字典

        // 格式化字符串
        OP_FORMAT_STRING,   // [spec_idx:16bit] 格式化栈顶值

        // 列表推导式
        OP_LIST_INIT,       // 压入空 List
        OP_LIST_APPEND,     // 将栈顶值追加到栈中第 N 位置的 List

        // 闭包上值
        OP_GET_UPVALUE,     // [idx:16bit]
        OP_SET_UPVALUE,     // [idx:16bit]

        // 类 & OOP
        OP_CLASS,           // [name_idx:16bit] 创建 ClassDefinition
        OP_METHOD,          // [name_idx:16bit] 添加方法到类
        OP_INHERIT,         // 继承
        OP_GET_PROPERTY,    // [name_idx:16bit]
        OP_SET_PROPERTY,    // [name_idx:16bit]
        OP_INVOKE,          // [name_idx:16bit, argc:8bit]
        OP_GET_SUPER,       // [name_idx:16bit]
        OP_SUPER_INVOKE,    // [name_idx:16bit, argc:8bit]

        // 导入
        OP_IMPORT,          // [path_idx:16bit]

        // 切片
        OP_SLICE_GET,       // 切片索引读取
        OP_SLICE_SET,
        OP_REF_WRITEBACK,
    };

    // ═══════════════════════════════════════════
    // 字节码块 (Chunk)
    // 存储一段编译后的字节码 + 常量池 + 行号信息
    // ═══════════════════════════════════════════
    class Chunk {
    public:
        std::vector<uint8_t> code;      // 字节码流
        std::vector<Value> constants;   // 常量池
        std::vector<int> lines;         // 每条指令对应的源码行号

        // ── 写入接口 ──

        void write(uint8_t byte, int line) {
            code.push_back(byte);
            lines.push_back(line);
        }

        void write(OpCode op, int line) {
            write(static_cast<uint8_t>(op), line);
        }

        // 写入 16-bit 操作数 (大端序)
        void write16(uint16_t val, int line) {
            write(static_cast<uint8_t>((val >> 8) & 0xFF), line);
            write(static_cast<uint8_t>(val & 0xFF), line);
        }

        // 添加常量到常量池，返回索引
        uint16_t addConstant(const Value& val) {
            constants.push_back(val);
            if (constants.size() > 65535)
                throw std::runtime_error("Compiler Error: Too many constants in one chunk (max 65535).");
            return static_cast<uint16_t>(constants.size() - 1);
        }

        // 添加常量并生成 OP_CONSTANT 指令
        void emitConstant(const Value& val, int line) {
            uint16_t idx = addConstant(val);
            write(OpCode::OP_CONSTANT, line);
            write16(idx, line);
        }

        // 生成跳转指令，返回需要回填的偏移位置
        int emitJump(OpCode op, int line) {
            write(op, line);
            write(0xFF, line);  // 占位
            write(0xFF, line);  // 占位
            return static_cast<int>(code.size()) - 2;
        }

        // 回填跳转偏移
        void patchJump(int offset) {
            int jump = static_cast<int>(code.size()) - offset - 2;
            if (jump > 65535)
                throw std::runtime_error("Compiler Error: Jump too large.");
            code[offset] = static_cast<uint8_t>((jump >> 8) & 0xFF);
            code[offset + 1] = static_cast<uint8_t>(jump & 0xFF);
        }

        // 生成回跳（用于循环）
        void emitLoop(int loopStart, int line) {
            write(OpCode::OP_LOOP, line);
            int offset = static_cast<int>(code.size()) - loopStart + 2;
            if (offset > 65535)
                throw std::runtime_error("Compiler Error: Loop body too large.");
            write16(static_cast<uint16_t>(offset), line);
        }

        // 读取 16-bit 操作数
        uint16_t read16(int offset) const {
            return static_cast<uint16_t>((code[offset] << 8) | code[offset + 1]);
        }

        // ── 反汇编器（调试用）──

        void disassemble(const std::string& name) const {
            std::cout << "=== " << name << " ===" << std::endl;
            int offset = 0;
            while (offset < static_cast<int>(code.size())) {
                offset = disassembleInstruction(offset);
            }
            std::cout << "==================" << std::endl;
        }

        int disassembleInstruction(int offset) const {
            std::cout << std::setw(4) << std::setfill('0') << offset << "  ";

            if (offset > 0 && lines[offset] == lines[offset - 1])
                std::cout << "   | ";
            else
                std::cout << std::setw(4) << lines[offset] << " ";

            auto op = static_cast<OpCode>(code[offset]);
            switch (op) {
            case OpCode::OP_CONSTANT: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_CONSTANT       " << idx << " (";
                std::cout << constants[idx] << ")" << std::endl;
                return offset + 3;
            }
            case OpCode::OP_NONE:           std::cout << "OP_NONE" << std::endl; return offset + 1;
            case OpCode::OP_TRUE:           std::cout << "OP_TRUE" << std::endl; return offset + 1;
            case OpCode::OP_FALSE:          std::cout << "OP_FALSE" << std::endl; return offset + 1;
            case OpCode::OP_POP:            std::cout << "OP_POP" << std::endl; return offset + 1;
            case OpCode::OP_ADD:            std::cout << "OP_ADD" << std::endl; return offset + 1;
            case OpCode::OP_SUBTRACT:       std::cout << "OP_SUBTRACT" << std::endl; return offset + 1;
            case OpCode::OP_MULTIPLY:       std::cout << "OP_MULTIPLY" << std::endl; return offset + 1;
            case OpCode::OP_DIVIDE:         std::cout << "OP_DIVIDE" << std::endl; return offset + 1;
            case OpCode::OP_MODULO:         std::cout << "OP_MODULO" << std::endl; return offset + 1;
            case OpCode::OP_POWER:          std::cout << "OP_POWER" << std::endl; return offset + 1;
            case OpCode::OP_NEGATE:         std::cout << "OP_NEGATE" << std::endl; return offset + 1;
            case OpCode::OP_NOT:            std::cout << "OP_NOT" << std::endl; return offset + 1;
            case OpCode::OP_EQUAL:          std::cout << "OP_EQUAL" << std::endl; return offset + 1;
            case OpCode::OP_NOT_EQUAL:      std::cout << "OP_NOT_EQUAL" << std::endl; return offset + 1;
            case OpCode::OP_LESS:           std::cout << "OP_LESS" << std::endl; return offset + 1;
            case OpCode::OP_LESS_EQUAL:     std::cout << "OP_LESS_EQUAL" << std::endl; return offset + 1;
            case OpCode::OP_GREATER:        std::cout << "OP_GREATER" << std::endl; return offset + 1;
            case OpCode::OP_GREATER_EQUAL:  std::cout << "OP_GREATER_EQUAL" << std::endl; return offset + 1;
            case OpCode::OP_PRINT:          std::cout << "OP_PRINT" << std::endl; return offset + 1;
            case OpCode::OP_RETURN:         std::cout << "OP_RETURN" << std::endl; return offset + 1;

            case OpCode::OP_GET_GLOBAL:
            case OpCode::OP_SET_GLOBAL:
            case OpCode::OP_DEFINE_GLOBAL: {
                uint16_t idx = read16(offset + 1);
                std::string opName = (op == OpCode::OP_GET_GLOBAL) ? "OP_GET_GLOBAL" :
                    (op == OpCode::OP_SET_GLOBAL) ? "OP_SET_GLOBAL" :
                    "OP_DEFINE_GLOBAL";
                std::cout << std::left << std::setw(18) << opName << idx << " (";
                if (std::holds_alternative<std::string>(constants[idx].data))
                    std::cout << std::get<std::string>(constants[idx].data);
                else std::cout << constants[idx];
                std::cout << ")" << std::endl;
                return offset + 3;
            }

            case OpCode::OP_GET_LOCAL:
            case OpCode::OP_SET_LOCAL: {
                uint16_t slot = read16(offset + 1);
                std::string opName = (op == OpCode::OP_GET_LOCAL) ? "OP_GET_LOCAL" : "OP_SET_LOCAL";
                std::cout << std::left << std::setw(18) << opName << "slot " << slot << std::endl;
                return offset + 3;
            }

            case OpCode::OP_JUMP: {
                uint16_t jump = read16(offset + 1);
                std::cout << "OP_JUMP           -> " << (offset + 3 + jump) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_JUMP_IF_FALSE: {
                uint16_t jump = read16(offset + 1);
                std::cout << "OP_JUMP_IF_FALSE  -> " << (offset + 3 + jump) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_LOOP: {
                uint16_t jump = read16(offset + 1);
                std::cout << "OP_LOOP           -> " << (offset + 3 - jump) << std::endl;
                return offset + 3;
            }

            case OpCode::OP_CALL: {
                uint8_t argc = code[offset + 1];
                std::cout << "OP_CALL           " << static_cast<int>(argc) << " args" << std::endl;
                return offset + 2;
            }

            case OpCode::OP_CALL_BUILTIN: {
                uint16_t idx = read16(offset + 1);
                uint8_t argc = code[offset + 3];
                std::cout << "OP_CALL_BUILTIN   " << std::get<std::string>(constants[idx].data)
                    << " (" << static_cast<int>(argc) << " args)" << std::endl;
                return offset + 4;
            }
            case OpCode::OP_CLOSURE: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_CLOSURE        " << idx << std::endl;
                return offset + 3;
            }
            case OpCode::OP_CALL_USER: {
                uint8_t argc = code[offset + 1];
                std::cout << "OP_CALL_USER      " << static_cast<int>(argc) << " args" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_DUP:          std::cout << "OP_DUP" << std::endl; return offset + 1;
            case OpCode::OP_ITER_INIT: {
                uint8_t flag = code[offset + 1];
                std::cout << "OP_ITER_INIT      " << (flag ? "destruct" : "normal") << std::endl;
                return offset + 2;
            }
            case OpCode::OP_ITER_NEXT: {
                uint16_t jump = read16(offset + 1);
                std::cout << "OP_ITER_NEXT      -> " << (offset + 3 + jump) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_ITER_VAR:     std::cout << "OP_ITER_VAR" << std::endl; return offset + 1;
            case OpCode::OP_IN:             std::cout << "OP_IN" << std::endl; return offset + 1;
            case OpCode::OP_BUILD_LIST: {
                uint16_t count = read16(offset + 1);
                std::cout << "OP_BUILD_LIST     " << count << " elements" << std::endl;
                return offset + 3;
            }
            case OpCode::OP_BUILD_MATRIX: {
                uint16_t r = read16(offset + 1);
                uint16_t c = read16(offset + 3);
                std::cout << "OP_BUILD_MATRIX   " << r << "x" << c << std::endl;
                return offset + 5;
            }
            case OpCode::OP_INDEX_GET: {
                uint8_t dims = code[offset + 1];
                std::cout << "OP_INDEX_GET      " << static_cast<int>(dims) << " dims" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_INDEX_SET: {
                uint8_t dims = code[offset + 1];
                std::cout << "OP_INDEX_SET      " << static_cast<int>(dims) << " dims" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_STRINGIFY:      std::cout << "OP_STRINGIFY" << std::endl; return offset + 1;
            case OpCode::OP_CONCAT_STRINGS: {
                uint16_t count = read16(offset + 1);
                std::cout << "OP_CONCAT_STRINGS " << count << " parts" << std::endl;
                return offset + 3;
            }
            case OpCode::OP_DESTRUCT: {
                uint8_t count = code[offset + 1];
                std::cout << "OP_DESTRUCT       " << static_cast<int>(count) << " vars" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_TRY_BEGIN: {
                uint16_t jump = read16(offset + 1);
                uint16_t nameIdx = read16(offset + 3);
                std::cout << "OP_TRY_BEGIN      catch -> " << (offset + 5 + jump);
                if (nameIdx < constants.size() &&
                    std::holds_alternative<std::string>(constants[nameIdx].data))
                    std::cout << " (var: " << std::get<std::string>(constants[nameIdx].data) << ")";
                std::cout << std::endl;
                return offset + 5;  // ★ 1 opcode + 2 offset + 2 nameIdx
            }
            case OpCode::OP_TRY_END:       std::cout << "OP_TRY_END" << std::endl; return offset + 1;
            case OpCode::OP_THROW:          std::cout << "OP_THROW" << std::endl; return offset + 1;
            case OpCode::OP_BUILD_DICT: {
                uint16_t count = read16(offset + 1);
                std::cout << "OP_BUILD_DICT     " << count << " pairs" << std::endl;
                return offset + 3;
            }
            case OpCode::OP_FORMAT_STRING: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_FORMAT_STRING  spec=" << idx << std::endl;
                return offset + 3;
            }
            case OpCode::OP_LIST_INIT:     std::cout << "OP_LIST_INIT" << std::endl; return offset + 1;
            case OpCode::OP_LIST_APPEND: {
                uint16_t depth = read16(offset + 1);
                std::cout << "OP_LIST_APPEND    depth=" << depth << std::endl;
                return offset + 3;
            }
            case OpCode::OP_GET_UPVALUE: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_GET_UPVALUE    " << idx << std::endl;
                return offset + 3;
            }
            case OpCode::OP_SET_UPVALUE: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_SET_UPVALUE    " << idx << std::endl;
                return offset + 3;
            }
            case OpCode::OP_CLASS: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_CLASS          " << std::get<std::string>(constants[idx].data) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_METHOD: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_METHOD         " << std::get<std::string>(constants[idx].data) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_INHERIT:       std::cout << "OP_INHERIT" << std::endl; return offset + 1;
            case OpCode::OP_GET_PROPERTY: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_GET_PROPERTY   " << std::get<std::string>(constants[idx].data) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_SET_PROPERTY: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_SET_PROPERTY   " << std::get<std::string>(constants[idx].data) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_INVOKE: {
                uint16_t idx = read16(offset + 1);
                uint8_t argc = code[offset + 3];
                std::cout << "OP_INVOKE         " << std::get<std::string>(constants[idx].data)
                    << " (" << static_cast<int>(argc) << " args)" << std::endl;
                return offset + 4;
            }
            case OpCode::OP_IMPORT: {
                // ★ 编译器不发射操作数，路径通过栈传递
                std::cout << "OP_IMPORT" << std::endl;
                return offset + 1;
            }
            case OpCode::OP_SLICE_GET: {
                uint8_t dims = code[offset + 1];
                std::cout << "OP_SLICE_GET      " << static_cast<int>(dims) << " dims" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_SLICE_SET: {
                uint8_t dims = code[offset + 1];
                std::cout << "OP_SLICE_SET      " << static_cast<int>(dims) << " dims" << std::endl;
                return offset + 2;
            }
            case OpCode::OP_GET_SUPER: {
                uint16_t idx = read16(offset + 1);
                std::cout << "OP_GET_SUPER      " << std::get<std::string>(constants[idx].data) << std::endl;
                return offset + 3;
            }
            case OpCode::OP_SUPER_INVOKE: {
                uint16_t idx = read16(offset + 1);
                uint8_t argc = code[offset + 3];
                std::cout << "OP_SUPER_INVOKE   " << std::get<std::string>(constants[idx].data)
                    << " (" << static_cast<int>(argc) << " args)" << std::endl;
                return offset + 4;
            }
            case OpCode::OP_REF_WRITEBACK: {
                uint8_t count = code[offset + 1];
                std::cout << "OP_REF_WRITEBACK  " << static_cast<int>(count) << " args" << std::endl;
                int pos = offset + 2;
                for (int k = 0; k < count; ++k) {
                    uint8_t argIdx = code[pos];
                    uint8_t srcType = code[pos + 1];
                    uint16_t srcRef = static_cast<uint16_t>((code[pos + 2] << 8) | code[pos + 3]);
                    std::string typeName = (srcType == 1) ? "global" : (srcType == 2) ? "local" : "upvalue";
                    std::cout << "                    arg " << static_cast<int>(argIdx)
                        << " → " << typeName << " " << srcRef << std::endl;
                    pos += 4;
                }
                return pos;
            }
            default:
                std::cout << "UNKNOWN_OP " << static_cast<int>(code[offset]) << std::endl;
                return offset + 1;
            }
        }
    };

    struct CompiledFunction {
        std::string name;
        int arity = 0;
        int maxArity = 0;
        int localCount = 0;
        Chunk chunk;

        // ★ 上值捕获信息
        struct UpvalueInfo {
            std::string name;
            bool isLocal;   // true=来自外层局部变量, false=来自外层上值
            int index;      // 对应的 slot 或 upvalue index
        };
        std::vector<UpvalueInfo> upvalues;
        std::vector<bool> paramIsRef;
    };

    // ═══════════════════════════════════════════
    // 调用帧 (Call Frame)
    // ═══════════════════════════════════════════
    struct CallFrame {
        const CompiledFunction* function = nullptr;
        int ip = 0;
        int stackBase = 0;
        std::shared_ptr<std::vector<Value>> upvalues;  // ★ 闭包捕获值
    };


} // namespace jc

#endif // JC2_BYTECODE_H
