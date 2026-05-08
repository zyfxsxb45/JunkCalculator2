// Value.h
#ifndef JC2_VALUE_H
#define JC2_VALUE_H

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4702) 
#endif

#include <variant>
#include <string>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <numeric> 
#include <map>
#include <unordered_map>
#include "../math/Complex.h"
#include "../math/Matrix.h"
#include "../math/BigInt.h"
#include "../frontend/Expr.h"
#include "../math/Fraction.h"
#include "../math/Base.h"
#include "../math/Tolerance.h"
#include "../modules/Image.h"
#include "../modules/Probability.h"
#include "GcHeap.h"
#include "../cas/Symbolic.h" 

namespace jc {
    class Value;
    struct FunctionClosure;
    std::string setValueKey(const Value& v);

    struct ValueHasher {
        size_t operator()(const Value& v) const;
    };
    struct ValueEqual {
        bool operator()(const Value& lhs, const Value& rhs) const;
    };

    struct RecursionGuard {
        std::vector<const void*>& vis;
        bool isCycle;
        RecursionGuard(std::vector<const void*>& v, const void* p) : vis(v) {
            isCycle = (std::find(vis.begin(), vis.end(), p) != vis.end());
            if (!isCycle) vis.push_back(p);
        }
        ~RecursionGuard() {
            if (!isCycle) vis.pop_back(); // 退出时自动抹除足迹
        }
    };

    // =======================================================
    // 高级引用语义 Dict (底层交由智能指针接管防深拷贝)
    // =======================================================
    struct DictData;
    class Dict {
    private:
        std::shared_ptr<DictData> ptr;
    public:
        Dict();
        void freeze();
        bool isFrozen() const;

        void set(const Value& key, const Value& val);
        Value* get(const Value& key);
        const Value* get(const Value& key) const;
        bool has(const Value& key) const;
        bool remove(const Value& key);

        size_t size() const;
        bool empty() const;

        std::vector<Value> getKeys() const;
        std::vector<std::pair<Value, Value>> getEntries() const;

        const void* id() const { return ptr.get(); }
        void clear();
    };

    // =======================================================
    // 高级引用语义 List (底层交由智能指针接管防深拷贝)
    // =======================================================
    struct ListData;
    class List {
    private:
        std::shared_ptr<ListData> ptr;
    public:
        List();
        void freeze();
        bool isFrozen() const;

        void push_back(const Value& val);
        Value& at(int idx);
        const Value& at(int idx) const;
        void set(int idx, const Value& val);
        void insert(int idx, const Value& val);
        void removeAt(int idx);

        size_t size() const;
        bool empty() const;
        const std::vector<Value>& raw() const;
        std::vector<Value>& raw();
        const void* id() const { return ptr.get(); }
        void clear();
    };

    // =======================================================
    // 高级引用语义 Set (无序去重集合，O(1) 查找)
    // =======================================================
    struct SetData;
    class Set {
    private:
        std::shared_ptr<SetData> ptr;
    public:
        Set();
        void freeze();
        bool isFrozen() const;

        bool insert(const Value& val);
        bool contains(const Value& val) const;
        bool erase(const Value& val);

        size_t size() const;
        bool empty() const;
        const std::vector<Value>& raw() const;
        const void* id() const { return ptr.get(); }
        void clear();
    };

    struct ClassDefinition {
        std::string name;
        std::shared_ptr<ClassDefinition> parent;
        std::map<std::string, std::shared_ptr<FunctionClosure>> methods;
    };

    struct Instance {
        std::shared_ptr<ClassDefinition> classDef;
        Dict fields;
        std::any nativeData;
    };

    struct SuperProxy {
        std::shared_ptr<Instance> instance;
        std::shared_ptr<ClassDefinition> parentClass;
    };

    using SuperProxyPtr = std::shared_ptr<SuperProxy>;

    using ValueVariant = std::variant<
        std::monostate, double, BigInt, BaseNum, Fraction, std::string,
        Complex, RealMatrix, ComplexMatrix, StringMatrix, Dict, List, Set,
        std::shared_ptr<FunctionClosure>,
        std::shared_ptr<ClassDefinition>,
        std::shared_ptr<Instance>,
        SuperProxyPtr, SymExpr
    >;

    template<typename> struct always_false : std::false_type {};

    std::pair<bool, Value> invokeDunder(const std::shared_ptr<Instance>& inst, const std::string& methodName, const std::vector<Value>& args = {});

    class Value {
    private:
        Value(std::monostate) : data(std::monostate{}) {}

        static std::pair<bool, BigInt> getExactIntRoot(const BigInt& base, int64_t n) {
            if (n <= 0) return { false, BigInt(0) };
            if (n == 1) return { true, base };
            bool isNeg = base.isNegative();
            if (isNeg && n % 2 == 0) return { false, BigInt(0) };
            BigInt absBase = base.abs();
            if (absBase.isZero()) return { true, BigInt(0) };
            if (absBase == BigInt(1)) return { true, isNeg ? (BigInt(0) - BigInt(1)) : BigInt(1) };
            // 用位数估算上界，极大缩小搜索空间
            std::string s = absBase.toString();
            size_t rootLen = s.length() / static_cast<size_t>(n) + 1;
            std::string upperStr = "1";
            upperStr.append(rootLen, '0');
            BigInt lower(1), upper(upperStr);
            while (!(upper < lower)) {
                BigInt mid = (lower + upper) / BigInt(2);
                BigInt midPow = mid.pow(BigInt(n));
                if (midPow == absBase) {
                    return { true, isNeg ? (BigInt(0) - mid) : mid };
                }
                else if (midPow < absBase) {
                    lower = mid + BigInt(1);
                }
                else {
                    upper = mid - BigInt(1);
                }
            }
            return { false, BigInt(0) };
        }
        // ========================================================
        // 有理数精确有理数次幂
        // (num/den) ^ (p/q) → 分子分母分别开 q 次根，再求 p 次幂
        // 返回 {是否成功, Value 结果}
        // ========================================================
        static std::pair<bool, Value> tryExactRationalPow(
            const BigInt& num, const BigInt& den, int64_t p, int64_t q)
        {
            if (q <= 0) return { false, Value() };
            auto [numOk, numRoot] = getExactIntRoot(num, q);
            if (!numOk) return { false, Value() };
            auto [denOk, denRoot] = getExactIntRoot(den, q);
            if (!denOk) return { false, Value() };
            // 开根成功，求 p 次幂
            if (p >= 0) {
                Fraction result = Fraction(numRoot, denRoot).pow(p);
                return { true, Value::fromFraction(result) };
            }
            else {
                // 负指数：先取倒数再求正幂
                Fraction result = Fraction(denRoot, numRoot).pow(-p);
                return { true, Value::fromFraction(result) };
            }
        }
    public:
        ValueVariant data;

        Value() : data(0.0) {}
        Value(double val) : data(val) {}
        Value(std::string val) : data(std::move(val)) {}
        Value(const char* val) : data(std::string(val)) {}
        Value(Complex val) : data(val) {}
        Value(RealMatrix val) : data(std::move(val)) {}
        Value(ComplexMatrix val) : data(std::move(val)) {}
        Value(BigInt val) : data(std::move(val)) {}
        Value(std::shared_ptr<FunctionClosure> val);
        Value(Fraction val) : data(std::move(val)) {}
        Value(BaseNum val) : data(std::move(val)) {}
        Value(StringMatrix val) : data(std::move(val)) {}
        Value(Dict val) : data(std::move(val)) {}
        Value(List val) : data(std::move(val)) {}
        Value(Set val) : data(std::move(val)) {}
        Value(SymExpr val) {
            if (val.ptr && val.ptr->getType() == SymType::NUM) {
                auto numNode = std::static_pointer_cast<SymNum>(val.ptr);
                // 拆包 CASVal (std::variant<double, BigInt, Fraction, Complex>)
                std::visit([this](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, Fraction>) {
                        // 连同分母为 1 的 Fraction 也顺手击碎为 BigInt！
                        if (arg.getDen() == BigInt(1)) {
                            this->data = arg.getNum();
                        }
                        else {
                            this->data = arg;
                        }
                    }
                    else {
                        // BigInt, double, Complex 直接降维接收
                        this->data = arg;
                    }
                    }, numNode->value);
            }
            else {
                // 若仍含有变量或算不尽的根式，则保留其符号表达式的高维形态
                this->data = std::move(val);
            }
        }
        Value(std::shared_ptr<ClassDefinition> val) : data(std::move(val)) {}
        Value(std::shared_ptr<Instance> val) : data(std::move(val)) {}

