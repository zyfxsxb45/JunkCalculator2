#ifndef JC2_BUILTIN_REGISTRY_H
#define JC2_BUILTIN_REGISTRY_H

#include "Value.h"
#include <any>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace jc {

using NativeCallable = std::function<Value(const std::vector<Value>&)>;

// ═══════════════════════════════════════════
// 共享工具函数（BuiltinRegistry + Evaluator 通用）
// ═══════════════════════════════════════════
namespace helpers {

    inline Value anyToVal(const std::any& a) { return std::any_cast<Value>(a); }
    inline std::any valToAny(const Value& v) { return std::make_any<Value>(v); }

    inline Dict getDictMap(const Value& v, const std::string& fnName) {
        if (std::holds_alternative<Dict>(v.data)) return std::get<Dict>(v.data);
        if (std::holds_alternative<std::shared_ptr<Instance>>(v.data)) return std::get<std::shared_ptr<Instance>>(v.data)->fields;
        throw std::runtime_error("Type Error: " + fnName + "() expects a Dict or Instance.");
    }

    inline bool isTruthy(const Value& v) {
        if (std::holds_alternative<std::monostate>(v.data)) return false;
        if (std::holds_alternative<double>(v.data)) {
            double d = std::get<double>(v.data);
            return !Tol::isEq(d, 0.0) && !std::isnan(d);
        }
        if (std::holds_alternative<BigInt>(v.data))
            return !std::get<BigInt>(v.data).isZero();
        if (std::holds_alternative<Complex>(v.data))
            return !Tol::isEq(std::get<Complex>(v.data).modulus(), 0.0);
        if (std::holds_alternative<Fraction>(v.data))
            return !std::get<Fraction>(v.data).getNum().isZero();
        if (std::holds_alternative<BaseNum>(v.data))
            return !std::get<BaseNum>(v.data).getValue().isZero();
        if (std::holds_alternative<std::string>(v.data))
            return !std::get<std::string>(v.data).empty();
        if (std::holds_alternative<Dict>(v.data))
            return !std::get<Dict>(v.data).empty();
        if (std::holds_alternative<List>(v.data))
            return !std::get<List>(v.data).empty();
        if (std::holds_alternative<Set>(v.data))
            return !std::get<Set>(v.data).empty();
        return true;
    }

    inline std::vector<double> extractDS(const Value& v, const std::string& f) {
        if (std::holds_alternative<RealMatrix>(v.data))
            return std::get<RealMatrix>(v.data).rawData();
        if (std::holds_alternative<ComplexMatrix>(v.data)) {
            const auto& cd = std::get<ComplexMatrix>(v.data).rawData();
            std::vector<double> r(cd.size());
            for (size_t i = 0; i < cd.size(); ++i) {
                if (std::abs(cd[i].imag) > 1e-15)
                    throw std::runtime_error(f + "() requires real data.");
                r[i] = cd[i].real;
            }
            return r;
        }
        throw std::runtime_error(f + "() requires a matrix/vector.");
    }

    inline double computeMean(const std::vector<double>& d) {
        double s = 0; for (double v : d) s += v; return s / d.size();
    }
    inline double computeVar(const std::vector<double>& d) {
        double s = 0, sq = 0;
        for (double v : d) { s += v; sq += v * v; }
        double m = s / d.size();
        return sq / d.size() - m * m;
    }
    inline double computeStd(const std::vector<double>& d) { return std::sqrt(computeVar(d)); }
    inline double computeSvar(const std::vector<double>& d) {
        return computeVar(d) * static_cast<double>(d.size()) / static_cast<double>(d.size() - 1);
    }
    inline double computeCov(const std::vector<double>& X, const std::vector<double>& Y) {
        double mX = computeMean(X), mY = computeMean(Y);
        double c = 0;
        for (size_t i = 0; i < X.size(); ++i) c += (X[i] - mX) * (Y[i] - mY);
        return c / X.size();
    }
    inline double computeCorr(const std::vector<double>& X, const std::vector<double>& Y) {
        return computeCov(X, Y) / (computeStd(X) * computeStd(Y));
    }

