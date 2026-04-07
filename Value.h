#ifndef JC2_VALUE_H
#define JC2_VALUE_H

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4702) // unreachable code (MSVC if-constexpr false positive)
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
#include "Complex.h"
#include "Matrix.h"
#include "BigInt.h"
#include "Expr.h"
#include "Fraction.h"
#include "Base.h"
#include "Tolerance.h"
#include "Image.h"
#include "Probability.h"      // ★ 新增

namespace jc {

    struct FunctionClosure; 

    class Dict {
    private:
        std::vector<std::pair<std::string, std::any>> entries;
    public:
        Dict() = default;

        void set(const std::string& key, const std::any& val) {
            for (auto& [k, v] : entries) {
                if (k == key) { v = val; return; }
            }
            entries.push_back({ key, val });
        }

        std::any* get(const std::string& key) {
            for (auto& [k, v] : entries) {
                if (k == key) return &v;
            }
            return nullptr;
        }

        const std::any* get(const std::string& key) const {
            for (const auto& [k, v] : entries) {
                if (k == key) return &v;
            }
            return nullptr;
        }

        bool has(const std::string& key) const {
            for (const auto& [k, v] : entries)
                if (k == key) return true;
            return false;
        }

        bool remove(const std::string& key) {
            for (auto it = entries.begin(); it != entries.end(); ++it) {
                if (it->first == key) { entries.erase(it); return true; }
            }
            return false;
        }

        size_t size() const { return entries.size(); }
        bool empty() const { return entries.empty(); }

        std::vector<std::string> getKeys() const {
            std::vector<std::string> keys;
            for (const auto& [k, v] : entries) keys.push_back(k);
            return keys;
        }

        const std::vector<std::pair<std::string, std::any>>& getEntries() const {
            return entries;
        }
    };

    class List {
    private:
        std::vector<std::any> elements;
    public:
        List() = default;

        void push_back(const std::any& val) { elements.push_back(val); }

