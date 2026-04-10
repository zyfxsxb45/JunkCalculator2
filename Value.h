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
#include "Complex.h"
#include "Matrix.h"
#include "BigInt.h"
#include "Expr.h"
#include "Fraction.h"
#include "Base.h"
#include "Tolerance.h"
#include "Image.h"
#include "Probability.h"

namespace jc {

    struct FunctionClosure;

    struct PrintGuard {
        std::vector<const void*>& vis;
        bool isCycle;
        PrintGuard(std::vector<const void*>& v, const void* p) : vis(v) {
            isCycle = (std::find(vis.begin(), vis.end(), p) != vis.end());
            if (!isCycle) vis.push_back(p);
        }
        ~PrintGuard() {
            if (!isCycle) vis.pop_back(); // 打印退出时自动抹除足迹
        }
    };

    // =======================================================
    // 高级引用语义 Dict (底层交由智能指针接管防深拷贝)
    // =======================================================
    class Dict {
    private:
        std::shared_ptr<std::unordered_map<std::string, std::any>> ptr;
    public:
        Dict() : ptr(std::make_shared<std::unordered_map<std::string, std::any>>()) {}

        void set(const std::string& key, const std::any& val) {
            (*ptr)[key] = val;
        }

        std::any* get(const std::string& key) {
            auto it = ptr->find(key);
            if (it != ptr->end()) return &it->second;
            return nullptr;
        }

        const std::any* get(const std::string& key) const {
            auto it = ptr->find(key);
            if (it != ptr->end()) return &it->second;
            return nullptr;
        }

        bool has(const std::string& key) const {
            return ptr->find(key) != ptr->end();
        }

        bool remove(const std::string& key) {
            return ptr->erase(key) > 0;
        }

        size_t size() const { return ptr->size(); }
        bool empty() const { return ptr->empty(); }

        std::vector<std::string> getKeys() const {
            std::vector<std::string> keys;
            keys.reserve(ptr->size());
            for (const auto& kv : *ptr) keys.push_back(kv.first);
            return keys;
        }

        std::vector<std::pair<std::string, std::any>> getEntries() const {
            std::vector<std::pair<std::string, std::any>> res;
            res.reserve(ptr->size());
            for (const auto& kv : *ptr) res.push_back(kv);
            return res;
        }

        const void* id() const { return ptr.get(); }
    };

    // =======================================================
    // 高级引用语义 List (底层交由智能指针接管防深拷贝)
    // =======================================================
    class List {
    private:
        std::shared_ptr<std::vector<std::any>> ptr;
    public:
        List() : ptr(std::make_shared<std::vector<std::any>>()) {}

        void push_back(const std::any& val) { ptr->push_back(val); }