    inline std::vector<double> toVecHelper(const Value& v, const std::string& fn) {
        if (std::holds_alternative<RealMatrix>(v.data))
            return std::get<RealMatrix>(v.data).rawData();
        if (std::holds_alternative<ComplexMatrix>(v.data)) {
            const auto& cd = std::get<ComplexMatrix>(v.data).rawData();
            std::vector<double> r(cd.size());
            for (size_t i = 0; i < cd.size(); ++i) {
                if (!Tol::isEq(cd[i].imag, 0.0))
                    throw std::runtime_error(fn + "() requires real data.");
                r[i] = cd[i].real;
            }
            return r;
        }
        throw std::runtime_error(fn + "() expects a matrix/vector.");
    }

    inline Value toRowVec(const std::vector<double>& v) {
        int n = static_cast<int>(v.size());
        if (n == 0) return Value(RealMatrix(1, 0));
        return Value(RealMatrix(1, n, v));
    }

    inline Value callClosure(const std::shared_ptr<FunctionClosure>& cl,
        const std::vector<Value>& args) {
        if (!cl || !cl->isNative())
            throw std::runtime_error(
                "Runtime Error: Closure is not callable in this context.");
        auto& fn = std::any_cast<NativeCallable&>(cl->nativeFn);
        return fn(args);
    }

    // ═══ Phase 2 回调 ═══
    // 通用闭包调用器（Evaluator 注入带作用域管理的版本，VM 走 nativeFn）
    inline std::function<Value(std::shared_ptr<FunctionClosure>,
        const std::vector<Value>&)> callFunctionCallback = nullptr;
    // 路径解析器（相对路径 → 绝对路径）
    inline std::function<std::string(const std::string&)> resolvePathCallback = nullptr;
    // 安全调用闭包：优先用回调，降级到 nativeFn
    inline Value safeCallFunction(const std::shared_ptr<FunctionClosure>& cl,
        const std::vector<Value>& args) {
        if (callFunctionCallback) return callFunctionCallback(cl, args);
        return callClosure(cl, args);  // 降级到 nativeFn 直调
    }
    // 安全路径解析：优先用回调，降级到当前目录拼接
    inline std::string safeResolvePath(const std::string& path) {
        if (resolvePathCallback) return resolvePathCallback(path);
        namespace fs = std::filesystem;
        fs::path p(path);
        if (p.is_absolute()) return p.string();
        return fs::weakly_canonical(fs::current_path() / p).string();
    }

    // ═══ Dunder 调用机制（VM/Evaluator 通过回调注入 self 绑定）═══
    // 静态回调：由 VM 或 Evaluator 在初始化时设置，用于绑定 self
    inline std::function<void(const std::string&, const Value&)> setGlobalCallback = nullptr;
    inline std::function<Value(const std::string&)> getGlobalCallback = nullptr;
    // 检查 Instance 是否有指定 dunder 方法（沿继承链查找）
    inline bool hasDunder(const Value& val, const std::string& name) {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(val.data)) return false;
        auto c = std::get<std::shared_ptr<Instance>>(val.data)->classDef;
        while (c) {
            if (c->methods.count(name)) return true;
            c = c->parent;
        }
        return false;
    }
    // ==============================================================
    // ★ Native 上下文栈：供 C++ 内建原生态环境使用
    // ==============================================================
    inline std::vector<Value> nativeSelfStack;
    inline std::vector<Value> nativeClassStack;

    inline std::pair<bool, Value> tryCallDunder(
        const std::shared_ptr<Instance>& inst,
        const std::string& methodName,
        const std::vector<Value>& args = {})
    {
        auto c = inst->classDef;
        while (c) {
            auto it = c->methods.find(methodName);
            if (it != c->methods.end()) {
                // ★ 启用原生无污染栈压入环境 (避开污染 globals! )
                nativeSelfStack.push_back(Value(inst));
                nativeClassStack.push_back(Value(c));
                Value result;
                try {
                    result = safeCallFunction(it->second, args);
                }
                catch (...) {
                    nativeSelfStack.pop_back(); nativeClassStack.pop_back();
                    throw;
                }
                nativeSelfStack.pop_back(); nativeClassStack.pop_back();
                return { true, result };
            }
            c = c->parent;
        }
        return { false, Value::none() };
    }

    inline std::function<Value(const std::string&)> evalCallback = nullptr;
    // ★ 可注入的 runFile 回调（导入脚本使用）
    inline std::function<void(const std::string&)> runFileCallback = nullptr;
    inline std::vector<std::string> g_scriptDirStack;