        std::any& at(int idx) {
            int n = static_cast<int>(elements.size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index " + std::to_string(idx) + " out of bounds [0, " + std::to_string(n - 1) + "].");
            return elements[idx];
        }

        const std::any& at(int idx) const {
            int n = static_cast<int>(elements.size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index " + std::to_string(idx) + " out of bounds [0, " + std::to_string(n - 1) + "].");
            return elements[idx];
        }

        void set(int idx, const std::any& val) {
            int n = static_cast<int>(elements.size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index out of bounds.");
            elements[idx] = val;
        }

        void insert(int idx, const std::any& val) {
            int n = static_cast<int>(elements.size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx > n)
                throw std::out_of_range("List Error: Insert index out of bounds.");
            elements.insert(elements.begin() + idx, val);
        }

        void removeAt(int idx) {
            int n = static_cast<int>(elements.size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Remove index out of bounds.");
            elements.erase(elements.begin() + idx);
        }

        size_t size() const { return elements.size(); }
        bool empty() const { return elements.empty(); }
        const std::vector<std::any>& raw() const { return elements; }
        std::vector<std::any>& raw() { return elements; }
    };

    // ★ ═══ Class & Instance ═══
    struct ClassDefinition {
        std::string name;
        std::shared_ptr<ClassDefinition> parent;  // ★ nullptr = no parent
        std::map<std::string, std::shared_ptr<FunctionClosure>> methods;
    };

    struct Instance {
        std::shared_ptr<ClassDefinition> classDef;
        Dict fields;
        std::any nativeData;  // ★ 原生 C++ 对象（Image, Distribution 等）
    };

    struct SuperProxy {
        std::shared_ptr<Instance> instance;
        std::shared_ptr<ClassDefinition> parentClass;
    };

    using SuperProxyPtr = std::shared_ptr<SuperProxy>;

    using ValueVariant = std::variant<
        std::monostate, double, BigInt, BaseNum, Fraction, std::string,
        Complex, RealMatrix, ComplexMatrix, StringMatrix, Dict, List,
        std::shared_ptr<FunctionClosure>,
        std::shared_ptr<ClassDefinition>,   // ★ 新增
        std::shared_ptr<Instance>,
        SuperProxyPtr // ★ 新增
    >;

    template<typename> struct always_false : std::false_type {};

    class Value {
    private:
        Value(std::monostate) : data(std::monostate{}) {}
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
        Value(std::shared_ptr<FunctionClosure> val) : data(std::move(val)) {}
        Value(Fraction val) : data(std::move(val)) {}
        Value(BaseNum val) : data(std::move(val)) {}
        Value(StringMatrix val) : data(std::move(val)) {}
        Value(Dict val) : data(std::move(val)) {}
        Value(List val) : data(std::move(val)) {}
        Value(std::shared_ptr<ClassDefinition> val) : data(std::move(val)) {}  // ★
        Value(std::shared_ptr<Instance> val) : data(std::move(val)) {}          // ★

        // 辅助方法
        bool isInstance() const { return std::holds_alternative<std::shared_ptr<Instance>>(data); }    // ★
        bool isClass() const { return std::holds_alternative<std::shared_ptr<ClassDefinition>>(data); } // ★
        std::shared_ptr<Instance> asInstance() const {  // ★
            if (!isInstance()) throw std::runtime_error("Type Error: Expected an instance.");
            return std::get<std::shared_ptr<Instance>>(data);
        }

        static Value negativePow(double base, int64_t p, int64_t q) {
            // base 必须 < 0，传入时已保证
            double absBase = std::abs(base);
            if (q % 2 != 0) {
                // 奇数根在实数域有定义：(-|a|)^(p/q) = (-1)^p * |a|^(p/q)
                double magnitude = std::pow(absBase, static_cast<double>(std::abs(p)) / static_cast<double>(q));
                if (p < 0) magnitude = 1.0 / magnitude;
                // p 为奇数时结果取负
                bool negResult = (std::abs(p) % 2 != 0);
                return Value(negResult ? -magnitude : magnitude);
            }
            else {
                // 偶数根 → 复数
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

        // --- 工具函数族 (用于强行从 Variant 抽离数值) ---

        double asDouble() const {
            if (std::holds_alternative<double>(data)) return std::get<double>(data);
            if (std::holds_alternative<BigInt>(data)) return std::get<BigInt>(data).toDouble();
            if (std::holds_alternative<Fraction>(data)) return std::get<Fraction>(data).toDouble(); // <--- 新增
            if (std::holds_alternative<BaseNum>(data)) return std::get<BaseNum>(data).getValue().toDouble(); // <--- 追加
            throw std::runtime_error("Type Error: Expected a real number.");
        }

        Complex asComplex() const {
            if (std::holds_alternative<Complex>(data)) return std::get<Complex>(data);
            if (std::holds_alternative<double>(data)) return Complex(std::get<double>(data));
            if (std::holds_alternative<BigInt>(data)) return Complex(std::get<BigInt>(data).toDouble());
            if (std::holds_alternative<Fraction>(data)) return Complex(std::get<Fraction>(data).toDouble()); // <--- 新增
            if (std::holds_alternative<BaseNum>(data)) return Complex(std::get<BaseNum>(data).getValue().toDouble()); // <--- 追加
            throw std::runtime_error("Type Error: Expected a number or complex.");
        }

        BigInt asBigInt() const {
            if (std::holds_alternative<BigInt>(data)) return std::get<BigInt>(data);
            if (std::holds_alternative<double>(data)) {
                double val = std::get<double>(data);
                if (std::abs(val) > 9.22337e18) { // 超过 int64_t 最大安全承载边界
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
            // 如果它本来是个实数矩阵，强行把它每一项转成复数，升维！
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

        // ===============================================
        // 动态计算核心 (Dynamic Type Promotion)
        // ===============================================

        Value operator-() const {
            return std::visit([](auto&& a) -> Value {
                if constexpr (requires { -a; }) { return -a; }
                else { throw std::runtime_error("Type Error: Cannot negate this type."); }
                }, data);
        }

        // 加法：我们加入了类型升维的防脱节挂钩
        friend Value operator+(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                if constexpr (requires { a + b; }) {
                    auto result = a + b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result; // 本身就能相加（比如 double+double, RealMat+RealMat, ComplexMat+ComplexMat，甚至 double+Complex，因为你的 Complex.h 重载过它！）
                }
                // 升维 1：实数矩阵 + 复数矩阵
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, ComplexMatrix>) {
                    return lhs.asComplexMatrix() + rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, RealMatrix>) {
                    return lhs.asComplexMatrix() + rhs.asComplexMatrix();
                }
                // 升维 2：实数矩阵 + 复数标量 (你的 Matrix 泛型不支持和不同标量加，必须转)
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    return lhs.asComplexMatrix() + b;
                }
                else if constexpr (std::is_same_v<T1, Complex> && std::is_same_v<T2, RealMatrix>) {
                    return a + rhs.asComplexMatrix();
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a + b); // 调用 BaseNum 的内置 operator+ 
                }
                // BaseNum 与 BigInt/double 混合 -> 被吸收入 Base 宇宙！
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a + BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) + b);
                }
                // 通用 BaseNum 降维：一旦参与运算，立刻打散成 BigInt 进行高精度结算！
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

        // 减法
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
                    return Value(a - b); // 调用 BaseNum 的内置 operator- 
                }
                // BaseNum 与 BigInt/double 混合 -> 被吸收入 Base 宇宙！
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a - BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) - b);
                }
                // 通用 BaseNum 降维：一旦参与运算，立刻打散成 BigInt 进行高精度结算！
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) - rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs - Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) - rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs - Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) - rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs - Value(b.toDouble()); }
                else { throw std::runtime_error("Type Error: Subtraction not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        // 乘法
        friend Value operator*(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;
                if constexpr (requires { a * b; }) {
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
                    return Value(a * b); // 调用 BaseNum 的内置 operator* 
                }
                // BaseNum 与 BigInt/double 混合 -> 被吸收入 Base 宇宙！
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a * BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) * b);
                }
                // 通用 BaseNum 降维：一旦参与运算，立刻打散成 BigInt 进行高精度结算！
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) * rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs * Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) * rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs * Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) * rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs * Value(b.toDouble()); }
                else { throw std::runtime_error("Type Error: Multiplication not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        // 双目除法 (A / B)
        friend Value operator/(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                // 1. 无损分数升级（拦截大整数除大整数）
                if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, BigInt>) {
                    if (b.isZero()) throw std::runtime_error("Math Error: Division by zero.");
                    if ((a % b).isZero()) {
                        return Value(a / b);  // BigInt / BigInt = BigInt（整除）
                    }
                    return Value(Fraction(a, b));
                }

                // 2. 右侧是矩阵：矩阵除法在数学上的等价替代 -> A / B = A * B^(-1)
                // 不管前面是什么类型，只要被除数是矩阵，我们就用已有的乘法算逆阵！
                else if constexpr (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>) {
                    return lhs * Value(b.inverse());
                }

                // 3. 常规运算 (同类型标量相除、矩阵除以标量等，底层天然支持 / 运算符的)
                else if constexpr (requires { a / b; }) {
                    if constexpr (requires { b == 0.0; }) {
                        if (b == 0.0) throw std::runtime_error("Math Error: Division by zero.");
                    }
                    auto result = a / b;
                    // ★ Fraction 归一化：分母为1时降级为BigInt（与+,-,*,%一致）
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }

                // 4. 左侧实数矩阵，右侧复数标量：矩阵升维后再除
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, Complex>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Division by zero complex number.");
                    return lhs.asComplexMatrix() / b;
                }

                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a / b); // 调用 BaseNum 的内置 operator/ 
                }
                // BaseNum 与 BigInt/double 混合 -> 被吸收入 Base 宇宙！
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a / BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) / b);
                }
                // 通用 BaseNum 降维：一旦参与运算，立刻打散成 BigInt 进行高精度结算！
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) / rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs / Value(b.getValue()); }

                // 5. 通用 BigInt 降维为 double 
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) / rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs / Value(b.toDouble()); }

                // 6. 通用 Fraction 降维为 double
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) / rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs / Value(b.toDouble()); }

                else { throw std::runtime_error("Type Error: Division not supported for these types."); }
                }, lhs.data, rhs.data);
        }

        friend Value operator^(const Value& lhs, const Value& rhs) {
            return std::visit([&](auto&& a, auto&& b) -> Value {
                using T1 = std::decay_t<decltype(a)>;
                using T2 = std::decay_t<decltype(b)>;

                // --- 纯标量 double ^ double ---
                if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, double>) {
                    if (a < 0 && std::floor(b) != b) {
                        // ★ 负底数非整数指数：尝试恢复分数信息
                        // 寻找最近的 p/q（q ≤ 1000）
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
                        // 无法恢复 → 升维复数
                        return Value(Complex(a, 0.0) ^ Complex(b, 0.0));
                    }
                    return std::pow(a, b);
                }
                // --- 复数乘方（保持不变）---
                else if constexpr ((std::is_same_v<T1, Complex> && std::is_same_v<T2, Complex>) ||
                    (std::is_same_v<T1, Complex> && std::is_same_v<T2, double>) ||
                    (std::is_same_v<T1, double> && std::is_same_v<T2, Complex>)) {
                    return a ^ b;
                }
                // --- 矩阵 ^ 实数（保持不变）---
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    std::is_same_v<T2, double>) {
                    if (Tol::isEq(b, std::round(b), 1e5)) {
                        return a.power(static_cast<int>(std::round(b)));
                    }
                    return Value((matLog(lhs.asComplexMatrix()) * Complex(b)).matExp());
                }
                // --- 矩阵 ^ 复数（保持不变）---
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    std::is_same_v<T2, Complex>) {
                    return Value((matLog(lhs.asComplexMatrix()) * b).matExp());
                }
                // --- 矩阵 ^ 矩阵（保持不变）---
                else if constexpr ((std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) &&
                    (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>)) {
                    return Value(matPow(lhs.asComplexMatrix(), rhs.asComplexMatrix()));
                }
                // --- 标量 ^ 矩阵（保持不变）---
                else if constexpr ((std::is_same_v<T1, double> || std::is_same_v<T1, Complex>) &&
                    (std::is_same_v<T2, RealMatrix> || std::is_same_v<T2, ComplexMatrix>)) {
                    Complex base_val;
                    if constexpr (std::is_same_v<T1, double>) base_val = Complex(a);
                    else base_val = a;
                    return Value((rhs.asComplexMatrix() * log(base_val)).matExp());
                }
                // --- BigInt ^ BigInt（已有，保持不变）---
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, BigInt>) {
                    if (b.isNegative()) {
                        Fraction res = Fraction(BigInt(1), a).pow(static_cast<int64_t>(b.abs().toDouble()));
                        return Value::fromFraction(res);
                    }
                    return Value(a.pow(b));
                }
                // --- Fraction ^ BigInt（已有，保持不变）---
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, BigInt>) {
                    Fraction res = a.pow(static_cast<int64_t>(b.toDouble()));
                    return Value::fromFraction(res);
                }

                // --- BigInt ^ Fraction（精确拦截层）---
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, Fraction>) {
                    int64_t p = static_cast<int64_t>(b.getNum().toDouble());
                    int64_t q = static_cast<int64_t>(b.getDen().toDouble());

                    if (a.isNegative()) {
                        return Value::negativePow(a.toDouble(), p, q);
                    }
                    if (a.isZero() && p <= 0) throw std::runtime_error("Math Error: Base 0 requires positive exponent.");
                    return Value(std::pow(a.toDouble(), static_cast<double>(p) / static_cast<double>(q)));
                }
                // --- Fraction ^ Fraction ---
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, Fraction>) {
                    double base = a.toDouble();
                    int64_t p = static_cast<int64_t>(b.getNum().toDouble());
                    int64_t q = static_cast<int64_t>(b.getDen().toDouble());

                    if (base < 0) {
                        return Value::negativePow(base, p, q);
                    }
                    return Value(std::pow(base, static_cast<double>(p) / static_cast<double>(q)));
                }
                // --- double ^ Fraction ---
                else if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, Fraction>) {
                    int64_t p = static_cast<int64_t>(b.getNum().toDouble());
                    int64_t q = static_cast<int64_t>(b.getDen().toDouble());

                    if (a < 0) {
                        return Value::negativePow(a, p, q);
                    }
                    return Value(std::pow(a, static_cast<double>(p) / static_cast<double>(q)));
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
                // 1. 纯 double
                if constexpr (std::is_same_v<T1, double> && std::is_same_v<T2, double>) {
                    if (b == 0.0) throw std::runtime_error("Math Error: Modulo by zero.");
                    return std::fmod(a, b); // C-style
                }
                // 2. 同类型自带 % 的（BigInt%BigInt, BaseNum%BaseNum 等）
                else if constexpr (requires { a% b; }) {
                    auto result = a % b;
                    if constexpr (std::is_same_v<decltype(result), Fraction>) {
                        return Value::fromFraction(result);
                    }
                    return result;
                }
                // =========================================================================
                // 3. 【修复漏洞】实数矩阵 % 纯实数标量 (Broadcast Element-wise Modulo)
                // =========================================================================
                else if constexpr (std::is_same_v<T1, RealMatrix> && std::is_same_v<T2, double>) {
                    if (jc::Tol::isEq(b, 0.0)) throw std::runtime_error("Math Error: Modulo by zero.");
                    RealMatrix res(a.getRows(), a.getCols());
                    for (int i = 0; i < a.getRows(); ++i) {
                        for (int j = 0; j < a.getCols(); ++j) {
                            res(i, j) = std::fmod(a(i, j), b); // 与操作符 % 定义保持一致 (C-Style 符号位保留)
                        }
                    }
                    return Value(res);
                }
                // =========================================================================
                // 4. 【修复漏洞】复数矩阵 % 纯实数标量 
                // =========================================================================
                else if constexpr (std::is_same_v<T1, ComplexMatrix> && std::is_same_v<T2, double>) {
                    if (jc::Tol::isEq(b, 0.0)) throw std::runtime_error("Math Error: Modulo by zero.");
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
                // 5. BaseNum 同化
                else if constexpr (std::is_same_v<T1, BaseNum> && std::is_same_v<T2, BaseNum>) {
                    return Value(a % b);
                }
                else if constexpr (std::is_same_v<T1, BaseNum> && (std::is_same_v<T2, double> || std::is_same_v<T2, BigInt>)) {
                    return Value(a % BaseNum(rhs.asBigInt(), 10));
                }
                else if constexpr (std::is_same_v<T2, BaseNum> && (std::is_same_v<T1, double> || std::is_same_v<T1, BigInt>)) {
                    return Value(BaseNum(lhs.asBigInt(), 10) % b);
                }
                // 6. 通用交互式降维处理梯队（巧妙连环：如果 T2 是 BigInt 或 Fraction，它将沉降成 double，然后递归再次触发上面的矩阵分支！）
                else if constexpr (std::is_same_v<T1, BaseNum>) { return Value(a.getValue()) % rhs; }
                else if constexpr (std::is_same_v<T2, BaseNum>) { return lhs % Value(b.getValue()); }
                else if constexpr (std::is_same_v<T1, BigInt>) { return Value(a.toDouble()) % rhs; }
                else if constexpr (std::is_same_v<T2, BigInt>) { return lhs % Value(b.toDouble()); }
                else if constexpr (std::is_same_v<T1, Fraction>) { return Value(a.toDouble()) % rhs; }
                else if constexpr (std::is_same_v<T2, Fraction>) { return lhs % Value(b.toDouble()); }
                // 7. 终极壁垒防线
                else {
                    throw std::runtime_error("Type Error: Modulo not supported for these types.");
                }
                }, lhs.data, rhs.data);
        }

        friend std::ostream& operator<<(std::ostream& os, const Value& val) {

            std::visit([&os](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    // 空值
                }
                else if constexpr (std::is_same_v<T, double>) {
                    double v = arg;
                    double rounded = std::round(v);

                    // 使用引擎判定：非绝对0 且 与整数非常接近 且 不超过int64范围
                    if (!Tol::isEq(rounded, 0.0)
                        && !Tol::isEq(v, 0.0)
                        && Tol::isEq(v, rounded, 1e5)  // 放宽 ULP 至 1e5 做视觉吸附
                        && std::abs(rounded) < 1e15) {

                        if (rounded == std::trunc(rounded)) {
                            os << static_cast<int64_t>(rounded);
                        }
                        else {
                            os << rounded;
                        }
                    }
                    else {
                        os << v;
                    }
                }
                else if constexpr (std::is_same_v<T, std::string>) os << arg;
                else if constexpr (std::is_same_v<T, jc::Complex>) os << arg;  // 走 Complex::operator<< 已清洗
                else if constexpr (std::is_same_v<T, jc::RealMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, jc::ComplexMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, BigInt>) os << arg;
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) os << arg->toString();
                else if constexpr (std::is_same_v<T, Fraction>) os << arg;
                else if constexpr (std::is_same_v<T, BaseNum>) os << arg;
                else if constexpr (std::is_same_v<T, jc::StringMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, jc::Dict>) {
                    os << "{";
                    const auto& entries = arg.getEntries();
                    for (size_t ii = 0; ii < entries.size(); ++ii) {
                        os << "\"" << entries[ii].first << "\": ";
                        try {
                            const auto& v = std::any_cast<const jc::Value&>(entries[ii].second);
                            os << v;
                        }
                        catch (...) { os << "?"; }
                        if (ii < entries.size() - 1) os << ", ";
                    }
                    os << "}";
                }
                else if constexpr (std::is_same_v<T, jc::List>) {
                    os << "[";
                    for (size_t ii = 0; ii < arg.size(); ++ii) {
                        try {
                            const auto& v = std::any_cast<const jc::Value&>(arg.raw()[ii]);
                            os << v;
                        }
                        catch (...) { os << "?"; }
                        if (ii < arg.size() - 1) os << ", ";
                    }
                    os << "]";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) {
                    os << "<class " << arg->name << ">";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) {
                    if (arg->nativeData.has_value()) {
                        if (arg->nativeData.type() == typeid(std::shared_ptr<Image>)) {
                            auto& img = std::any_cast<std::shared_ptr<Image>&>(arg->nativeData);
                            os << "<Image " << img->width() << "x" << img->height() << ">";
                        }
                        else if (arg->nativeData.type() == typeid(std::shared_ptr<Distribution>)) {
                            auto& dist = std::any_cast<std::shared_ptr<Distribution>&>(arg->nativeData);
                            os << dist->toString();
                        }
                        else { goto printInstance; }
                    }
                    else {
                    printInstance:
                        os << "<" << arg->classDef->name << " {";
                        const auto& entries = arg->fields.getEntries();
                        for (size_t ii = 0; ii < entries.size(); ++ii) {
                            if (ii > 0) os << ", ";
                            os << entries[ii].first << ": ";
                            try { os << std::any_cast<const jc::Value&>(entries[ii].second); }
                            catch (...) { os << "?"; }
                        }
                        os << "}>";
                    }
                }
                else if constexpr (std::is_same_v<T, SuperProxyPtr>) {
                    os << "<super>";
                }
                else static_assert(always_false<T>::value, "Unsupported type for ostream <<");
                }, val.data);
            return os;
        }

