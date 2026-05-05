#ifndef JC2_MODULE_H
#define JC2_MODULE_H

#include "../memory/Value.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jc {

    class Evaluator; // 前向声明

    // ═══ 原生模块注册 API ═══
    using NativeCallable = std::function<Value(const std::vector<Value>&)>;

    struct NativeModule {
        std::string name;
        std::string description;
        std::function<void(
            std::map<std::string, Value>&,              // environment
            std::map<std::string, NativeCallable>&,     // builtins
            std::map<std::string, std::set<int>>&       // builtinArity
            )> loader;
    };

    // ═══ 全局模块注册表 ═══
    inline std::map<std::string, NativeModule>& getNativeModules() {
        static std::map<std::string, NativeModule> modules;
        return modules;
    }

    // ═══ 模块注册辅助类 ═══
    class ModuleReg {
        std::map<std::string, NativeCallable>& builtins;
        std::map<std::string, std::set<int>>& arity;
        std::map<std::string, Value>& env;
    public:
        ModuleReg(std::map<std::string, Value>& e,
            std::map<std::string, NativeCallable>& b,
            std::map<std::string, std::set<int>>& a)
            : builtins(b), arity(a), env(e) {
        }

        void reg(const std::string& name, std::set<int> aritySet, NativeCallable fn) {
            builtins[name] = std::move(fn);
            arity[name] = std::move(aritySet);
        }

        void set(const std::string& name, Value val) {
            env[name] = std::move(val);
        }
    };

    // ═══ 模块定义宏 ═══
#define JC2_MODULE(moduleName) \
        static void _jc2_load_##moduleName( \
            std::map<std::string, jc::Value>& env, \
            std::map<std::string, jc::NativeCallable>& builtins, \
            std::map<std::string, std::set<int>>& arity); \
        namespace { \
            struct _jc2_reg_##moduleName { \
                _jc2_reg_##moduleName() { \
                    jc::getNativeModules()[#moduleName] = { \
                        #moduleName, "", _jc2_load_##moduleName \
                    }; \
                } \
            } _jc2_instance_##moduleName; \
        } \
        static void _jc2_load_##moduleName( \
            std::map<std::string, jc::Value>& env, \
            std::map<std::string, jc::NativeCallable>& builtins, \
            std::map<std::string, std::set<int>>& arity)
} // namespace jc

#endif // JC2_MODULE_H