// ═══════════════════════════════════════════
// 值比较引擎 (Value Comparison)
// ═══════════════════════════════════════════
    inline bool checkEqual(const Value& lhs, const Value& rhs) {
        if (lhs.data.index() == rhs.data.index()) {
            if (std::holds_alternative<std::monostate>(lhs.data)) return true;
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
                return std::get<BaseNum>(lhs.data).getValue() == std::get<BaseNum>(rhs.data).getValue();
            if (std::holds_alternative<Set>(lhs.data) && std::holds_alternative<Set>(rhs.data)) {
                const auto& a = std::get<Set>(lhs.data);
                const auto& b = std::get<Set>(rhs.data);
                if (a.id() == b.id()) return true;
                if (a.size() != b.size()) return false;
                for (const auto& [key, val] : a.raw()) {
                    if (!b.contains(key)) return false;
                }
                return true;
            }
            return false;
        }

        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data))
            return Fraction(std::get<BigInt>(lhs.data)) == std::get<Fraction>(rhs.data);
        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))
            return std::get<Fraction>(lhs.data) == Fraction(std::get<BigInt>(rhs.data));

        try { return lhs.asComplex() == rhs.asComplex(); }
        catch (...) { return false; }
    }

    inline bool checkLess(const Value& lhs, const Value& rhs) {
        if (std::holds_alternative<std::string>(lhs.data) && std::holds_alternative<std::string>(rhs.data))
            return std::get<std::string>(lhs.data) < std::get<std::string>(rhs.data);

        if (std::holds_alternative<std::string>(lhs.data) || std::holds_alternative<std::string>(rhs.data))
            throw std::runtime_error("Type Error: Cannot compare string with non-string type.");

        if (std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))
            return std::get<BigInt>(lhs.data) < std::get<BigInt>(rhs.data);

        if (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<Fraction>(rhs.data))
            return std::get<Fraction>(lhs.data) < std::get<Fraction>(rhs.data);

        if ((std::holds_alternative<BigInt>(lhs.data) && std::holds_alternative<Fraction>(rhs.data)) ||
            (std::holds_alternative<Fraction>(lhs.data) && std::holds_alternative<BigInt>(rhs.data))) {
            Fraction a = std::holds_alternative<Fraction>(lhs.data) ? std::get<Fraction>(lhs.data) : Fraction(std::get<BigInt>(lhs.data));
            Fraction b = std::holds_alternative<Fraction>(rhs.data) ? std::get<Fraction>(rhs.data) : Fraction(std::get<BigInt>(rhs.data));
            return a < b;
        }

        double a = lhs.asDouble(), b = rhs.asDouble();
        return (a < b && !Tol::isEq(a, b));
    }

    inline bool checkGreater(const Value& lhs, const Value& rhs) {
        return checkLess(rhs, lhs);
    }
} // namespace helpers

// ═══════════════════════════════════════════
// BuiltinRegistry — 纯无状态函数注册表
// ═══════════════════════════════════════════
class BuiltinRegistry {
public:
    void registerAll();

    std::map<std::string, NativeCallable>& getBuiltins() { return builtins; }
    std::map<std::string, std::set<int>>& getArity() { return builtinArity; }
    const std::map<std::string, NativeCallable>& getBuiltins() const { return builtins; }
    const std::map<std::string, std::set<int>>& getArity() const { return builtinArity; }

private:
    std::map<std::string, NativeCallable> builtins;
    std::map<std::string, std::set<int>> builtinArity;

    void reg(const std::string& name, std::set<int> arity, NativeCallable fn) {
        builtins[name] = std::move(fn);
        builtinArity[name] = std::move(arity);
    }

    void registerMath();
    void registerComplex();
    void registerFraction();
    void registerPolySolver();
    void registerMatrixOps();
    void registerDecompositions();
    void registerLinearSolvers();
    void registerVectors();
    void registerNumberTheory();
    void registerBase();
    void registerStatistics();
    void registerRandom();
    void registerSystemUtils();
    void registerControlFlow();
    void registerStringFunctions();
    void registerArrayFunctions();
    void registerStringMatrix();
    void registerDictFunctions();
    void registerListConversion();
    void registerIntrospection();
    void registerFormatType();
    void registerHigherOrder();
    void registerCalculus();        // ★ Phase 2
    void registerFileIO();          // ★ Phase 2
    void registerErrorHandling();   // ★ Phase 2
    void registerSystemShell();
    void registerTypeChecks();
    void registerSetFunctions();
};

} // namespace jc
#endif // JC2_BUILTIN_REGISTRY_H