// =================================================================
// 逆向反编译引擎：将内存对象完美还原为 JC2 合法代码 (Serialization)
// =================================================================
        std::string toJC2Expression() const {
            return std::visit([](auto&& arg) -> std::string {
                using T = std::decay_t<decltype(arg)>;

                if constexpr (std::is_same_v<T, std::monostate>) {
                    return "0"; // 空值丢弃
                }
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
                    // 字符串必须加双引号，才能在 parse 时还原为 STRING token
                    return "\"" + arg + "\"";
                }
                else if constexpr (std::is_same_v<T, Complex>) {
                    // 把复数拆解成精准的代码形式： (3) + (4)*i
                    std::ostringstream oss;
                    oss << "(" << std::setprecision(16) << arg.real << ") + (" << arg.imag << ")*i";
                    return oss.str();
                }
                else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix>) {
                    // 把矩阵拆解成标准的 [a, b; c, d] 代码列表
                    std::string res = "[";
                    int r = arg.getRows(), c = arg.getCols();
                    for (int i = 0; i < r; ++i) {
                        for (int j = 0; j < c; ++j) {
                            // 递归调用，把矩阵里的元素变成了完美的代码
                            res += Value(arg(i, j)).toJC2Expression();
                            if (j < c - 1) res += ", ";
                        }
                        if (i < r - 1) res += "; ";
                    }
                    res += "]";
                    return res;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) {
                    // 目前无法完美反编译闭包的纯代码树，但用户自定义的函数大多不在变量池而在专门的语句中，暂存其表示
                    return "\"<function>\"";
                }
                else if constexpr (std::is_same_v<T, StringMatrix>) {
                    // 不太可能通过 workspace 序列化字符串矩阵，但做个兜底
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
                    const auto& entries = arg.getEntries();
                    std::string res = "dict(";
                    for (size_t ii = 0; ii < entries.size(); ++ii) {
                        res += "\"" + entries[ii].first + "\", ";
                        try {
                            const auto& v = std::any_cast<const jc::Value&>(entries[ii].second);
                            res += v.toJC2Expression();
                        }
                        catch (...) { res += "0"; }
                        if (ii < entries.size() - 1) res += ", ";
                    }
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, List>) {
                    std::string res = "list(";
                    for (size_t ii = 0; ii < arg.size(); ++ii) {
                        try {
                            const auto& v = std::any_cast<const jc::Value&>(arg.raw()[ii]);
                            res += v.toJC2Expression();
                        }
                        catch (...) { res += "0"; }
                        if (ii < arg.size() - 1) res += ", ";
                    }
                    res += ")";
                    return res;
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) {
                    return "\"<class " + arg->name + ">\"";
                }
                else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) {
                    return "\"<" + arg->classDef->name + " instance>\"";
                }
                else if constexpr (std::is_same_v<T, SuperProxyPtr>) {
                    return "\"<super>\"";
                }
                else {
                    return "0";
                }
                }, data);
        }

    };

    inline Value ldivide(const Value& lhs, const Value& rhs) {
        return std::visit([&](auto&& a, [[maybe_unused]] auto&& b) -> Value {
            using T1 = std::decay_t<decltype(a)>;
            // 1. 左边是矩阵 -> A^(-1) * B
            if constexpr (std::is_same_v<T1, RealMatrix> || std::is_same_v<T1, ComplexMatrix>) {
                return Value(a.inverse()) * rhs;
            }
            // 2. 其他所有情况 -> B / A（白嫖 operator/ 的全部跨维度降级逻辑）
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
        std::any nativeFn;       // ★ 改：std::any 代替 std::function<Value(...)>
        std::vector<Value> defaultValues;   // ★ defaultValues[i].isNone() = 该参数必填
        // ★ 参数数量辅助
        int minArgs() const {
            int count = static_cast<int>(paramNames.size());
            // 从末尾向前数连续的有默认值的参数
            for (int i = static_cast<int>(paramNames.size()) - 1; i >= 0; --i) {
                if (i < static_cast<int>(defaultValues.size()) && !defaultValues[i].isNone())
                    count--;
                else
                    break;
            }
            return count;
        }
        int maxArgs() const { return static_cast<int>(paramNames.size()); }
        bool acceptsArgCount(int n) const { return n >= minArgs() && n <= maxArgs(); }
        bool hasRef() const {
            for (bool b : isRef) if (b) return true;
            return false;
        }
        bool hasCaptures() const { return capturedEnv.has_value(); }
        bool isNative() const { return nativeFn.has_value(); }

        FunctionClosure(std::vector<std::string> paramNames, std::vector<bool> isRef,
            std::string rawBody, std::shared_ptr<Expr> body)
            : paramNames(std::move(paramNames)), isRef(std::move(isRef)),
            rawBody(std::move(rawBody)), body(std::move(body)) {
        }

        FunctionClosure(std::vector<std::string> paramNames, std::vector<bool> isRef,
            std::string rawBody, std::shared_ptr<Expr> body,
            std::any capturedEnv)
            : paramNames(std::move(paramNames)), isRef(std::move(isRef)),
            rawBody(std::move(rawBody)), body(std::move(body)),
            capturedEnv(std::move(capturedEnv)) {
        }

        std::string toString() const {
            if (isNative()) return "<function " + rawBody + "()>";
            std::string params;
            for (size_t i = 0; i < paramNames.size(); ++i) {
                if (isRef[i]) params += "ref ";
                params += paramNames[i];
                if (i < paramNames.size() - 1) params += ", ";
            }
            return "<function(" + params + ")>";
        }
    };

    struct ErrorSignal { std::string message; };

} // namespace jc

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // JC2_VALUE_H