        bool isInstance() const { return std::holds_alternative<std::shared_ptr<Instance>>(data); }
        bool isClass() const { return std::holds_alternative<std::shared_ptr<ClassDefinition>>(data); }
        bool isComplex() const { return std::holds_alternative<Complex>(data);}
        
        bool isSymbolic() const { return std::holds_alternative<SymExpr>(data); }
        SymExpr asSymbolic() const {
            if (std::holds_alternative<SymExpr>(data)) return std::get<SymExpr>(data);
            // 包容万象，随时准备将普通数字强行拔高为符号节点！
            if (std::holds_alternative<double>(data))  return SymExpr(std::get<double>(data));
            if (std::holds_alternative<BigInt>(data))  return SymExpr(std::get<BigInt>(data));
            if (std::holds_alternative<Fraction>(data))return SymExpr(std::get<Fraction>(data));
            if (std::holds_alternative<Complex>(data)) return SymExpr(std::get<Complex>(data));
            throw std::runtime_error("TypeError: Expected a symbolic expression or exact number.");
        }

        std::shared_ptr<Instance> asInstance() const {
            if (!isInstance()) throw std::runtime_error("Type Error: Expected an instance.");
            return std::get<std::shared_ptr<Instance>>(data);
        }

        static Value negativePow(double base, int64_t p, int64_t q) {
            double absBase = std::abs(base);
            if (q % 2 != 0) {
                double magnitude = std::pow(absBase, static_cast<double>(std::abs(p)) / static_cast<double>(q));
                if (p < 0) magnitude = 1.0 / magnitude;
                bool negResult = (std::abs(p) % 2 != 0);
                double res = negResult ? -magnitude : magnitude;
                double rounded = std::round(res);
                if (Tol::isEq(res, rounded, 1e5) && std::abs(rounded) < 9e15) {
                    return Value(BigInt(static_cast<int64_t>(rounded)));
                }
                return Value(res);
            }
            else {
                return Value(Complex(base, 0.0) ^ Complex(static_cast<double>(p) / static_cast<double>(q), 0.0));
            }
        }

        Value(SuperProxyPtr sp) : data(std::move(sp)) {}
        bool isSuperProxy() const {
            return std::holds_alternative<SuperProxyPtr>(data);
        }
        SuperProxy asSuperProxy() const {
            if (!isSuperProxy()) throw std::runtime_error("Type Error: Expected super proxy.");
            return *std::get<SuperProxyPtr>(data);
        }
        static Value makeSuperProxy(std::shared_ptr<Instance> inst,
            std::shared_ptr<ClassDefinition> parent) {
            return Value(std::make_shared<SuperProxy>(SuperProxy{ std::move(inst), std::move(parent) }));
        }


        static Value fromFraction(const Fraction& f) {
            if (f.getDen() == BigInt(1)) return Value(f.getNum());
            return Value(f);
        }

        bool isBaseNum() const { return std::holds_alternative<BaseNum>(data); }
        std::shared_ptr<FunctionClosure> asFunction() const {
            if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(data)) {
                return std::get<std::shared_ptr<FunctionClosure>>(data);
            }
            throw std::runtime_error("Type Error: Expected a function.");
        }

        bool isFunctionClosure() const {
            return std::holds_alternative<std::shared_ptr<FunctionClosure>>(data);
        }
        bool isString() const {
            return std::holds_alternative<std::string>(data);
        }

        static Value none() { return Value(std::monostate{}); }
        bool isNone() const { return std::holds_alternative<std::monostate>(data); }

        double asDouble() const {
            if (std::holds_alternative<double>(data)) return std::get<double>(data);
            if (std::holds_alternative<BigInt>(data)) return std::get<BigInt>(data).toDouble();
            if (std::holds_alternative<Fraction>(data)) return std::get<Fraction>(data).toDouble();
            if (std::holds_alternative<BaseNum>(data)) return std::get<BaseNum>(data).getValue().toDouble();
            throw std::runtime_error("Type Error: Expected a real number.");
        }

        Complex asComplex() const {
            if (std::holds_alternative<Complex>(data)) return std::get<Complex>(data);
            if (std::holds_alternative<double>(data)) return Complex(std::get<double>(data));
            if (std::holds_alternative<BigInt>(data)) return Complex(std::get<BigInt>(data).toDouble());
            if (std::holds_alternative<Fraction>(data)) return Complex(std::get<Fraction>(data).toDouble());
            if (std::holds_alternative<BaseNum>(data)) return Complex(std::get<BaseNum>(data).getValue().toDouble());
            throw std::runtime_error("Type Error: Expected a number or complex.");
        }

        BigInt asBigInt() const {
            if (std::holds_alternative<BigInt>(data)) return std::get<BigInt>(data);
            if (std::holds_alternative<double>(data)) {
                double val = std::get<double>(data);
                if (std::abs(val) > 9.22337e18) {
                    throw std::runtime_error("Math Error: Value too massively large to be safely converted to an exact layout integer.");
                }
                return BigInt(static_cast<int64_t>(std::round(val)));
            }
            if (std::holds_alternative<BaseNum>(data)) return std::get<BaseNum>(data).getValue();
            if (std::holds_alternative<Fraction>(data)) {
                const auto& f = std::get<Fraction>(data);
                if (f.getDen() == BigInt(1)) return f.getNum();
                throw std::runtime_error("Type Error: Fraction is not an integer (" + f.toString() + ").");
            }
            throw std::runtime_error("Type Error: Expected an integer.");
        }

        bool isBigInt() const { return std::holds_alternative<BigInt>(data); }

        RealMatrix asRealMatrix() const {
            if (std::holds_alternative<RealMatrix>(data)) return std::get<RealMatrix>(data);
            throw std::runtime_error("Type Error: Expected a real matrix.");
        }

        ComplexMatrix asComplexMatrix() const {
            if (std::holds_alternative<ComplexMatrix>(data)) return std::get<ComplexMatrix>(data);
            if (std::holds_alternative<RealMatrix>(data)) {
                const RealMatrix& m = std::get<RealMatrix>(data);
                std::vector<Complex> flat(m.getRows() * m.getCols());
                for (int r = 0; r < m.getRows(); ++r) {
                    for (int c = 0; c < m.getCols(); ++c) {
                        flat[r * m.getCols() + c] = Complex(m(r, c));
                    }
                }
                return ComplexMatrix(m.getRows(), m.getCols(), flat);
            }
            throw std::runtime_error("Type Error: Expected a matrix.");
        }

        // ==========================================
        // 统一哈希判定 (The One and Only Hashability)
        // ==========================================
        bool isHashable() const {
            static thread_local std::vector<const void*> visited;
            return std::visit([this](auto&& arg) -> bool {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate> ||
                              std::is_same_v<T, double> ||
                              std::is_same_v<T, BigInt> ||
                              std::is_same_v<T, Fraction> ||
                              std::is_same_v<T, Complex> ||
                              std::is_same_v<T, BaseNum> ||
                              std::is_same_v<T, std::string> ||
                              std::is_same_v<T, SymExpr> ||
                              std::is_same_v<T, RealMatrix> ||
                              std::is_same_v<T, ComplexMatrix> ||
                              std::is_same_v<T, StringMatrix> ||
                              std::is_same_v<T, std::shared_ptr<FunctionClosure>> ||
                              std::is_same_v<T, std::shared_ptr<ClassDefinition>>) {
                    return true;
                }
                else if constexpr (std::is_same_v<T, List>) {
                    if (!arg.isFrozen()) return false;
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return true;
                    for (const auto& e : arg.raw()) {
                        try {
                            if (!e.isHashable()) return false;
                        } catch (...) { return false; }
                    }
                    return true;
                }
                else if constexpr (std::is_same_v<T, Dict>) {
                    if (!arg.isFrozen()) return false;
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return true;
                    for (const auto& [k, v] : arg.getEntries()) {
                        try {
                            if (!v.isHashable()) return false;
                        } catch (...) { return false; }
                    }
                    return true;
                }
                else if constexpr (std::is_same_v<T, Set>) {
                    if (!arg.isFrozen()) return false;
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return true;
                    for (const auto& v : arg.raw()) {
                        try {
                            if (!v.isHashable()) return false;
                        } catch (...) { return false; }
                    }
                    return true;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) {
                    if (!arg) return false;
                    RecursionGuard guard(visited, arg.get());
                    if (guard.isCycle) return true;
                    auto c = arg->classDef;
                    while (c) {
                        if (c->methods.count("__hash__")) return true;
                        c = c->parent;
                    }
                    return false;
                }
                return false;
            }, data);
        }

        // ==========================================
