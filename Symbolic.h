// Symbolic.h
#ifndef JC2_SYMBOLIC_H
#define JC2_SYMBOLIC_H

#include "BigInt.h"
#include "Fraction.h"
#include "Complex.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <variant>
#include <functional>
#include <type_traits>
#include <tuple>

namespace jc {
    class Value;

    // ==========================================
    // 内部高精度数值载体 (隔离 VM)
    // ==========================================
    using CASVal = std::variant<double, BigInt, Fraction>;

    bool isCasZero(const CASVal& v);
    bool isCasOne(const CASVal& v);
    std::string casToString(const CASVal& v);

    // ==========================================
    // AST 节点定义
    // ==========================================
    enum class SymType { NUM, VAR, ADD, MUL, POW, FUNC };

    class SymNode {
    public:
        virtual ~SymNode() = default;
        virtual SymType getType() const = 0;
        virtual std::string toString() const = 0;

        virtual bool isZero() const { return false; }
        virtual bool isOne() const { return false; }
    };

    // ==========================================
    // 符号表达式代理类 (The Value Proxy)
    // ==========================================
    class SymExpr {
    public:
        std::shared_ptr<SymNode> ptr;

        SymExpr();
        explicit SymExpr(std::shared_ptr<SymNode> p) : ptr(std::move(p)) {}

        // 隐式升维构造
        SymExpr(double v);
        template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
        SymExpr(T v);
        SymExpr(const BigInt& v);
        SymExpr(const Fraction& v);
        SymExpr(const Complex& v);
        SymExpr(const CASVal& v);

        static SymExpr makeVar(const std::string& name);

        std::string toString() const;
        bool isZero() const;
        bool isOne() const;

        // ★ 修改：改成全局友元函数！这是 C++ 支持 1 + x 隐式转换的核心魔法
        friend SymExpr operator+(const SymExpr& a, const SymExpr& b);
        friend SymExpr operator-(const SymExpr& a, const SymExpr& b);
        friend SymExpr operator*(const SymExpr& a, const SymExpr& b);
        friend SymExpr operator/(const SymExpr& a, const SymExpr& b);
        friend SymExpr operator^(const SymExpr& a, const SymExpr& b);

        // 单目负号可以保留为成员
        SymExpr operator-() const;

        bool operator==(const SymExpr& other) const {
            return toString() == other.toString();
        }
        bool operator!=(const SymExpr& other) const {
            return !(*this == other);
        }
    };

    // ★ 必须在类外提供全局声明，否则某些编译器无法在普通查找中找到这些运算符
    SymExpr operator+(const SymExpr& a, const SymExpr& b);
    SymExpr operator-(const SymExpr& a, const SymExpr& b);
    SymExpr operator*(const SymExpr& a, const SymExpr& b);
    SymExpr operator/(const SymExpr& a, const SymExpr& b);
    SymExpr operator^(const SymExpr& a, const SymExpr& b);

    // ==========================================
    // CAS 操作函数
    // ==========================================
    SymExpr expand(const SymExpr& expr, int64_t maxPowTerms = static_cast<int64_t>(1e5));
    SymExpr subs(const SymExpr& expr, const std::string& var, const SymExpr& val);
    SymExpr evalFloat(const SymExpr& expr);   // 已有：全部转 double
    SymExpr evalValue(const SymExpr& expr);   // 新增：保留 Complex 等精确类型
    SymExpr diff(const SymExpr& expr, const std::string& var);

    int getAstNodeCount(const SymExpr& expr);
    SymExpr contract(const SymExpr& expr);
    SymExpr simplify(const SymExpr& expr);

    // 因式分解
    SymExpr factor(const SymExpr& expr, int depth = 0);
    // 实数域完全因式分解
    SymExpr factorReal(const SymExpr& expr);