        std::any& at(int idx) {
            int n = static_cast<int>(ptr->size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index out of bounds.");
            return (*ptr)[idx];
        }

        const std::any& at(int idx) const {
            int n = static_cast<int>(ptr->size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index out of bounds.");
            return (*ptr)[idx];
        }

        void set(int idx, const std::any& val) {
            int n = static_cast<int>(ptr->size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Index out of bounds.");
            (*ptr)[idx] = val;
        }

        void insert(int idx, const std::any& val) {
            int n = static_cast<int>(ptr->size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx > n)
                throw std::out_of_range("List Error: Insert index out of bounds.");
            ptr->insert(ptr->begin() + idx, val);
        }

        void removeAt(int idx) {
            int n = static_cast<int>(ptr->size());
            if (idx < 0) idx = n + idx;
            if (idx < 0 || idx >= n)
                throw std::out_of_range("List Error: Remove index out of bounds.");
            ptr->erase(ptr->begin() + idx);
        }

        size_t size() const { return ptr->size(); }
        bool empty() const { return ptr->empty(); }
        const std::vector<std::any>& raw() const { return *ptr; }
        std::vector<std::any>& raw() { return *ptr; }
        const void* id() const { return ptr.get(); }
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
        Complex, RealMatrix, ComplexMatrix, StringMatrix, Dict, List,
        std::shared_ptr<FunctionClosure>,
        std::shared_ptr<ClassDefinition>,
        std::shared_ptr<Instance>,
        SuperProxyPtr
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
        Value(std::shared_ptr<ClassDefinition> val) : data(std::move(val)) {}
        Value(std::shared_ptr<Instance> val) : data(std::move(val)) {}

        bool isInstance() const { return std::holds_alternative<std::shared_ptr<Instance>>(data); }
        bool isClass() const { return std::holds_alternative<std::shared_ptr<ClassDefinition>>(data); }
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
                return Value(negResult ? -magnitude : magnitude);
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
                    return std::pow(a, b);
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
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, BigInt>) {
                    if (b.isNegative()) {
                        Fraction res = Fraction(BigInt(1), a).pow(static_cast<int64_t>(b.abs().toDouble()));
                        return Value::fromFraction(res);
                    }
                    return Value(a.pow(b));
                }
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, BigInt>) {
                    Fraction res = a.pow(static_cast<int64_t>(b.toDouble()));
                    return Value::fromFraction(res);
                }
                else if constexpr (std::is_same_v<T1, BigInt> && std::is_same_v<T2, Fraction>) {
                    int64_t p = static_cast<int64_t>(b.getNum().toDouble());
                    int64_t q = static_cast<int64_t>(b.getDen().toDouble());

                    if (a.isNegative()) {
                        return Value::negativePow(a.toDouble(), p, q);
                    }
                    if (a.isZero() && p <= 0) throw std::runtime_error("Math Error: Base 0 requires positive exponent.");
                    return Value(std::pow(a.toDouble(), static_cast<double>(p) / static_cast<double>(q)));
                }
                else if constexpr (std::is_same_v<T1, Fraction> && std::is_same_v<T2, Fraction>) {
                    double base = a.toDouble();
                    int64_t p = static_cast<int64_t>(b.getNum().toDouble());
                    int64_t q = static_cast<int64_t>(b.getDen().toDouble());

                    if (base < 0) {
                        return Value::negativePow(base, p, q);
                    }
                    return Value(std::pow(base, static_cast<double>(p) / static_cast<double>(q)));
                }
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
                    if (jc::Tol::isEq(b, 0.0)) throw std::runtime_error("Math Error: Modulo by zero.");
                    RealMatrix res(a.getRows(), a.getCols());
                    for (int i = 0; i < a.getRows(); ++i) {
                        for (int j = 0; j < a.getCols(); ++j) {
                            res(i, j) = std::fmod(a(i, j), b);
                        }
                    }
                    return Value(res);
                }
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
            std::visit([&os](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, std::monostate>) {}
                else if constexpr (std::is_same_v<T, double>) {
                    double v = arg;
                    double rounded = std::round(v);
                    if (!Tol::isEq(rounded, 0.0) && !Tol::isEq(v, 0.0) && Tol::isEq(v, rounded, 1e5) && std::abs(rounded) < 1e15) {
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
                else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) os << arg->toString();
                else if constexpr (std::is_same_v<T, Fraction>) os << arg;
                else if constexpr (std::is_same_v<T, BaseNum>) os << arg;
                else if constexpr (std::is_same_v<T, jc::StringMatrix>) os << arg;
                else if constexpr (std::is_same_v<T, jc::Dict>) {
                    PrintGuard guard(visited, arg.id());
                    if (guard.isCycle) { os << "{...}"; return; } // 防爆护盾！
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
                    PrintGuard guard(visited, arg.id());
                    if (guard.isCycle) { os << "[...]"; return; } // 防爆护盾！
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

        std::string toJC2Expression() const {
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
                    return "\"<function>\"";
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
                else { return "none()"; }
                }, data);
        }

    };

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

        int minArgs() const {
            int count = static_cast<int>(paramNames.size());
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

    struct ErrorSignal { std::string message; };

} // namespace jc

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // JC2_VALUE_H