// 统一真值判定 (The One and Only Truthiness)
// ==========================================
        bool truthy() const {
            if (std::holds_alternative<std::monostate>(data)) return false;
            if (std::holds_alternative<double>(data)) {
                double d = std::get<double>(data);
                return d != 0.0 && !std::isnan(d);
            }
            if (std::holds_alternative<BigInt>(data))
                return !std::get<BigInt>(data).isZero();
            if (std::holds_alternative<Complex>(data)) {
                const auto& c = std::get<Complex>(data);
                return c.real != 0.0 || c.imag != 0.0;
            }
            if (std::holds_alternative<Fraction>(data))
                return !std::get<Fraction>(data).getNum().isZero();
            if (std::holds_alternative<BaseNum>(data))
                return !std::get<BaseNum>(data).getValue().isZero();
            if (std::holds_alternative<std::string>(data))
                return !std::get<std::string>(data).empty();
            if (std::holds_alternative<List>(data))
                return !std::get<List>(data).empty();
            if (std::holds_alternative<Dict>(data))
                return !std::get<Dict>(data).empty();
            if (std::holds_alternative<Set>(data))
                return !std::get<Set>(data).empty();
            if (std::holds_alternative<SymExpr>(data))
                return !std::get<SymExpr>(data).isZero();
            return true;
        }

        // ==========================================
        // 统一相等判定 (The One and Only Equality)
        // ==========================================
        static bool equals(const Value& lhs, const Value& rhs) {
            // 防循环递归锁
            static thread_local std::vector<std::pair<const void*, const void*>> comparingPairs;

            // 同类型快速通道
            if (lhs.data.index() == rhs.data.index()) {
                if (std::holds_alternative<std::monostate>(lhs.data)) return true;
                if (std::holds_alternative<double>(lhs.data))
                    return std::get<double>(lhs.data) == std::get<double>(rhs.data);
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
                if (std::holds_alternative<SymExpr>(lhs.data))
                    return std::get<SymExpr>(lhs.data) == std::get<SymExpr>(rhs.data);

                if (std::holds_alternative<RealMatrix>(lhs.data)) {
                    const auto& a = std::get<RealMatrix>(lhs.data);
                    const auto& b = std::get<RealMatrix>(rhs.data);
                    if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                    for (int i = 0; i < a.getRows(); ++i)
                        for (int j = 0; j < a.getCols(); ++j)
                            if (a(i, j) != b(i, j)) return false;
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
                if (std::holds_alternative<StringMatrix>(lhs.data)) {
                    const auto& a = std::get<StringMatrix>(lhs.data);
                    const auto& b = std::get<StringMatrix>(rhs.data);
                    if (a.getRows() != b.getRows() || a.getCols() != b.getCols()) return false;
                    for (int i = 0; i < a.getRows(); ++i)
                        for (int j = 0; j < a.getCols(); ++j)
                            if (a(i, j) != b(i, j)) return false;
                    return true;
                }
                if (std::holds_alternative<List>(lhs.data)) {
                    const auto& a = std::get<List>(lhs.data);
                    const auto& b = std::get<List>(rhs.data);
                    if (a.id() == b.id()) return true;
                    if (a.size() != b.size()) return false;
                    auto pair = a.id() < b.id() ? std::make_pair(a.id(), b.id()) : std::make_pair(b.id(), a.id());
                    if (std::find(comparingPairs.begin(), comparingPairs.end(), pair) != comparingPairs.end()) return true;
                    comparingPairs.push_back(pair);
                    bool eq = true;
                    for (size_t i = 0; i < a.size(); ++i) {
                        try {
                            Value va = a.raw()[i];
                            Value vb = b.raw()[i];
                            if (!equals(va, vb)) { eq = false; break; }
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
                    if (std::find(comparingPairs.begin(), comparingPairs.end(), pair) != comparingPairs.end()) return true;
                    comparingPairs.push_back(pair);
                    bool eq = true;
                    for (const auto& [key, val] : a.getEntries()) {
                        const auto* bval = b.get(key);
                        if (!bval) { eq = false; break; }
                        try {
                            Value va = val;
                            Value vb = *bval;
                            if (!equals(va, vb)) { eq = false; break; }
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
                    for (const auto& val : a.raw()) {
                        if (!b.contains(val)) return false;
                    }
                    return true;
                }
                if (std::holds_alternative<std::shared_ptr<Instance>>(lhs.data)) {
                    auto inst1 = std::get<std::shared_ptr<Instance>>(lhs.data);
                    auto inst2 = std::get<std::shared_ptr<Instance>>(rhs.data);
                    if (inst1.get() == inst2.get()) return true;
                    auto [found, res] = invokeDunder(inst1, "__eq__", {rhs});
                    if (found) return res.truthy();
                    return false;
                }
                if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(lhs.data))
                    return std::get<std::shared_ptr<FunctionClosure>>(lhs.data).get() ==
                    std::get<std::shared_ptr<FunctionClosure>>(rhs.data).get();
                if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(lhs.data))
                    return std::get<std::shared_ptr<ClassDefinition>>(lhs.data).get() ==
                    std::get<std::shared_ptr<ClassDefinition>>(rhs.data).get();
                if (std::holds_alternative<SuperProxyPtr>(lhs.data)) {
                    auto sp1 = std::get<SuperProxyPtr>(lhs.data);
                    auto sp2 = std::get<SuperProxyPtr>(rhs.data);
                    if (!sp1 || !sp2) return sp1 == sp2;
                    return sp1->instance.get() == sp2->instance.get() && sp1->parentClass.get() == sp2->parentClass.get();
                }
                return false;
            }

            // 跨类型兼容比较
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

            if (std::holds_alternative<std::shared_ptr<Instance>>(lhs.data)) {
                auto [found, res] = invokeDunder(std::get<std::shared_ptr<Instance>>(lhs.data), "__eq__", {rhs});
                if (found) return res.truthy();
            }
            if (std::holds_alternative<std::shared_ptr<Instance>>(rhs.data)) {
                auto [found, res] = invokeDunder(std::get<std::shared_ptr<Instance>>(rhs.data), "__eq__", {lhs});
                if (found) return res.truthy();
            }

            if (std::holds_alternative<std::monostate>(lhs.data) ||
                std::holds_alternative<std::monostate>(rhs.data))
                return false;

            // ★ 终极数值降维防线：安全处理极大 BigInt 与浮点/复数的跨类型比较
            if (std::holds_alternative<BaseNum>(lhs.data)) return equals(Value(std::get<BaseNum>(lhs.data).getValue()), rhs);
            if (std::holds_alternative<BaseNum>(rhs.data)) return equals(lhs, Value(std::get<BaseNum>(rhs.data).getValue()));

            auto getNumeric = [](const Value& v) -> std::optional<Complex> {
                try {
                    if (std::holds_alternative<double>(v.data)) return Complex(std::get<double>(v.data));
                    if (std::holds_alternative<Complex>(v.data)) return std::get<Complex>(v.data);
                    if (std::holds_alternative<Fraction>(v.data)) return Complex(std::get<Fraction>(v.data).toDouble());
                } catch (...) {}
                return std::nullopt;
            };

            auto numL = getNumeric(lhs);
            auto numR = getNumeric(rhs);

            if (numL && numR) return *numL == *numR;

            if (std::holds_alternative<BigInt>(lhs.data) && numR) {
                const BigInt& b = std::get<BigInt>(lhs.data);
                if (numR->imag != 0.0) return false;
                double d = numR->real;
                if (std::floor(d) != d) return false;
                if (std::abs(d) < 9e15) return b == BigInt(static_cast<int64_t>(d));
                try { return b.toDouble() == d; } catch (...) { return false; }
            }
            if (std::holds_alternative<BigInt>(rhs.data) && numL) {
                const BigInt& b = std::get<BigInt>(rhs.data);
                if (numL->imag != 0.0) return false;
                double d = numL->real;
                if (std::floor(d) != d) return false;
                if (std::abs(d) < 9e15) return b == BigInt(static_cast<int64_t>(d));
                try { return b.toDouble() == d; } catch (...) { return false; }
            }

            return false;
        }

        // Value.h 的 class Value 内部
        std::string typeName() const {
            if (std::holds_alternative<std::monostate>(data)) return "none";
            if (std::holds_alternative<double>(data)) return "double";
            if (std::holds_alternative<BigInt>(data)) return "BigInt";
            if (std::holds_alternative<Fraction>(data)) return "Fraction";
            if (std::holds_alternative<Complex>(data)) return "Complex";
            if (std::holds_alternative<std::string>(data)) return "string";
            if (std::holds_alternative<List>(data)) return "list";
            if (std::holds_alternative<Dict>(data)) return "dict";
            if (std::holds_alternative<Set>(data)) return "set";
            if (std::holds_alternative<RealMatrix>(data)) return "RealMatrix";
            if (std::holds_alternative<ComplexMatrix>(data)) return "ComplexMatrix";
            if (std::holds_alternative<StringMatrix>(data)) return "StringMatrix";
            if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(data)) return "function";
            if (std::holds_alternative<std::shared_ptr<Instance>>(data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(data);
                return (inst && inst->classDef) ? inst->classDef->name : "instance";
            }
            if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(data)) return "class";
            if (std::holds_alternative<BaseNum>(data)) return "BaseNum";
            if (std::holds_alternative<SuperProxyPtr>(data)) return "super";
            if (std::holds_alternative<SymExpr>(data)) return "symbolic";
            return "unknown";
        }

        Value operator-() const {
            return std::visit([](auto&& a) -> Value {
                if constexpr (requires { -a; }) { return -a; }
                else { throw std::runtime_error("Type Error: Cannot negate this type."); }
                }, data);
        }

        friend Value operator+(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (requires { a + b; }) {
                    auto result = a + b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, ComplexMatrix>) {
                    return lhs.asComplexMatrix() + rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, RealMatrix>) {
                    return lhs.asComplexMatrix() + rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    return lhs.asComplexMatrix() + b;
                }
                else if constexpr (std::is_same_v<T1, Complex> && std::is_same_v<T2, RealMatrix>) {
                    return a + rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a + b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a + BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) + b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) + rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs + Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) + rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs + Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) + rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs + Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, StringMatrix> && std::is_same_v<T2, StringMatrix>) {
                    if (a.getRows() != b.getRows() || a.getCols() != b.getCols())
                        throw std::runtime_error("Type Error: StringMatrix dimensions must match for +.");
                    std::vector<std::string> flat(a.getRows() * a.getCols());
                    for (int i = 0; i < a.getRows(); ++i)
                        for (int j = 0; j < a.getCols(); ++j)
                            flat[i * a.getCols() + j] = a(i, j) + b(i, j);
                    return Value(StringMatrix(a.getRows(), a.getCols(), flat));
                }
                else {
                    throw std::runtime_error("Type Error: Cannot add these types.");
                }
                }, lhs.data, rhs.data);
        }

        friend Value operator-(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (requires { a - b; }) {
                    auto result = a - b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, ComplexMatrix>) {
                    return lhs.asComplexMatrix() - rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, RealMatrix>) {
                    return lhs.asComplexMatrix() - rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    return lhs.asComplexMatrix() - b;
                }
                else if constexpr (std::is_same_v<T1, Complex> && std::is_same_v<T2, RealMatrix>) {
                    return a - rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a - b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a - BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) - b);
                }
                else if constexpr (std::is_same_v<T1, Set> && std::is_same_v<T2, Set>) {
                    Set result;
                    for (const auto& val : a.raw()) {
                        if (!b.contains(val)) result.insert(val);
                    }
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) - rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs - Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) - rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs - Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) - rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs - Value(b.toDouble()); }
                else { throw std::runtime_error("Type Error: Subtraction not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        friend Value operator*(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (requires { a* b; }) {
                    auto result = a * b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, ComplexMatrix>) {
                    return lhs.asComplexMatrix() * rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, RealMatrix>) {
                    return lhs.asComplexMatrix() * rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    return lhs.asComplexMatrix() * b;
                }
                else if constexpr (std::is_same_v<T1, Complex> && std::is_same_v<T2, RealMatrix>) {
                    return a * rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a * b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a * BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) * b);
                }
                // 在 operator* 的 std::visit lambda 中，最终 throw 之前添加：
                else if constexpr (std::is_same_v<T1, std::string> && std::is_same_v<T2, double>) {
                    int n = static_cast<int>(std::round(b));
                    if (n < 0) throw std::runtime_error("Type Error: String repeat count must be non-negative.");
                    std::string result;
                    result.reserve(a.size() * n);
                    for (int i = 0; i < n; ++i) result += a;
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, std::string>) {
                    int n = static_cast<int>(std::round(a));
                    if (n < 0) throw std::runtime_error("Type Error: String repeat count must be non-negative.");
                    std::string result;
                    result.reserve(b.size() * n);
                    for (int i = 0; i < n; ++i) result += b;
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, std::string> && std::is_same_v<T2, BigInt>) {
                    int n = static_cast<int>(b.toDouble());
                    if (n < 0) throw std::runtime_error("Type Error: String repeat count must be non-negative.");
                    std::string result;
                    result.reserve(a.size() * n);
                    for (int i = 0; i < n; ++i) result += a;
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, std::string>) {
                    int n = static_cast<int>(a.toDouble());
                    if (n < 0) throw std::runtime_error("Type Error: String repeat count must be non-negative.");
                    std::string result;
                    result.reserve(b.size() * n);
                    for (int i = 0; i < n; ++i) result += b;
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, Set> && std::is_same_v<T2, Set>) {
                    Set result;
                    // 笛卡尔积：返回由两两组合的 List(二元组) 构成的 Set
                    for (const auto& v1Any : a.raw()) {
                        for (const auto& v2Any : b.raw()) {
                            List pair;
                            pair.push_back(v1Any);
                            pair.push_back(v2Any);
                            pair.freeze();
                            Value pairVal(pair);
                            result.insert(pairVal);
                        }
                    }
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) * rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs * Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) * rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs * Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) * rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs * Value(b.toDouble()); }
                else { throw std::runtime_error("Type Error: Multiplication not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        friend Value operator/(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, BigInt>) {
                    if (b.isZero()) throw std::runtime_error("Math Error: Division by zero.");
                    if ((a % b).isZero()) {
                        return Value(a / b);
                    }
                    return Value(Fraction(a, b));
                }
                else if constexpr (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>) {
                    return lhs * Value(b.inverse());
                }
                else if constexpr (requires { a / b; }) {
                    if constexpr (requires { b == 0.0; }) {
                        if (b == 0.0) throw std::runtime_error("Math Error: Division by zero.");
                    }
                    auto result = a / b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Division by zero complex number.");
                    return lhs.asComplexMatrix() / b;
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a / b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a / BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) / b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) / rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs / Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) / rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs / Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) / rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs / Value(b.toDouble()); }
                else { throw std::runtime_error("Type Error: Division not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        friend Value operator^(const Value& lhs, const Value& rhs) {
            if (lhs.isSymbolic() || rhs.isSymbolic())
                return Value(lhs.asSymbolic() ^ rhs.asSymbolic());
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, double>) {
                    if (a < 0 && std::floor(b) != b) {
                        for (int q = 2; q <= 1000; ++q) {
                            double p = b * q;
                            double rounded = std::round(p);
                            if (Tol::isEq(p, rounded, 1e5)) {
                                int64_t pInt = static_cast<int64_t>(rounded);
                                int64_t g = std::gcd(std::abs(pInt), static_cast<int64_t>(q));
                                int64_t pRed = pInt / g;
                                int64_t qRed = q / g;
                                return Value::negativePow(a, pRed, qRed);
                            }
                        }
                        return Value(Complex(a, 0.0) ^ Complex(b, 0.0));
                    }
                    double res = std::pow(a, b);
                    double rounded = std::round(res);
                    if (Tol::isEq(res, rounded, 1e5) && std::abs(rounded) < 9e15) {
                        return Value(BigInt(static_cast<int64_t>(rounded)));
                    }
                    return Value(res);
                }
                else if constexpr ((std::is_same_v<T1, Complex> && std::is_same_v<T2, Complex>) ||
                    (std::is_same_v<T1, Complex> && std::is_same_v<T2, double>) ||
                    (std::is_same_v<T1, double> && std::is_same_v<T2, Complex>)) {
                    return a ^ b;
                }
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    std::is_same_v<T2, double>) {
                    if (Tol::isEq(b, std::round(b), 1e5)) {
                        return a.power(static_cast<int>(std::round(b)));
                    }
                    return Value((matLog(lhs.asComplexMatrix()) * Complex(b)).matExp());
                }
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    std::is_same_v<T2, Complex>) {
                    return Value((matLog(lhs.asComplexMatrix()) * b).matExp());
                }
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>)) {
                    return Value(matPow(lhs.asComplexMatrix(), rhs.asComplexMatrix()));
                }
                else if constexpr ((std::is_same_v<T1, double> || std::is_same_v<T1, Complex>) &&
                    (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>)) {
                    Complex base_val;
                    if constexpr (std::is_same_v<T1, double>) base_val = Complex(a);
                    else base_val = a;
                    return Value((rhs.asComplexMatrix() * log(base_val)).matExp());
                }
                // ==============================================================