    // 获取多项式最高次幂
    int getDegree(const SymExpr& expr, const std::string& var);
    // 多项式带余除法：返回 {商 (Quotient), 余数 (Remainder)}
    std::pair<SymExpr, SymExpr> polyDiv(const SymExpr& dividend, const SymExpr& divisor, const std::string& var);
    // 多项式最大公约数 (基于欧几里得算法)
    SymExpr polyGCD(const SymExpr& a, const SymExpr& b, const std::string& var);
    // 多项式扩展欧几里得算法 (EEA)
    std::tuple<SymExpr, SymExpr, SymExpr> polyEGCD(const SymExpr& a, const SymExpr& b, const std::string& var);
    // 多项式无平方分解 (Square-Free Factorization)
    std::vector<std::pair<SymExpr, int>> polySquareFree(const SymExpr& p, const std::string& var);
    // 多项式结式 (Sylvester Resultant)
    SymExpr polyResultant(const SymExpr& a, const SymExpr& b, const std::string& var);

    // 代数重写引擎 (Pattern Matching Engine)
    SymExpr applyRule(const SymExpr& expr, const SymExpr& pattern, const SymExpr& target);

    // 符号方程求解
    std::vector<SymExpr> solveEq(const SymExpr& expr, const std::string& var);

    // ==========================================
    // 🚀 下一步发展路线图 (Roadmap) 接口预留
    // ==========================================
    SymExpr limit(const SymExpr& expr, const std::string& var, const SymExpr& val);
    SymExpr trigsimp(const SymExpr& expr);
    SymExpr taylor(const SymExpr& expr, const std::string& var, const SymExpr& a, int order); // 泰勒展开

    // 内部工具暴露给 Integration 模块
    std::pair<bool, int64_t> extractExactInt(const CASVal& cval);
    bool containsVar(const std::shared_ptr<SymNode>& node, const std::string& var);
    bool matchAST(const std::shared_ptr<SymNode>& node, const std::shared_ptr<SymNode>& pat, std::map<std::string, SymExpr>& captures);
    SymExpr substituteCaptures(const SymExpr& target, const std::map<std::string, SymExpr>& captures);
    SymExpr simplifyCore(const SymExpr& expr);
    std::vector<SymExpr> extractCoeffs(const SymExpr& expr, const std::string& var);

    // ==========================================
    // 派生数学节点
    // ==========================================

    class SymNum : public SymNode {
    public:
        CASVal value;
        explicit SymNum(CASVal v) : value(std::move(v)) {}
        SymType getType() const override { return SymType::NUM; }
        std::string toString() const override { return casToString(value); }
        bool isZero() const override { return isCasZero(value); }
        bool isOne() const override { return isCasOne(value); }
    };

    class SymVar : public SymNode {
    public:
        std::string name;
        explicit SymVar(std::string n) : name(std::move(n)) {}
        SymType getType() const override { return SymType::VAR; }
        std::string toString() const override { return name; }
    };

    class SymAdd : public SymNode {
    public:
        std::vector<std::shared_ptr<SymNode>> args;
        explicit SymAdd(std::vector<std::shared_ptr<SymNode>> a) : args(std::move(a)) {}
        SymType getType() const override { return SymType::ADD; }
        std::string toString() const override;
    };

    class SymMul : public SymNode {
    public:
        std::vector<std::shared_ptr<SymNode>> args;
        explicit SymMul(std::vector<std::shared_ptr<SymNode>> a) : args(std::move(a)) {}
        SymType getType() const override { return SymType::MUL; }
        std::string toString() const override;
    };

    class SymPow : public SymNode {
    public:
        std::shared_ptr<SymNode> base;
        std::shared_ptr<SymNode> exp;
        SymPow(std::shared_ptr<SymNode> b, std::shared_ptr<SymNode> e) : base(std::move(b)), exp(std::move(e)) {}
        SymType getType() const override { return SymType::POW; }
        std::string toString() const override;
    };

    class SymFunc : public SymNode {
    public:
        std::string name;
        std::vector<std::shared_ptr<SymNode>> args;
        SymFunc(std::string n, std::vector<std::shared_ptr<SymNode>> a)
            : name(std::move(n)), args(std::move(a)) {
        }
        SymType getType() const override { return SymType::FUNC; }
        std::string toString() const override;
    };

    // 模板构造函数实现 (必须在 SymNum 完整定义之后)
    template<typename T, std::enable_if_t<std::is_integral_v<T>, int>>
    SymExpr::SymExpr(T v) : ptr(std::make_shared<SymNum>(BigInt(static_cast<int64_t>(v)))) {}

} // namespace jc

#endif // JC2_SYMBOLIC_H