// 精确整数幂：BigInt ^ BigInt
// ==============================================================
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, BigInt>) {
                    if (b.isNegative()) {
                        Fraction res = Fraction(BigInt(1), a).pow(b.abs().toInt64());
                        return Value::fromFraction(res);
                    }
                    return Value(a.pow(b));
                }
                // ==============================================================
                // 精确分数幂：Fraction ^ BigInt
                // ==============================================================
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, BigInt>) {
                    Fraction res = a.pow(b.toInt64());
                    return Value::fromFraction(res);
                }
                // ==============================================================
                // ★ BigInt ^ Fraction：尝试精确开根 + 指数自动规范化
                // ==============================================================
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, Fraction>) {
                    if (b.getDen() == BigInt(1))
                        return lhs ^ Value(b.getNum());

                    int64_t p = 0, q = 0;
                    try {
                        p = b.getNum().toInt64();
                        q = b.getDen().toInt64();
                        if (q < 0) { p = -p; q = -q; }

                        // 尝试彻底算尽（如 8^(1/3) -> 2）
                        auto [ok, val] = Value::tryExactRationalPow(a, BigInt(1), p, q);
                        if (ok) return val;
                    }
                    catch (...) {}

                    // ★ 算不尽时的自动化简：欧几里得带余除法 p = k*q + r (其中 0 <= r < q)
                    if (q > 0) {
                        int64_t k = p / q;
                        int64_t r = p % q;
                        if (r < 0) { r += q; k -= 1; }

                        // 如果能提炼出整数部分 k，则计算 base^k 再乘上剩余根式
                        if (k != 0 && r != 0) {
                            Value exactPart = lhs ^ Value(BigInt(k));
                            Value symPart = Value(SymExpr(a) ^ SymExpr(Fraction(BigInt(r), BigInt(q))));
                            return exactPart * symPart;
                        }
                    }

                    // 完全无法化简，保留最简符号形式
                    return Value(SymExpr(a) ^ SymExpr(b));
                }
                // ==============================================================
                // ★ Fraction ^ Fraction：尝试精确开根 + 指数自动规范化
                // ==============================================================
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, Fraction>) {
                    if (b.getDen() == BigInt(1))
                        return lhs ^ Value(b.getNum());

                    int64_t p = 0, q = 0;
                    try {
                        p = b.getNum().toInt64();
                        q = b.getDen().toInt64();
                        if (q < 0) { p = -p; q = -q; }

                        auto [ok, val] = Value::tryExactRationalPow(a.getNum(), a.getDen(), p, q);
                        if (ok) return val;
                    }
                    catch (...) {}

                    // ★ 同理提取带余除法
                    if (q > 0) {
                        int64_t k = p / q;
                        int64_t r = p % q;
                        if (r < 0) { r += q; k -= 1; }

                        if (k != 0 && r != 0) {
                            Value exactPart = lhs ^ Value(BigInt(k));
                            Value symPart = Value(SymExpr(a) ^ SymExpr(Fraction(BigInt(r), BigInt(q))));
                            return exactPart * symPart;
                        }
                    }

                    // 完全无法化简，保留最简符号形式
                    return Value(SymExpr(a) ^ SymExpr(b));
                }
                // ==============================================================
                // double 参与：走普通浮点科学计算
                // ==============================================================
                else if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, Fraction>) {
                    if (a < 0) {
                        try {
                            int64_t p = b.getNum().toInt64();
                            int64_t q = b.getDen().toInt64();
                            return Value::negativePow(a, p, q);
                        } catch (...) {
                            return Value(Complex(a, 0.0) ^ Complex(b.toDouble(), 0.0));
                        }
                    }
                    double res = std::pow(a, b.toDouble());
                    double rounded = std::round(res);
                    if (Tol::isEq(res, rounded, 1e5) && std::abs(rounded) < 9e15) {
                        return Value(BigInt(static_cast<int64_t>(rounded)));
                    }
                    return Value(res);
                }
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, double>) {
                    double res = std::pow(a.toDouble(), b);
                    double rounded = std::round(res);
                    if (Tol::isEq(res, rounded, 1e5) && std::abs(rounded) < 9e15) {
                        return Value(BigInt(static_cast<int64_t>(rounded)));
                    }
                    return Value(res);
                }
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, double>) {
                    double res = std::pow(a.toDouble(), b);
                    double rounded = std::round(res);
                    if (Tol::isEq(res, rounded, 1e5) && std::abs(rounded) < 9e15) {
                        return Value(BigInt(static_cast<int64_t>(rounded)));
                    }
                    return Value(res);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a ^ b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a ^ BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) ^ b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) ^ rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs ^ Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) ^ rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs ^ Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) ^ rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs ^ Value(b.toDouble()); }
                else {
                    throw std::runtime_error("Type Error: Power operation not supported for these types.");
                }
                }, lhs.data, rhs.data);
        }

        friend Value operator%(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, double>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Modulo by zero.");
                    return std::fmod(a, b);
                }
                else if constexpr (requires { a% b; }) {
                    auto result = a % b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, double>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Modulo by zero.");
                    RealMatrix res(a.getRows(), a.getCols());
                    for (int i = 0; i < a.getRows(); ++i) {
                        for (int j = 0; j < a.getCols(); ++j) {
                            res(i, j) = std::fmod(a(i, j), b);
                        }
                    }
                    return Value(res);
                }
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, double>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Modulo by zero.");
                    ComplexMatrix res(a.getRows(), a.getCols());
                    for (int i = 0; i < a.getRows(); ++i) {
                        for (int j = 0; j < a.getCols(); ++j) {
                            double re = std::fmod(a(i, j).real, b);
                            double im = std::fmod(a(i, j).imag, b);
                            res(i, j) = Complex(re, im);
                        }
                    }
                    return Value(res);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a % b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a % BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) % b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) % rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs % Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) % rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs % Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) % rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs % Value(b.toDouble()); }
                else {
                    throw std::runtime_error("Type Error: Modulo not supported for these types.");
                }
                }, lhs.data, rhs.data);
        }

        friend std::ostream& operator<<(std::ostream& os, const Value& val) {
            static thread_local std::vector<const void*> visited;
            auto printNested = [&os](const Value& v) {
                if (v.isNone()) os << "none";
                else os << v;
                };
            std::visit([&os, &printNested](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {}
                else if constexpr (std::is_same_v<T, double>) {
                    double v = arg;
                    double rounded = std::round(v);
                    if (rounded != 0.0 && v != 0.0 && Tol::isEq(v, rounded, 1e5) && std::abs(rounded) < 1e15) {
                        if (rounded == std::trunc(rounded)) { os << static_cast<int64_t>(rounded); }
                        else { os << rounded; }
                    }
                    else { os << v; }
                }
                else if constexpr (std::is_same_v<T, std::string>) os << arg;
                else if constexpr (std::is_same_v<T, jc::Complex>) os << arg;
                else if constexpr (std::is_same_v<T, jc::RealMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, jc::ComplexMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, BigInt>) os << arg;
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) {
                    if (arg) os << arg->toString();
                    else os << "<function null>";
                }
                else if constexpr (std::is_same_v<T, Fraction>) os << arg;
                else if constexpr (std::is_same_v<T, BaseNum>) os << arg;
                else if constexpr (std::is_same_v<T, jc::StringMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, jc::Dict>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) { os << "{...}"; return; }
                    os << "{";
                    const auto& entries = arg.getEntries();
                    for (size_t ii = 0; ii < entries.size(); ++ii) {
                        try { printNested(entries[ii].first); } catch (...) { os << "?"; }
                        os << ": ";
                        try {
                            const auto& v = entries[ii].second;
                            printNested(v);  // ★ 替换 os << v
                        }
                        catch (...) { os << "?"; }
                        if (ii < entries.size() - 1) os << ", ";
                    }
                    os << "}";
                }
                else if constexpr (std::is_same_v<T, jc::List>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) { os << "[...]"; return; }
                    os << "[";
                    for (size_t ii = 0; ii < arg.size(); ++ii) {
                        try {
                            const auto& v = arg.raw()[ii];
                            printNested(v);  // ★ 替换 os << v
                        }
                        catch (...) { os << "?"; }
                        if (ii < arg.size() - 1) os << ", ";
                    }
                    os << "]";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) {
                    if (!arg) os << "<class null>";
                    else os << "<class " << arg->name << ">";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) {
                    if (!arg) { os << "<instance null>"; return; }
                    RecursionGuard guard(visited, arg.get());
                    std::string cname = arg->classDef ? arg->classDef->name : "unknown";
                    if (guard.isCycle) {
                        os << "<" << cname << " {...}>";
                        return;
                    }
                    bool printedNative = false;
                    if (arg->nativeData.has_value()) {
                        if (arg->nativeData.type() == typeid(std::shared_ptr<Image>)) {
                            auto& img = std::any_cast<std::shared_ptr<Image>&>(arg->nativeData);
                            os << "<Image " << img->width() << "x" << img->height() << ">";
                            printedNative = true;
                        }
                        else if (arg->nativeData.type() == typeid(std::shared_ptr<Distribution>)) {
                            auto& dist = std::any_cast<std::shared_ptr<Distribution>&>(arg->nativeData);
                            os << dist->toString();
                            printedNative = true;
                        }
                    }
                    if (!printedNative) {
                        os << "<" << cname << " {";
                        const auto& entries = arg->fields.getEntries();
                        for (size_t ii = 0; ii < entries.size(); ++ii) {
                            if (ii > 0) os << ", ";
                            try { printNested(entries[ii].first); } catch (...) { os << "?"; }
                            os << ": ";
                            try { printNested(entries[ii].second); }  // ★
                            catch (...) { os << "?"; }
                        }
                        os << "}>";
                    }
                }
                else if constexpr (std::is_same_v<T, jc::Set>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) { os << "Set{...}"; return; }
                    os << "Set{";
                    const auto& elems = arg.raw();
                    for (size_t ii = 0; ii < elems.size(); ++ii) {
                        try {
                            const auto& v = elems[ii];
                            printNested(v);
                        }
                        catch (...) { os << "?"; }
                        if (ii < elems.size() - 1) os << ", ";
                    }
                    os << "}";
                }
                else if constexpr (std::is_same_v<T, SuperProxyPtr>) {
                    os << (arg ? "<super>" : "<super null>");
                }
                else if constexpr (std::is_same_v<T, SymExpr>) {
                    os << arg.toString();
                }
                else static_assert(always_false<T>::value, "Unsupported type for ostream <<");
                }, val.data);
            return os;
        }

        friend Value operator&(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                if constexpr (std::is_same_v<T1, Set> && std::is_same_v<T2, Set>) {
                    Set result;
                    for (const auto& val : a.raw()) {
                        if (b.contains(val)) result.insert(val);
                    }
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a.bitAnd(b)); // 底层的 bitAnd 已经具备校验进制合法性的能力
                }
                else {
                    throw std::runtime_error("Type Error: Bitwise/Set AND '&' not supported for these types.");
                }
                }, lhs.data, rhs.data);
        }

        friend Value operator|(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                if constexpr (std::is_same_v<T1, Set> && std::is_same_v<T2, Set>) {
                    Set result;
                    for (const auto& val : a.raw()) result.insert(val);
                    for (const auto& val : b.raw()) result.insert(val);
                    return Value(result);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a.bitOr(b));
                }
                else {
                    throw std::runtime_error("Type Error: Bitwise/Set OR '|' not supported for these types.");
                }
                }, lhs.data, rhs.data);
        }

        std::string toJC2Expression() const {
            static thread_local std::vector<const void*> visited;
            return std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, std::monostate>) { return "none()"; }
                else if constexpr (std::is_same_v<T, double>) {
                    std::ostringstream oss;
                    oss << std::setprecision(16) << arg;
                    return oss.str();
                }
                else if constexpr (std::is_same_v<T, BaseNum>) {
                    return "base(" + arg.getValue().toString() + ", " + std::to_string(arg.getRadix()) + ")";
                }
                else if constexpr (std::is_same_v<T, BigInt> || std::is_same_v<T, Fraction>) {
                    return arg.toString();
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    return "\"" + arg + "\"";
                }
                else if constexpr (std::is_same_v<T, Complex>) {
                    std::ostringstream oss;
                    oss << "(" << std::setprecision(16) << arg.real << ") + (" << arg.imag << ")*i";
                    return oss.str();
                }
                else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix>) {
                    std::string res = "[";
                    int r = arg.getRows(), c = arg.getCols();
                    for (int i = 0; i < r; ++i) {
                        for (int j = 0; j < c; ++j) {
                            res += Value(arg(i, j)).toJC2Expression();
                            if (j < c - 1) res += ", ";
                        }
                        if (i < r - 1) res += "; ";
                    }
                    res += "]";
                    return res;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) {
                    return arg ? "\"<function>\"" : "\"<function null>\"";
                }
                else if constexpr (std::is_same_v<T, StringMatrix>) {
                    std::string res = "strmat(";
                    int r = arg.getRows(), c = arg.getCols();
                    res += std::to_string(r) + ", " + std::to_string(c);
                    for (int i = 0; i < r; ++i)
                        for (int j = 0; j < c; ++j)
                            res += ", \"" + arg(i, j) + "\"";
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, Dict>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return "dict()";
                    const auto& entries = arg.getEntries();
                    std::string res = "dict(";
                    for (size_t ii = 0; ii < entries.size(); ++ii) {
                        try { res += entries[ii].first.toJC2Expression(); } catch (...) { res += "0"; }
                        res += ", ";
                        try {
                            const auto& v = entries[ii].second;
                            res += v.toJC2Expression();
                        }
                        catch (...) { res += "0"; }
                        if (ii < entries.size() - 1) res += ", ";
                    }
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, List>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return "list()";
                    std::string res = "list(";
                    for (size_t ii = 0; ii < arg.size(); ++ii) {
                        try {
                            const auto& v = arg.raw()[ii];
                            res += v.toJC2Expression();
                        }
                        catch (...) { res += "0"; }
                        if (ii < arg.size() - 1) res += ", ";
                    }
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, Set>) {
                    RecursionGuard guard(visited, arg.id());
                    if (guard.isCycle) return "Set()";
                    std::string res = "Set(";
                    const auto& elems = arg.raw();
                    for (size_t ii = 0; ii < elems.size(); ++ii) {
                        try {
                            const auto& v = elems[ii];
                            res += v.toJC2Expression();
                        }
                        catch (...) { res += "0"; }
                        if (ii < elems.size() - 1) res += ", ";
                    }
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) {
                    return arg ? "\"<class " + arg->name + ">\"" : "\"<class null>\"";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) {
                    if (!arg) return "\"<instance null>\"";
                    std::string cname = arg->classDef ? arg->classDef->name : "unknown";
                    return "\"<" + cname + " instance>\"";
                }
                else if constexpr (std::is_same_v<T, SuperProxyPtr>) {
                    return arg ? "\"<super>\"" : "\"<super null>\"";
                }
                else if constexpr (std::is_same_v<T, SymExpr>) {
                    return "sym(\" " + arg.toString() + "\")";
                }
                else { return "none()"; }
                }, data);
        }

        std::string toString() const {
            if (std::holds_alternative<std::string>(data)) {
                return std::get<std::string>(data);
            }
            std::ostringstream oss;
            oss << *this; // 直接复用我们写好的 operator<< 完美格式化
            return oss.str();
        }
    }; // class Value

    inline Value ldivide(const Value& lhs, const Value& rhs) {
        return std::visit([&](auto&& a, [[maybe_unused]] auto&& b) -> Value {
            using T1 = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) {
                return Value(a.inverse()) * rhs;
            }
            else {
                return rhs / lhs;
            }
            }, lhs.data, rhs.data);
    }

    struct FunctionClosure {
        std::vector<std::string> paramNames;
        std::vector<bool> isRef;
        std::string rawBody;
        std::shared_ptr<Expr> body;
        std::any capturedEnv;
        std::any nativeFn;
        int compiledFnIndex = -1;
        std::vector<Value> defaultValues;
        bool hasRestParam = false;

        // ★ 新增：闭包词法绑定的上下文 (Arrow Function this绑定)
        Value boundSelf = Value::none();
        Value boundClass = Value::none();

        int minArgs() const {
            int count = static_cast<int>(paramNames.size());
            if (hasRestParam && count > 0) {
                count--; // 可变参数本身是可选的
            }
            for (int i = count - 1; i >= 0; --i) {
                if (i < static_cast<int>(defaultValues.size()) && !defaultValues[i].isNone())
                    count--;
                else
                    break;
            }
            return count;
        }
        int maxArgs() const { return static_cast<int>(paramNames.size()); }
        bool acceptsArgCount(int n) const { 
            return n >= minArgs() && (hasRestParam || n <= maxArgs()); 
        }
        bool hasRef() const {
            for (bool b : isRef) if (b) return true;
            return false;
        }
        bool hasCaptures() const { return capturedEnv.has_value(); }

        // ★ 核心改动：isNative 此时仅代表它持有一个 C++ 可调用的回调。
        bool isNative() const { return nativeFn.has_value(); }

        // ★ 核心改动：它是否归属于 VM 字节码
        bool isBytecode() const { return compiledFnIndex >= 0; }

        FunctionClosure(std::vector<std::string> paramNames, std::vector<bool> isRef,
            std::string rawBody, std::shared_ptr<Expr> body, bool hasRestParam = false)  // ★
            : paramNames(std::move(paramNames)), isRef(std::move(isRef)),
            rawBody(std::move(rawBody)), body(std::move(body)), hasRestParam(hasRestParam) {
        }
        FunctionClosure(std::vector<std::string> paramNames, std::vector<bool> isRef,
            std::string rawBody, std::shared_ptr<Expr> body,
            std::any capturedEnv, bool hasRestParam = false)  // ★
            : paramNames(std::move(paramNames)), isRef(std::move(isRef)),
            rawBody(std::move(rawBody)), body(std::move(body)),
            capturedEnv(std::move(capturedEnv)), hasRestParam(hasRestParam) {
        }

        std::string toString() const {
            // isBytecode 的不再直接显示 <function ()> 如果它是通过 nativeFn 构建的壳
            if (isNative() && !isBytecode()) return "<function " + rawBody + "()>";
            std::string params;
            for (size_t i = 0; i < paramNames.size(); ++i) {
                if (isRef[i]) params += "ref ";
                params += paramNames[i];
                if (i < paramNames.size() - 1) params += ", ";
            }
            return "<function(" + params + ")>";
        }
    };

    struct ErrorSignal : public std::runtime_error {
        std::string message;
        explicit ErrorSignal(const std::string& msg) : std::runtime_error(msg), message(msg) {}
    };

    struct StackTracedException : public std::runtime_error {
        std::string rawMessage;

        StackTracedException(const std::string& raw, const std::string& fullTraceText)
            : std::runtime_error(fullTraceText), rawMessage(raw) {}
    };

    inline Value::Value(std::shared_ptr<FunctionClosure> val) {
        if (val) {
            GcHeap::get().track(
                val.get(),
                [w = std::weak_ptr<FunctionClosure>(val)]() { return !w.expired(); },
                [w = std::weak_ptr<FunctionClosure>(val)]() {
                    auto sp = w.lock();
                    if (sp) {
                        sp->boundSelf = Value::none();
                        sp->boundClass = Value::none();
                        sp->capturedEnv.reset();
                    }
                }
            );
        }
        data = std::move(val);
    }

struct DictData {
    std::vector<std::pair<Value, Value>> elements;
    std::unordered_map<Value, size_t, ValueHasher, ValueEqual> keyMap;
    bool is_frozen = false;
};

struct ListData {
    std::vector<Value> vec;
    bool is_frozen = false;
};

struct SetData {
    std::vector<Value> elements;
    std::unordered_set<Value, ValueHasher, ValueEqual> keys;
    bool is_frozen = false;
};

inline Dict::Dict() : ptr(std::make_shared<DictData>()) {
    GcHeap::get().track(
        ptr.get(),
        [w = std::weak_ptr<DictData>(ptr)]() { return !w.expired(); },
        [w = std::weak_ptr<DictData>(ptr)]() {
            auto sp = w.lock(); if (sp) { sp->elements.clear(); sp->keyMap.clear(); }
        }
    );
}
inline void Dict::freeze() { ptr->is_frozen = true; }
inline bool Dict::isFrozen() const { return ptr->is_frozen; }
inline size_t Dict::size() const { return ptr->elements.size(); }
inline bool Dict::empty() const { return ptr->elements.empty(); }
inline void Dict::clear() {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    ptr->elements.clear();
    ptr->keyMap.clear();
}

inline List::List() : ptr(std::make_shared<ListData>()) {
    GcHeap::get().track(
        ptr.get(),
        [w = std::weak_ptr<ListData>(ptr)]() { return !w.expired(); },
        [w = std::weak_ptr<ListData>(ptr)]() {
            auto sp = w.lock(); if (sp) sp->vec.clear();
        }
    );
}
inline void List::freeze() { ptr->is_frozen = true; }
inline bool List::isFrozen() const { return ptr->is_frozen; }
inline size_t List::size() const { return ptr->vec.size(); }
inline bool List::empty() const { return ptr->vec.empty(); }
inline void List::clear() {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    ptr->vec.clear();
}

inline Set::Set() : ptr(std::make_shared<SetData>()) {
    GcHeap::get().track(
        ptr.get(),
        [w = std::weak_ptr<SetData>(ptr)]() { return !w.expired(); },
        [w = std::weak_ptr<SetData>(ptr)]() {
            auto sp = w.lock();
            if (sp) { sp->elements.clear(); sp->keys.clear(); }
        }
    );
}
inline void Set::freeze() { ptr->is_frozen = true; }
inline bool Set::isFrozen() const { return ptr->is_frozen; }
inline size_t Set::size() const { return ptr->elements.size(); }
inline bool Set::empty() const { return ptr->elements.empty(); }
inline void Set::clear() {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    ptr->elements.clear(); ptr->keys.clear();
}

inline size_t ValueHasher::operator()(const Value& v) const {
    static thread_local std::vector<const void*> visited;

    if (v.isString()) return std::hash<std::string>{}(std::get<std::string>(v.data));
    if (v.isNone()) return 0;
    
    if (std::holds_alternative<std::shared_ptr<Instance>>(v.data)) {
        auto inst = std::get<std::shared_ptr<Instance>>(v.data);
        RecursionGuard guard(visited, inst.get());
        if (guard.isCycle) return 0;
        auto [found, res] = invokeDunder(inst, "__hash__");
        if (found) return operator()(res); // 递归哈希其返回值（如数字或字符串）
        return std::hash<const void*>{}(inst.get());
    }
    if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(v.data)) return std::hash<const void*>{}(std::get<std::shared_ptr<ClassDefinition>>(v.data).get());
    if (std::holds_alternative<std::shared_ptr<FunctionClosure>>(v.data)) return std::hash<const void*>{}(std::get<std::shared_ptr<FunctionClosure>>(v.data).get());

    if (std::holds_alternative<BaseNum>(v.data)) {
        return operator()(Value(std::get<BaseNum>(v.data).getValue()));
    }

    if (std::holds_alternative<List>(v.data)) {
        auto& l = std::get<List>(v.data);
        RecursionGuard guard(visited, l.id());
        if (guard.isCycle) return 0;
        size_t h = 0;
        for (const auto& e : l.raw()) {
            h ^= operator()(e) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
    if (std::holds_alternative<Dict>(v.data)) {
        auto& d = std::get<Dict>(v.data);
        RecursionGuard guard(visited, d.id());
        if (guard.isCycle) return 0;
        size_t h = 0;
        for (const auto& [k, val] : d.getEntries()) {
            size_t kv_hash = operator()(k);
            kv_hash ^= operator()(val) + 0x9e3779b9 + (kv_hash << 6) + (kv_hash >> 2);
            // MurmurHash3 风格的雪崩扰乱，打散位分布
            kv_hash ^= (kv_hash >> 16);
            kv_hash *= 0x85ebca6b;
            kv_hash ^= (kv_hash >> 13);
            kv_hash *= 0xc2b2ae35;
            kv_hash ^= (kv_hash >> 16);
            // 满足交换律的累加，彻底无视插入顺序
            h += kv_hash;
        }
        return h;
    }
    if (std::holds_alternative<Set>(v.data)) {
        auto& s = std::get<Set>(v.data);
        RecursionGuard guard(visited, s.id());
        if (guard.isCycle) return 0;
        size_t h = 0;
        for (const auto& val : s.raw()) {
            size_t e_hash = operator()(val);
            // 雪崩扰乱
            e_hash ^= (e_hash >> 16);
            e_hash *= 0x85ebca6b;
            e_hash ^= (e_hash >> 13);
            e_hash *= 0xc2b2ae35;
            e_hash ^= (e_hash >> 16);
            // 满足交换律的累加
            h += e_hash;
        }
        return h;
    }

    if (std::holds_alternative<RealMatrix>(v.data)) {
        const auto& m = std::get<RealMatrix>(v.data);
        size_t h = std::hash<int>{}(m.getRows()) ^ (std::hash<int>{}(m.getCols()) << 1);
        for (double d : m.rawData()) h ^= operator()(Value(d)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
    if (std::holds_alternative<ComplexMatrix>(v.data)) {
        const auto& m = std::get<ComplexMatrix>(v.data);
        size_t h = std::hash<int>{}(m.getRows()) ^ (std::hash<int>{}(m.getCols()) << 1);
        for (const Complex& c : m.rawData()) h ^= operator()(Value(c)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
    if (std::holds_alternative<StringMatrix>(v.data)) {
        const auto& m = std::get<StringMatrix>(v.data);
        size_t h = std::hash<int>{}(m.getRows()) ^ (std::hash<int>{}(m.getCols()) << 1);
        for (const std::string& s : m.rawData()) h ^= operator()(Value(s)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
    if (std::holds_alternative<SymExpr>(v.data)) {
        const auto& sym = std::get<SymExpr>(v.data);
        if (sym.ptr) return std::hash<std::string>{}(sym.ptr->getSignature());
        return 0;
    }

    try {
        Complex c = v.asComplex();
        double r = (c.real == 0.0) ? 0.0 : c.real; // 抹平 -0.0
        if (r == std::round(r)) r = std::round(r); // 抹平 1.0 与 1 的差异
        double i = (c.imag == 0.0) ? 0.0 : c.imag;
        if (i == std::round(i)) i = std::round(i);
        size_t h1 = std::hash<double>{}(r);
        size_t h2 = std::hash<double>{}(i);
        return h1 ^ (h2 << 1);
    } catch (...) {
        return std::hash<std::string>{}(v.toJC2Expression());
    }
}

inline bool ValueEqual::operator()(const Value& lhs, const Value& rhs) const {
    return Value::equals(lhs, rhs);
}

inline void Dict::set(const Value& key, const Value& val) {
    if (!key.isHashable()) throw std::runtime_error("TypeError: unhashable type as dict key.");
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    auto it = ptr->keyMap.find(key);
    if (it != ptr->keyMap.end()) {
        ptr->elements[it->second].second = val;
    } else {
        ptr->keyMap[key] = ptr->elements.size();
        ptr->elements.push_back({key, val});
    }
}

inline Value* Dict::get(const Value& key) {
    auto it = ptr->keyMap.find(key);
    if (it != ptr->keyMap.end()) return &ptr->elements[it->second].second;
    return nullptr;
}

inline const Value* Dict::get(const Value& key) const {
    auto it = ptr->keyMap.find(key);
    if (it != ptr->keyMap.end()) return &ptr->elements[it->second].second;
    return nullptr;
}

inline bool Dict::has(const Value& key) const {
    if (!key.isHashable()) throw std::runtime_error("TypeError: unhashable type as dict key.");
    return ptr->keyMap.find(key) != ptr->keyMap.end();
}

inline bool Dict::remove(const Value& key) {
    if (!key.isHashable()) throw std::runtime_error("TypeError: unhashable type as dict key.");
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    auto it = ptr->keyMap.find(key);
    if (it == ptr->keyMap.end()) return false;
    size_t idx = it->second;
    ptr->keyMap.erase(it);
    ptr->elements.erase(ptr->elements.begin() + idx);
    for (size_t i = idx; i < ptr->elements.size(); ++i) {
        ptr->keyMap[ptr->elements[i].first] = i;
    }
    return true;
}

inline std::vector<Value> Dict::getKeys() const {
    std::vector<Value> keys;
    keys.reserve(ptr->elements.size());
    for (const auto& kv : ptr->elements) keys.push_back(kv.first);
    return keys;
}

inline std::vector<std::pair<Value, Value>> Dict::getEntries() const {
    return ptr->elements;
}

inline bool Set::insert(const Value& val) {
    if (!val.isHashable()) throw std::runtime_error("TypeError: unhashable type as set element.");
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    if (ptr->keys.count(val)) return false;
    ptr->keys.insert(val);
    ptr->elements.push_back(val);
    return true;
}

inline bool Set::contains(const Value& val) const {
    if (!val.isHashable()) throw std::runtime_error("TypeError: unhashable type as set element.");
    return ptr->keys.count(val) > 0;
}

inline bool Set::erase(const Value& val) {
    if (!val.isHashable()) throw std::runtime_error("TypeError: unhashable type as set element.");
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    if (!ptr->keys.erase(val)) return false;
    for (auto it = ptr->elements.begin(); it != ptr->elements.end(); ++it) {
        if (Value::equals(*it, val)) { ptr->elements.erase(it); return true; }
    }
    return true;
}

inline const std::vector<Value>& Set::raw() const {
    return ptr->elements;
}

inline void List::push_back(const Value& val) {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    ptr->vec.push_back(val);
}

inline Value& List::at(int idx) {
    int n = static_cast<int>(ptr->vec.size());
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n)
        throw std::out_of_range("List Error: Index out of bounds.");
    return ptr->vec[idx];
}

inline const Value& List::at(int idx) const {
    int n = static_cast<int>(ptr->vec.size());
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n)
        throw std::out_of_range("List Error: Index out of bounds.");
    return ptr->vec[idx];
}

inline void List::set(int idx, const Value& val) {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    int n = static_cast<int>(ptr->vec.size());
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n)
        throw std::out_of_range("List Error: Index out of bounds.");
    ptr->vec[idx] = val;
}

inline void List::insert(int idx, const Value& val) {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    int n = static_cast<int>(ptr->vec.size());
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx > n)
        throw std::out_of_range("List Error: Insert index out of bounds.");
    ptr->vec.insert(ptr->vec.begin() + idx, val);
}

inline void List::removeAt(int idx) {
    if (ptr->is_frozen) throw std::runtime_error("Runtime Error: Object is frozen.");
    int n = static_cast<int>(ptr->vec.size());
    if (idx < 0) idx = n + idx;
    if (idx < 0 || idx >= n)
        throw std::out_of_range("List Error: Remove index out of bounds.");
    ptr->vec.erase(ptr->vec.begin() + idx);
}

inline const std::vector<Value>& List::raw() const { return ptr->vec; }
inline std::vector<Value>& List::raw() { return ptr->vec; }

} // namespace jc

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#endif // JC2_VALUE_H
