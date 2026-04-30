// Symbolic.cpp
#include "Symbolic.h"
#include "Factorization.h"
#include "Value.h"          // ★ 新增：统一走 Value 运算
#include "SymEval.h"
#include "SymRules.h"
#include <sstream>
#include <cmath>
#include <algorithm>
#include <functional>
#include <set>
#include <numeric>
#include <map>

#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <optional>

namespace jc {

    // ==========================================
    // 全局表达式内存池 (DAG Interning Pool)
    // ==========================================
    static std::unordered_map<std::string, std::weak_ptr<SymNode>> g_symPool;

    void SymExpr::cleanupPool() {
        for (auto it = g_symPool.begin(); it != g_symPool.end(); ) {
            if (it->second.expired()) {
                it = g_symPool.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::shared_ptr<SymNode> SymExpr::intern(const std::shared_ptr<SymNode>& node) {
        if (!node) return nullptr;
        
        // 启发式自动清理：每驻留 10000 个新节点，清理一次失效的弱引用
        static int pruneCounter = 0;
        if (++pruneCounter > 10000) {
            pruneCounter = 0;
            cleanupPool();
        }

        std::string key = node->getSignature();
        auto& weakRef = g_symPool[key];
        if (auto shared = weakRef.lock()) {
            return shared;
        }
        weakRef = node;
        return node;
    }

    // ==========================================
    // CASVal ↔ Value 桥接（全局单一权威）
    // ==========================================
    static Value casValToValue(const CASVal& v) {
        return std::visit([](auto&& arg) -> Value { return Value(arg); }, v);
    }

    static CASVal valueToCasVal(const Value& v) {
        if (std::holds_alternative<BigInt>(v.data))    return std::get<BigInt>(v.data);
        if (std::holds_alternative<Fraction>(v.data))  return std::get<Fraction>(v.data);
        if (std::holds_alternative<double>(v.data))    return std::get<double>(v.data);
        throw std::runtime_error("CAS Error: Cannot convert value to CAS type.");
    }

    // ==========================================
    // 谓词与工具（全部委托 Value）
    // ==========================================
    bool isCasZero(const CASVal& v) {
        return !casValToValue(v).truthy();
    }

    bool isCasOne(const CASVal& v) {
        try { return casValToValue(v).asDouble() == 1.0; }
        catch (...) { return false; }
    }

    // ==========================================
// 底层探测器：精准识别 CASVal 符号
// ==========================================
    bool isCasNegative(const CASVal& v) {
        return std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BigInt>) return arg.isNegative();
            else if constexpr (std::is_same_v<T, Fraction>) return arg.getNum().isNegative() != arg.getDen().isNegative();
            else if constexpr (std::is_same_v<T, double>) return arg < 0.0;
            else return false;
            }, v);
    }

    std::string casToString(const CASVal& v) {
        Value val = casValToValue(v);
        std::ostringstream oss;
        oss << val;
        return oss.str();
    }

    CASVal casAdd(const CASVal& a, const CASVal& b) {
        return valueToCasVal(casValToValue(a) + casValToValue(b));
    }

    CASVal casMul(const CASVal& a, const CASVal& b) {
        return valueToCasVal(casValToValue(a) * casValToValue(b));
    }

    // 从 CASVal 提取整数（委托 Value::asDouble）
    static std::pair<bool, int> casToInt(const CASVal& v) {
        try {
            double d = casValToValue(v).asDouble();
            if (d == std::floor(d) && std::abs(d) <= 1000)
                return { true, static_cast<int>(d) };
        }
        catch (...) {}
        return { false, 0 };
    }

    // ==========================================
    // toString 实现
    // ==========================================

    // ==========================================
    // 加法节点排版：原教旨安全版 
    // a + (-b) 自动识别首个字符转为 a - b
    // ==========================================
    std::string SymAdd::computeString() const {
        if (args.empty()) return "0";
        std::string res = "";

        for (size_t i = 0; i < args.size(); ++i) {
            std::string termStr = args[i]->toString();

            if (i > 0) {
                // 只要该项字符串以 '-' 开头，说明它是个纯正的负项 (如 "-3 * x" 或是 "-x")
                if (!termStr.empty() && termStr[0] == '-') {
                    res += " - ";
                    res += termStr.substr(1); // 仅仅切掉开头的负号，绝对安全！
                }
                else {
                    res += " + ";
                    res += termStr;
                }
            }
            else {
                res += termStr; // 第一项原样输出 (-x 还是 -x)
            }
        }
        return res;
    }

    // ==========================================
    // 乘法节点排版：原教旨安全版 
    // 自动拦截开头的负系数，且绝不误伤内部括号
    // ==========================================
    std::string SymMul::computeString() const {
        if (args.empty()) return "1";
        std::string res;

        size_t startIdx = 0;

        // 探查乘积的第一项是不是确凿的常数，且以 '-' 开头
        if (args[0]->getType() == SymType::NUM) {
            std::string firstStr = args[0]->toString();
            if (!firstStr.empty() && firstStr[0] == '-') {
                startIdx = 1; // 跨过第一项独立排版

                if (args.size() == 1) return firstStr; // 单独的 "-3"

                if (firstStr == "-1") {
                    res += "-"; // -1 * x 简化为 -x
                }
                else {
                    res += "-" + firstStr.substr(1) + " * "; // -3 * x 简化为 -3 * x
                }
            }
        }

        // 把后面的乘积拼接上去
        for (size_t i = startIdx; i < args.size(); ++i) {
            if (i > startIdx) res += " * ";

            std::string termStr = args[i]->toString();

            // 只有加法节点 (a+b) 在乘法中需要套括号保护！！其他由于 CAS 分配律必然无需多余括号！
            if (args[i]->getType() == SymType::ADD) {
                res += "(" + termStr + ")";
            }
            else {
                res += termStr;
            }
        }
        return res;
    }

    std::string SymPow::computeString() const {
        // ==========================================
        // 排版拦截：检测是否为纯分数指数 1/n
        // ==========================================
        if (exp->getType() == SymType::NUM) {
            auto expNum = std::static_pointer_cast<SymNum>(exp);
            if (std::holds_alternative<Fraction>(expNum->value)) {
                Fraction f = std::get<Fraction>(expNum->value);
                // 仅当分子为 1，且分母大于 1 时触发根式简写
                if (f.getNum() == BigInt(1) && f.getDen() > BigInt(1)) {
                    int64_t n = 0;
                    try {
                        n = static_cast<int64_t>(f.getDen().toDouble());
                    }
                    catch (...) {}

                    if (n == 2) {
                        return "sqrt(" + base->toString() + ")";
                    }
                    else if (n == 3) {
                        return "cbrt(" + base->toString() + ")";
                    }
                    else if (n > 3) {
                        return "root(" + base->toString() + ", " + std::to_string(n) + ")";
                    }
                }
            }
        }

        // ==========================================
        // 常规幂次排版 (带安全括号的保护逻辑)
        // ==========================================
        std::string bStr = base->toString();
        bool baseParen = false;

        if (base->getType() == SymType::ADD || base->getType() == SymType::MUL) {
            baseParen = true;
        }
        else if (base->getType() == SymType::NUM) {
            if (bStr.find('/') != std::string::npos || (!bStr.empty() && bStr[0] == '-')) {
                baseParen = true;
            }
        }

        if (baseParen && !(bStr.front() == '(' && bStr.back() == ')')) {
            bStr = "(" + bStr + ")";
        }

        std::string eStr = exp->toString();
        bool expParen = false;

        if (exp->getType() == SymType::ADD ||
            exp->getType() == SymType::MUL ||
            exp->getType() == SymType::POW) {
            expParen = true;
        }
        else if (exp->getType() == SymType::NUM) {
            if (eStr.find('/') != std::string::npos || (!eStr.empty() && eStr[0] == '-')) {
                expParen = true;
            }
        }

        if (expParen && !(eStr.front() == '(' && eStr.back() == ')')) {
            eStr = "(" + eStr + ")";
        }

        return bStr + "^" + eStr;
    }

    std::string SymFunc::computeString() const {
        std::string res = name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) res += ", ";
            res += args[i]->toString();
        }
        return res + ")";
    }

    std::string SymAdd::computeSignature() const {
        std::vector<uintptr_t> ptrs;
        ptrs.reserve(args.size());
        for (auto& arg : args) ptrs.push_back(reinterpret_cast<uintptr_t>(arg.get()));
        std::sort(ptrs.begin(), ptrs.end());
        std::string res = "A:[";
        for (size_t i = 0; i < ptrs.size(); ++i) {
            if (i > 0) res += ",";
            res += std::to_string(ptrs[i]);
        }
        return res + "]";
    }

    std::string SymMul::computeSignature() const {
        std::vector<uintptr_t> ptrs;
        ptrs.reserve(args.size());
        for (auto& arg : args) ptrs.push_back(reinterpret_cast<uintptr_t>(arg.get()));
        std::sort(ptrs.begin(), ptrs.end());
        std::string res = "M:[";
        for (size_t i = 0; i < ptrs.size(); ++i) {
            if (i > 0) res += ",";
            res += std::to_string(ptrs[i]);
        }
        return res + "]";
    }

    std::string SymPow::computeSignature() const {
        return "P:(" + std::to_string(reinterpret_cast<uintptr_t>(base.get())) + "," + std::to_string(reinterpret_cast<uintptr_t>(exp.get())) + ")";
    }

    std::string SymFunc::computeSignature() const {
        std::string res = "F:" + name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) res += ",";
            res += std::to_string(reinterpret_cast<uintptr_t>(args[i].get()));
        }
        return res + ")";
    }

    // =================================================================
// 整数类型的终极脱壳器：
// 无论它是 BigInt, Fraction(分母为1), double(形如2.0), Complex(形如2+0i)
// 只要它数学上是个精确整数，统统榨出其 int64_t 的灵魂！
// =================================================================
    std::pair<bool, int64_t> extractExactInt(const CASVal& cval) {
        return std::visit([](auto&& arg) -> std::pair<bool, int64_t> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, BigInt>) {
                try {
                    return { true, arg.toInt64() };
                } catch (...) {
                    return { false, 0 };
                }
            }
            else if constexpr (std::is_same_v<T, Fraction>) {
                if (arg.getDen() == BigInt(1)) {
                    try {
                        return { true, arg.getNum().toInt64() };
                    } catch (...) {
                        return { false, 0 };
                    }
                }
            }
            else if constexpr (std::is_same_v<T, double>) {
                if (std::isfinite(arg) && arg == std::floor(arg)) {
                    // C++ 浮点转整防御，最多精准到 53 位 (±9e15)
                    if (std::abs(arg) < 9e15) {
                        return { true, static_cast<int64_t>(arg) };
                    }
                }
            }

            return { false, 0 };
            }, cval);
    }

    // ==========================================
    // 表达式排序比较器 (Higher power first, smaller lexicographical first)
    // ==========================================
    static int compareSymNodes(const std::shared_ptr<SymNode>& a, const std::shared_ptr<SymNode>& b) {
        auto getCore = [](const std::shared_ptr<SymNode>& n) -> std::tuple<std::string, double, double> {
            std::shared_ptr<SymNode> core = n;
            if (n->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(n);
                std::vector<std::shared_ptr<SymNode>> vars;
                for (auto& arg : mul->args) {
                    if (arg->getType() != SymType::NUM) vars.push_back(arg);
                }
                if (vars.empty()) return {"", 0.0, 0.0}; // 纯常数
                if (vars.size() == 1) core = vars[0];
                else {
                    std::string s;
                    double totalExp = 0.0;
                    for (auto& v : vars) {
                        s += v->toString() + "*";
                        if (v->getType() == SymType::POW) {
                            auto p = std::static_pointer_cast<SymPow>(v);
                            if (p->exp->getType() == SymType::NUM) {
                                try { totalExp += casValToValue(std::static_pointer_cast<SymNum>(p->exp)->value).asDouble(); } catch(...) { totalExp += 1.0; }
                            } else totalExp += 1.0;
                        } else {
                            totalExp += 1.0;
                        }
                    }
                    return {s, 1.0, totalExp};
                }
            } else if (n->getType() == SymType::NUM) {
                return {"", 0.0, 0.0};
            }

            if (core->getType() == SymType::POW) {
                auto p = std::static_pointer_cast<SymPow>(core);
                if (p->exp->getType() == SymType::NUM) {
                    try {
                        double e = casValToValue(std::static_pointer_cast<SymNum>(p->exp)->value).asDouble();
                        return {p->base->toString(), e, e};
                    } catch(...) {}
                }
            }
            return {core->toString(), 1.0, 1.0};
        };

        auto [baseA, expA, totA] = getCore(a);
        auto [baseB, expB, totB] = getCore(b);

        // 纯常数排在最后
        if (baseA.empty() && !baseB.empty()) return 1;
        if (!baseA.empty() && baseB.empty()) return -1;
        if (baseA.empty() && baseB.empty()) return 0;

        // 总指数高的排在前面 (降幂排列)
        if (totA != totB) {
            return totA > totB ? -1 : 1;
        }

        // 字典序比较
        if (baseA != baseB) {
            return baseA < baseB ? -1 : 1;
        }

        // 单变量同底数，指数高的排在前面
        if (expA != expB) {
            return expA > expB ? -1 : 1;
        }

        return 0;
    }

    // ==========================================
    // SymExpr 构造
    // ==========================================
    SymExpr::SymExpr() : ptr(intern(std::make_shared<SymNum>(BigInt(0)))) {}
    SymExpr::SymExpr(double v) : ptr(intern(std::make_shared<SymNum>(v))) {}
    SymExpr::SymExpr(const BigInt& v) : ptr(intern(std::make_shared<SymNum>(v))) {}
    SymExpr::SymExpr(const Fraction& v) : ptr(intern(std::make_shared<SymNum>(v))) {}
    SymExpr::SymExpr(const Complex& v) {
        if (Tol::isEq(v.imag, 0.0)) {
            ptr = intern(std::make_shared<SymNum>(v.real));
        } else if (Tol::isEq(v.real, 0.0)) {
            SymExpr imagPart(v.imag);
            SymExpr iVar = SymExpr::makeVar("i");
            ptr = intern((imagPart * iVar).ptr);
        } else {
            SymExpr realPart(v.real);
            SymExpr imagPart(v.imag);
            SymExpr iVar = SymExpr::makeVar("i");
            ptr = intern((realPart + imagPart * iVar).ptr);
        }
    }
    SymExpr::SymExpr(const CASVal& v) : ptr(intern(std::make_shared<SymNum>(v))) {}

    SymExpr SymExpr::makeVar(const std::string& name) {
        return SymExpr(std::make_shared<SymVar>(name));
    }
    std::string SymExpr::toString() const { return ptr ? ptr->toString() : "null"; }
    bool SymExpr::isZero() const { return ptr && ptr->isZero(); }
    bool SymExpr::isOne() const { return ptr && ptr->isOne(); }

    SymExpr operator+(const SymExpr& a, const SymExpr& b) {
        if (!a.ptr) return b;
        if (!b.ptr) return a;
        if (a.isZero()) return b;
        if (b.isZero()) return a;
        std::vector<std::shared_ptr<SymNode>> flatArgs;
        std::function<void(const std::shared_ptr<SymNode>&)> flattenAdd =
            [&](const std::shared_ptr<SymNode>& node) {
            if (node->getType() == SymType::ADD) {
                for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args)
                    flattenAdd(arg);
            }
            else {
                flatArgs.push_back(node);
            }
            };
        flattenAdd(a.ptr);
        flattenAdd(b.ptr);
        CASVal sumConst = BigInt(0);
        struct TermData { CASVal coeff; std::shared_ptr<SymNode> baseNode; };
        std::map<std::string, TermData> symTerms;
        for (auto& node : flatArgs) {
            if (node->getType() == SymType::NUM) {
                sumConst = casAdd(sumConst, std::static_pointer_cast<SymNum>(node)->value);
            }
            else if (node->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(node);
                CASVal coeff = BigInt(1);
                std::vector<std::shared_ptr<SymNode>> symParts;
                for (auto& m_arg : mul->args) {
                    if (m_arg->getType() == SymType::NUM)
                        coeff = casMul(coeff, std::static_pointer_cast<SymNum>(m_arg)->value);
                    else
                        symParts.push_back(m_arg);
                }
                if (symParts.empty()) {
                    sumConst = casAdd(sumConst, coeff);
                }
                else {
                    SymExpr rem = (symParts.size() == 1)
                        ? SymExpr(symParts[0])
                        : SymExpr(std::make_shared<SymMul>(symParts));
                    std::string key = rem.ptr->getSignature();
                    if (symTerms.count(key))
                        symTerms[key].coeff = casAdd(symTerms[key].coeff, coeff);
                    else
                        symTerms[key] = { coeff, rem.ptr };
                }
            }
            else {
                std::string key = node->getSignature();
                if (symTerms.count(key))
                    symTerms[key].coeff = casAdd(symTerms[key].coeff, BigInt(1));
                else
                    symTerms[key] = { BigInt(1), node };
            }
        }
        // ★ 纯净输出：不做任何负号提取，直接组装 ADD 节点
        std::vector<std::pair<std::string, TermData>> sortedTerms(symTerms.begin(), symTerms.end());
        std::sort(sortedTerms.begin(), sortedTerms.end(), [](const auto& lhs, const auto& rhs) {
            return compareSymNodes(lhs.second.baseNode, rhs.second.baseNode) < 0;
        });

        std::vector<std::shared_ptr<SymNode>> newArgs;
        for (auto& [key, data] : sortedTerms) {
            if (isCasZero(data.coeff)) continue;
            if (isCasOne(data.coeff)) {
                newArgs.push_back(data.baseNode);
            }
            else {
                std::vector<std::shared_ptr<SymNode>> mArgs;
                mArgs.push_back(SymExpr(data.coeff).ptr);
                if (data.baseNode->getType() == SymType::MUL) {
                    auto inner = std::static_pointer_cast<SymMul>(data.baseNode);
                    mArgs.insert(mArgs.end(), inner->args.begin(), inner->args.end());
                }
                else {
                    mArgs.push_back(data.baseNode);
                }
                newArgs.push_back(SymExpr(std::make_shared<SymMul>(mArgs)).ptr);
            }
        }
        if (!isCasZero(sumConst))
            newArgs.push_back(SymExpr(sumConst).ptr);

        if (newArgs.empty()) return SymExpr(BigInt(0));
        if (newArgs.size() == 1) return SymExpr(newArgs[0]);
        return SymExpr(std::make_shared<SymAdd>(std::move(newArgs)));
    }
    // ==========================================
    // operator*（乘法与同底数指数合并：带 ADD 基底正规化）
    // ==========================================
    SymExpr operator*(const SymExpr& a, const SymExpr& b) {
        if (!a.ptr || !b.ptr) return SymExpr(BigInt(0));
        if (a.isZero() || b.isZero()) return SymExpr(0);
        if (a.isOne()) return b;
        if (b.isOne()) return a;
        std::vector<std::shared_ptr<SymNode>> flatArgs;
        std::function<void(const std::shared_ptr<SymNode>&)> flattenMul =
            [&](const std::shared_ptr<SymNode>& node) {
            if (node->getType() == SymType::MUL) {
                for (auto& arg : std::static_pointer_cast<SymMul>(node)->args)
                    flattenMul(arg);
            }
            else {
                flatArgs.push_back(node);
            }
            };
        flattenMul(a.ptr);
        flattenMul(b.ptr);
        CASVal prodConst = BigInt(1);
        struct FactorData { CASVal exp; std::shared_ptr<SymNode> baseNode; };
        std::map<std::string, FactorData> symFactors;
        // ★ 核心架构：ADD 基底首项负号正规化
        // 检测一个 ADD 节点的字典序最后一项（通常是最高次项）是否带负系数
        auto addLeadingNegative = [](const std::shared_ptr<SymNode>& node) -> bool {
            if (node->getType() != SymType::ADD) return false;
            auto add = std::static_pointer_cast<SymAdd>(node);
            if (add->args.empty()) return false;
            auto lastArg = add->args.back();
            if (lastArg->getType() == SymType::NUM) return false;
            if (lastArg->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(lastArg);
                if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                    return isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                }
            }
            return false; // VAR, POW 等隐含系数 +1
            };
        // 对 ADD 节点取反：(-y + z) → (y - z)
        auto negateAdd = [](const std::shared_ptr<SymNode>& node) -> std::shared_ptr<SymNode> {
            auto add = std::static_pointer_cast<SymAdd>(node);
            SymExpr result(BigInt(0));
            for (auto& arg : add->args) {
                result = result + (SymExpr(BigInt(-1)) * SymExpr(arg));
            }
            return result.ptr;
            };
        // 将一个因子（base^exp）登记到 symFactors，同时正规化 ADD 基底
        auto registerFactor = [&](std::shared_ptr<SymNode> base, CASVal expVal) {
            // ★ 如果 base 是 ADD 且首项为负，翻转基底，吸收符号到 prodConst
            if (addLeadingNegative(base)) {
                auto [isInt, n] = extractExactInt(expVal);
                if (isInt) {
                    base = negateAdd(base);
                    // (-1)^n：奇数幂贡献 -1，偶数幂贡献 1
                    if (n % 2 != 0) {
                        prodConst = casMul(prodConst, BigInt(-1));
                    }
                }
            }
            std::string key = base->getSignature();
            if (symFactors.count(key))
                symFactors[key].exp = casAdd(symFactors[key].exp, expVal);
            else
                symFactors[key] = { expVal, base };
            };
        for (auto& node : flatArgs) {
            if (node->getType() == SymType::NUM) {
                prodConst = casMul(prodConst, std::static_pointer_cast<SymNum>(node)->value);
            }
            else if (node->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(node);
                if (powNode->exp->getType() == SymType::NUM) {
                    CASVal expVal = std::static_pointer_cast<SymNum>(powNode->exp)->value;
                    registerFactor(powNode->base, expVal);
                }
                else {
                    std::string key = node->getSignature();
                    if (symFactors.count(key))
                        symFactors[key].exp = casAdd(symFactors[key].exp, BigInt(1));
                    else
                        symFactors[key] = { BigInt(1), node };
                }
            }
            else {
                // 普通因子视为 base^1
                registerFactor(node, BigInt(1));
            }
        }
        if (isCasZero(prodConst)) return SymExpr(0);

        // ★ 尝试将 prodConst 与 symFactors 中的 NUM base 合并 (例如 2 * 2^(-1/2) -> 2^(1/2))
        if (!isCasOne(prodConst)) {
            for (auto& [key, data] : symFactors) {
                if (data.baseNode->getType() == SymType::NUM) {
                    CASVal bVal = std::static_pointer_cast<SymNum>(data.baseNode)->value;
                    auto [isBInt, bInt] = extractExactInt(bVal);
                    if (isBInt && bInt != 0 && bInt != 1 && bInt != -1) {
                        while (true) {
                            bool extracted = false;
                            if (std::holds_alternative<BigInt>(prodConst)) {
                                BigInt p = std::get<BigInt>(prodConst);
                                if ((p % BigInt(bInt)).isZero()) {
                                    prodConst = p / BigInt(bInt);
                                    data.exp = casAdd(data.exp, BigInt(1));
                                    extracted = true;
                                }
                            } else if (std::holds_alternative<Fraction>(prodConst)) {
                                Fraction p = std::get<Fraction>(prodConst);
                                if ((p.getNum() % BigInt(bInt)).isZero()) {
                                    prodConst = Fraction(p.getNum() / BigInt(bInt), p.getDen());
                                    data.exp = casAdd(data.exp, BigInt(1));
                                    extracted = true;
                                } else if ((p.getDen() % BigInt(bInt)).isZero()) {
                                    prodConst = Fraction(p.getNum(), p.getDen() / BigInt(bInt));
                                    data.exp = casAdd(data.exp, BigInt(-1));
                                    extracted = true;
                                }
                            }
                            if (!extracted) break;
                        }
                    }
                }
            }
        }

        std::vector<std::pair<std::string, FactorData>> sortedFactors(symFactors.begin(), symFactors.end());
        std::sort(sortedFactors.begin(), sortedFactors.end(), [](const auto& lhs, const auto& rhs) {
            return compareSymNodes(lhs.second.baseNode, rhs.second.baseNode) < 0;
        });

        std::vector<std::shared_ptr<SymNode>> newArgs;
        for (auto& [key, data] : sortedFactors) {
            if (isCasZero(data.exp)) continue;
            
            SymExpr term = SymExpr(data.baseNode) ^ SymExpr(data.exp);
            if (term.ptr->getType() == SymType::NUM) {
                prodConst = casMul(prodConst, std::static_pointer_cast<SymNum>(term.ptr)->value);
            } else if (term.ptr->getType() == SymType::MUL) {
                auto innerMul = std::static_pointer_cast<SymMul>(term.ptr);
                for (auto& arg : innerMul->args) {
                    if (arg->getType() == SymType::NUM) {
                        prodConst = casMul(prodConst, std::static_pointer_cast<SymNum>(arg)->value);
                    } else {
                        newArgs.push_back(arg);
                    }
                }
            } else {
                newArgs.push_back(term.ptr);
            }
        }
        if (isCasZero(prodConst)) return SymExpr(BigInt(0));
        if (!isCasOne(prodConst) || newArgs.empty()) {
            newArgs.insert(newArgs.begin(), SymExpr(prodConst).ptr);
        }
        if (newArgs.size() == 1) return SymExpr(newArgs[0]);
        return SymExpr(std::make_shared<SymMul>(std::move(newArgs)));
    }

    // ==========================================
    // 单目与二元减法
    // ==========================================
    SymExpr SymExpr::operator-() const { return (*this) * SymExpr(-1); }
    SymExpr operator-(const SymExpr& a, const SymExpr& b) { return a + (-b); }

    static std::pair<bool, Value> tryEvalConst(const SymExpr& expr);

    // ==========================================
    // operator/（除法 → 委托 Value 求倒数）
    // ==========================================
    SymExpr operator/(const SymExpr& a, const SymExpr& b) {
        if (!a.ptr) return SymExpr(BigInt(0));
        if (!b.ptr || b.isZero()) throw std::runtime_error("CAS Error: Division by zero.");

        auto [bOk, bVal] = tryEvalConst(b);
        if (bOk && !bVal.truthy()) throw std::runtime_error("CAS Error: Division by zero.");

        if (b.ptr->getType() == SymType::NUM) {
            auto numNode = std::static_pointer_cast<SymNum>(b.ptr);
            try {
                Value reciprocal = Value(BigInt(1)) / casValToValue(numNode->value);
                return a * SymExpr(std::make_shared<SymNum>(valueToCasVal(reciprocal)));
            }
            catch (...) {}
        }

        return a * (b ^ SymExpr(-1));
    }

    // ==========================================
    // operator^（乘方 → 委托 Value 常数折叠）
    // ==========================================
    SymExpr operator^(const SymExpr& a, const SymExpr& b) {
        if (!a.ptr) return SymExpr(BigInt(0));
        if (!b.ptr) return SymExpr(BigInt(1));

        auto [aOk, aVal] = tryEvalConst(a);
        auto [bOk, bVal] = tryEvalConst(b);
        bool aIsZero = a.isZero() || (aOk && !aVal.truthy());
        bool bIsZero = b.isZero() || (bOk && !bVal.truthy());

        if (aIsZero) {
            if (bIsZero) throw std::runtime_error("CAS Error: 0^0 is undefined.");
            bool bIsNeg = false;
            if (bOk) {
                try { bIsNeg = bVal.asDouble() < 0.0; } catch(...) {}
            } else if (b.ptr->getType() == SymType::NUM) {
                bIsNeg = isCasNegative(std::static_pointer_cast<SymNum>(b.ptr)->value);
            }
            if (bIsNeg) throw std::runtime_error("CAS Error: Division by zero.");
            return SymExpr(0);
        }
        if (a.isOne() || bIsZero) return SymExpr(BigInt(1));
        if (b.isOne()) return a;

        // 常数折叠: NUM^NUM → 委托 Value（Value 内部会自动决定精确/符号/浮点）
        if (a.ptr->getType() == SymType::NUM && b.ptr->getType() == SymType::NUM) {
            auto baseNum = std::static_pointer_cast<SymNum>(a.ptr);
            auto expNum = std::static_pointer_cast<SymNum>(b.ptr);
            auto [isInt, n] = casToInt(expNum->value);
            if (isInt) {
                try {
                    Value result = casValToValue(baseNum->value) ^ casValToValue(expNum->value);
                    return result.asSymbolic();
                }
                catch (const std::runtime_error& e) {
                    std::string msg = e.what();
                    if (msg.find("Math Error") != std::string::npos) throw;
                    if (msg.find("CAS Error") != std::string::npos) throw;
                }
            } else if (std::holds_alternative<Fraction>(expNum->value)) {
                Fraction expF = std::get<Fraction>(expNum->value);
                BigInt m = expF.getNum();
                BigInt n_den = expF.getDen();
                
                auto processIntBase = [&](BigInt baseInt) -> SymExpr {
                    if (baseInt.isZero()) return SymExpr(BigInt(0));
                    if (baseInt == BigInt(1)) return SymExpr(BigInt(1));
                    
                    bool isNeg = baseInt.isNegative();
                    if (isNeg) baseInt = baseInt.abs();
                    
                    auto factors = baseInt.factorize();
                    SymExpr outside(BigInt(1));
                    BigInt insideInt(1);
                    
                    for (const auto& f : factors) {
                        BigInt p = f.first;
                        BigInt k(f.second);
                        BigInt totalPow = k * m;
                        
                        BigInt q = totalPow / n_den;
                        BigInt r = totalPow % n_den;
                        if (r.isNegative()) {
                            r = r + n_den;
                            q = q - BigInt(1);
                        }
                        
                        if (!q.isZero()) {
                            SymExpr term = SymExpr(p) ^ SymExpr(q);
                            if (outside.isOne()) outside = term;
                            else outside = SymExpr(std::make_shared<SymMul>(std::vector<std::shared_ptr<SymNode>>{outside.ptr, term.ptr}));
                        }
                        if (!r.isZero()) {
                            BigInt pr(1);
                            for(BigInt i(0); i < r; i = i + BigInt(1)) pr = pr * p;
                            insideInt = insideInt * pr;
                        }
                    }
                    
                    SymExpr res = outside;
                    if (insideInt > BigInt(1)) {
                        SymExpr insideSym(insideInt);
                        SymExpr fracSym(Fraction(BigInt(1), n_den));
                        SymExpr powPart(std::make_shared<SymPow>(insideSym.ptr, fracSym.ptr));
                        if (res.isOne()) res = powPart;
                        else res = SymExpr(std::make_shared<SymMul>(std::vector<std::shared_ptr<SymNode>>{res.ptr, powPart.ptr}));
                    }
                    
                    if (isNeg) {
                        BigInt q_neg = m / n_den;
                        BigInt r_neg = m % n_den;
                        if (r_neg.isNegative()) {
                            r_neg = r_neg + n_den;
                            q_neg = q_neg - BigInt(1);
                        }
                        
                        auto multiplyRes = [&](SymExpr factor) {
                            if (res.isOne()) res = factor;
                            else res = SymExpr(std::make_shared<SymMul>(std::vector<std::shared_ptr<SymNode>>{factor.ptr, res.ptr}));
                        };

                        if (!(q_neg % BigInt(2)).isZero()) {
                            multiplyRes(SymExpr(BigInt(-1)));
                        }
                        
                        if (!r_neg.isZero()) {
                            if (!(n_den % BigInt(2)).isZero()) {
                                if (!(r_neg % BigInt(2)).isZero()) {
                                    multiplyRes(SymExpr(BigInt(-1)));
                                }
                            } else if (n_den == BigInt(2) && r_neg == BigInt(1)) {
                                multiplyRes(SymExpr::makeVar("i"));
                            } else {
                                SymExpr minusOne(BigInt(-1));
                                SymExpr fracSym(Fraction(r_neg, n_den));
                                SymExpr powPart(std::make_shared<SymPow>(minusOne.ptr, fracSym.ptr));
                                multiplyRes(powPart);
                            }
                        }
                    }
                    return res;
                };

                if (std::holds_alternative<BigInt>(baseNum->value)) {
                    return processIntBase(std::get<BigInt>(baseNum->value));
                } else if (std::holds_alternative<Fraction>(baseNum->value)) {
                    Fraction baseF = std::get<Fraction>(baseNum->value);
                    SymExpr numRes = processIntBase(baseF.getNum());
                    SymExpr denRes = processIntBase(baseF.getDen());
                    return numRes / denRes;
                } else if (std::holds_alternative<double>(baseNum->value)) {
                    try {
                        Value result = casValToValue(baseNum->value) ^ casValToValue(expNum->value);
                        return result.asSymbolic();
                    } catch (...) {}
                }
            } else if (std::holds_alternative<double>(expNum->value)) {
                try {
                    Value result = casValToValue(baseNum->value) ^ casValToValue(expNum->value);
                    return result.asSymbolic();
                } catch (...) {}
            }
            // 非整数指数保留符号形式（由 Value 的升维机制自动保障）
        }

        // 假分数指数拆分: x^(3/2) -> x * x^(1/2)
        if (b.ptr->getType() == SymType::NUM) {
            auto expNum = std::static_pointer_cast<SymNum>(b.ptr);
            if (std::holds_alternative<Fraction>(expNum->value)) {
                Fraction expF = std::get<Fraction>(expNum->value);
                BigInt m = expF.getNum();
                BigInt n_den = expF.getDen();
                if (n_den > BigInt(1) && m.abs() > n_den) {
                    BigInt q = m / n_den;
                    BigInt r = m % n_den;
                    if (r.isNegative()) {
                        r = r + n_den;
                        q = q - BigInt(1);
                    }
                    if (!q.isZero() && !r.isZero()) {
                        SymExpr part1 = a ^ SymExpr(q);
                        SymExpr part2 = a ^ SymExpr(Fraction(r, n_den));
                        return SymExpr(std::make_shared<SymMul>(std::vector<std::shared_ptr<SymNode>>{part1.ptr, part2.ptr}));
                    }
                }
            }
        }

        // 幂的幂法则: (a^m)^n = a^(m*n)
        if (a.ptr->getType() == SymType::POW) {
            auto powNode = std::static_pointer_cast<SymPow>(a.ptr);
            SymExpr newExp = SymExpr(powNode->exp) * b;
            return SymExpr(powNode->base) ^ newExp;
        }

        // 乘积分配律: (a*b*c)^n = a^n * b^n * c^n
        if (a.ptr->getType() == SymType::MUL && b.ptr->getType() == SymType::NUM) {
            auto mulNode = std::static_pointer_cast<SymMul>(a.ptr);
            SymExpr result(BigInt(1));
            for (auto& factor : mulNode->args)
                result = result * (SymExpr(factor) ^ b);
            return result;
        }

        return SymExpr(std::make_shared<SymPow>(a.ptr, b.ptr));
    }

    
    // =================================================================
    // AST 体积计算器
    // =================================================================
    static int countNodes(const std::shared_ptr<SymNode>& node, std::unordered_set<const SymNode*>& visited) {
        if (!node) return 0;
        if (!visited.insert(node.get()).second) return 0;
        int count = 1;
        switch (node->getType()) {
        case SymType::ADD:
            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args)
                count += countNodes(arg, visited);
            break;
        case SymType::MUL:
            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args)
                count += countNodes(arg, visited);
            break;
        case SymType::POW:
            count += countNodes(std::static_pointer_cast<SymPow>(node)->base, visited);
            count += countNodes(std::static_pointer_cast<SymPow>(node)->exp, visited);
            break;
        case SymType::FUNC:
            for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args)
                count += countNodes(arg, visited);
            break;
        default: break;
        }
        return count;
    }

    int getAstNodeCount(const SymExpr& expr) {
        std::unordered_set<const SymNode*> visited;
        return countNodes(expr.ptr, visited);
    }

    // =================================================================
    // 代数重写引擎 (Pattern Matching Engine)
    // =================================================================
    
    // 检查节点是否为万能通配符
    static bool isWildcard(const std::shared_ptr<SymNode>& node, std::string& outName) {
        if (node && node->getType() == SymType::VAR) {
            std::string name = std::static_pointer_cast<SymVar>(node)->name;
            if (!name.empty() && name[0] == '_') {
                outName = name;
                return true;
            }
        }
        return false;
    }

    // 检查节点子树中是否包含万能通配符
    static bool hasWildcard(const std::shared_ptr<SymNode>& node) {
        if (!node) return false;
        if (node->getType() == SymType::VAR) {
            std::string name = std::static_pointer_cast<SymVar>(node)->name;
            return !name.empty() && name[0] == '_';
        }
        switch (node->getType()) {
            case SymType::ADD:
                for (auto& a : std::static_pointer_cast<SymAdd>(node)->args) if (hasWildcard(a)) return true;
                break;
            case SymType::MUL:
                for (auto& a : std::static_pointer_cast<SymMul>(node)->args) if (hasWildcard(a)) return true;
                break;
            case SymType::POW:
                return hasWildcard(std::static_pointer_cast<SymPow>(node)->base) || hasWildcard(std::static_pointer_cast<SymPow>(node)->exp);
            case SymType::FUNC:
                for (auto& a : std::static_pointer_cast<SymFunc>(node)->args) if (hasWildcard(a)) return true;
                break;
            default: break;
        }
        return false;
    }

    // 精确匹配 AST 并记录捕获变量 (支持 ADD/MUL 的全排列交换律检测)
    bool matchAST(const std::shared_ptr<SymNode>& node, const std::shared_ptr<SymNode>& pat, std::map<std::string, SymExpr>& captures) {
        if (!node || !pat) return false;
        
        std::string wcName;
        // 1. 若 pat 是通配符
        if (isWildcard(pat, wcName)) {
            auto it = captures.find(wcName);
            if (it != captures.end()) {
                return it->second == SymExpr(node);
            } else {
                captures[wcName] = SymExpr(node);
                return true;
            }
        }
        
        // 2. 类型检查
        if (node->getType() != pat->getType()) return false;
        
        // 3 & 4 & 5. 常规分支与交换律分支
        switch (node->getType()) {
            case SymType::NUM:
                return node->toString() == pat->toString();
            case SymType::VAR:
                return std::static_pointer_cast<SymVar>(node)->name == std::static_pointer_cast<SymVar>(pat)->name;
            case SymType::POW: {
                auto pNode = std::static_pointer_cast<SymPow>(node);
                auto pPat = std::static_pointer_cast<SymPow>(pat);
                std::map<std::string, SymExpr> tempCaptures = captures;
                if (matchAST(pNode->base, pPat->base, tempCaptures) && matchAST(pNode->exp, pPat->exp, tempCaptures)) {
                    captures = tempCaptures;
                    return true;
                }
                return false;
            }
            case SymType::FUNC: {
                auto fNode = std::static_pointer_cast<SymFunc>(node);
                auto fPat = std::static_pointer_cast<SymFunc>(pat);
                if (fNode->name != fPat->name || fNode->args.size() != fPat->args.size()) return false;
                std::map<std::string, SymExpr> tempCaptures = captures;
                for (size_t i = 0; i < fNode->args.size(); ++i) {
                    if (!matchAST(fNode->args[i], fPat->args[i], tempCaptures)) return false;
                }
                captures = tempCaptures;
                return true;
            }
            case SymType::ADD:
            case SymType::MUL: {
                const auto& nArgs = (node->getType() == SymType::ADD) ? std::static_pointer_cast<SymAdd>(node)->args : std::static_pointer_cast<SymMul>(node)->args;
                const auto& pArgs = (pat->getType() == SymType::ADD) ? std::static_pointer_cast<SymAdd>(pat)->args : std::static_pointer_cast<SymMul>(pat)->args;
                
                if (nArgs.size() != pArgs.size()) return false;
                
                // 预处理：精确匹配抵消 (剔除不含通配符的相同项)
                std::vector<bool> nUsed(nArgs.size(), false);
                std::vector<bool> pUsed(pArgs.size(), false);
                int matchCount = 0;

                for (size_t j = 0; j < pArgs.size(); ++j) {
                    if (!hasWildcard(pArgs[j])) {
                        for (size_t i = 0; i < nArgs.size(); ++i) {
                            if (!nUsed[i] && nArgs[i] == pArgs[j]) {
                                nUsed[i] = true;
                                pUsed[j] = true;
                                matchCount++;
                                break;
                            }
                        }
                    }
                }

                if (matchCount == nArgs.size()) return true;

                // 提取剩余的待匹配项
                std::vector<std::shared_ptr<SymNode>> remN, remP;
                for (size_t i = 0; i < nArgs.size(); ++i) if (!nUsed[i]) remN.push_back(nArgs[i]);
                for (size_t j = 0; j < pArgs.size(); ++j) if (!pUsed[j]) remP.push_back(pArgs[j]);

                // DFS 回溯匹配剩余项 (带剪枝)
                std::vector<bool> remNUsed(remN.size(), false);
                std::function<bool(size_t, std::map<std::string, SymExpr>&)> dfs = [&](size_t pIdx, std::map<std::string, SymExpr>& curCaps) -> bool {
                    if (pIdx == remP.size()) return true;
                    for (size_t i = 0; i < remN.size(); ++i) {
                        if (!remNUsed[i]) {
                            std::map<std::string, SymExpr> nextCaps = curCaps;
                            if (matchAST(remN[i], remP[pIdx], nextCaps)) {
                                remNUsed[i] = true;
                                if (dfs(pIdx + 1, nextCaps)) {
                                    curCaps = nextCaps;
                                    return true;
                                }
                                remNUsed[i] = false;
                            }
                        }
                    }
                    return false;
                };

                return dfs(0, captures);
            }
        }
        return false;
    }

    // 将捕获的 AST 塞回目标模板中
    SymExpr substituteCaptures(const SymExpr& target, const std::map<std::string, SymExpr>& captures) {
        if (!target.ptr) return target;
        
        std::string wcName;
        if (isWildcard(target.ptr, wcName)) {
            auto it = captures.find(wcName);
            if (it != captures.end()) return it->second;
            return target;
        }
        
        switch (target.ptr->getType()) {
            case SymType::NUM:
            case SymType::VAR:
                return target;
            case SymType::ADD: {
                auto add = std::static_pointer_cast<SymAdd>(target.ptr);
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) res = res + substituteCaptures(SymExpr(arg), captures);
                return res;
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(target.ptr);
                SymExpr res(BigInt(1));
                for (auto& arg : mul->args) res = res * substituteCaptures(SymExpr(arg), captures);
                return res;
            }
            case SymType::POW: {
                auto pow = std::static_pointer_cast<SymPow>(target.ptr);
                return substituteCaptures(SymExpr(pow->base), captures) ^ substituteCaptures(SymExpr(pow->exp), captures);
            }
            case SymType::FUNC: {
                auto func = std::static_pointer_cast<SymFunc>(target.ptr);
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : func->args) newArgs.push_back(substituteCaptures(SymExpr(arg), captures).ptr);
                return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
            }
        }
        return target;
    }

    // 核心入口：后序遍历并尝试重写
    SymExpr applyRule(const SymExpr& expr, const SymExpr& pattern, const SymExpr& target) {
        if (!expr.ptr) return expr;
        
        // 1. 无情探底（Post-order traversal）
        SymExpr current = expr;
        bool childChanged = false;
        
        switch (expr.ptr->getType()) {
            case SymType::ADD: {
                auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : add->args) {
                    SymExpr newArg = applyRule(SymExpr(arg), pattern, target);
                    newArgs.push_back(newArg.ptr);
                    if (newArg.ptr != arg) childChanged = true;
                }
                if (childChanged) {
                    SymExpr res(BigInt(0));
                    for (auto& arg : newArgs) res = res + SymExpr(arg);
                    current = res;
                }
                break;
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : mul->args) {
                    SymExpr newArg = applyRule(SymExpr(arg), pattern, target);
                    newArgs.push_back(newArg.ptr);
                    if (newArg.ptr != arg) childChanged = true;
                }
                if (childChanged) {
                    SymExpr res(BigInt(1));
                    for (auto& arg : newArgs) res = res * SymExpr(arg);
                    current = res;
                }
                break;
            }
            case SymType::POW: {
                auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
                SymExpr newBase = applyRule(SymExpr(pow->base), pattern, target);
                SymExpr newExp = applyRule(SymExpr(pow->exp), pattern, target);
                if (newBase.ptr != pow->base || newExp.ptr != pow->exp) {
                    current = newBase ^ newExp;
                }
                break;
            }
            case SymType::FUNC: {
                auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : func->args) {
                    SymExpr newArg = applyRule(SymExpr(arg), pattern, target);
                    newArgs.push_back(newArg.ptr);
                    if (newArg.ptr != arg) childChanged = true;
                }
                if (childChanged) {
                    current = SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
                }
                break;
            }
            default: break;
        }
        
        // 2. 尝试整体 matchAST
        std::map<std::string, SymExpr> captures;
        if (matchAST(current.ptr, pattern.ptr, captures)) {
            return substituteCaptures(target, captures);
        }
        
        // 3. 子集拦截匹配 (Subset Matching)
        if ((current.ptr->getType() == SymType::ADD && pattern.ptr->getType() == SymType::ADD) ||
            (current.ptr->getType() == SymType::MUL && pattern.ptr->getType() == SymType::MUL)) {
            
            const auto& cArgs = (current.ptr->getType() == SymType::ADD) ? std::static_pointer_cast<SymAdd>(current.ptr)->args : std::static_pointer_cast<SymMul>(current.ptr)->args;
            const auto& pArgs = (pattern.ptr->getType() == SymType::ADD) ? std::static_pointer_cast<SymAdd>(pattern.ptr)->args : std::static_pointer_cast<SymMul>(pattern.ptr)->args;
            
            size_t N = cArgs.size();
            size_t K = pArgs.size();
            
            if (N > K) {
                // 预处理：精确匹配抵消
                std::vector<bool> cUsed(N, false);
                std::vector<bool> pUsed(K, false);

                for (size_t j = 0; j < K; ++j) {
                    if (!hasWildcard(pArgs[j])) {
                        for (size_t i = 0; i < N; ++i) {
                            if (!cUsed[i] && cArgs[i] == pArgs[j]) {
                                cUsed[i] = true;
                                pUsed[j] = true;
                                break;
                            }
                        }
                    }
                }

                std::vector<std::shared_ptr<SymNode>> remP;
                for (size_t j = 0; j < K; ++j) if (!pUsed[j]) remP.push_back(pArgs[j]);

                std::map<std::string, SymExpr> finalCaptures;
                std::vector<bool> finalCUsed;

                // DFS 回溯寻找子集匹配
                std::function<bool(size_t, std::map<std::string, SymExpr>&, std::vector<bool>&)> dfsSubset = 
                    [&](size_t pIdx, std::map<std::string, SymExpr>& curCaps, std::vector<bool>& curCUsed) -> bool {
                    if (pIdx == remP.size()) {
                        finalCaptures = curCaps;
                        finalCUsed = curCUsed;
                        return true;
                    }
                    for (size_t i = 0; i < N; ++i) {
                        if (!curCUsed[i]) {
                            std::map<std::string, SymExpr> nextCaps = curCaps;
                            if (matchAST(cArgs[i], remP[pIdx], nextCaps)) {
                                curCUsed[i] = true;
                                if (dfsSubset(pIdx + 1, nextCaps, curCUsed)) return true;
                                curCUsed[i] = false;
                            }
                        }
                    }
                    return false;
                };

                std::map<std::string, SymExpr> initialCaps;
                if (dfsSubset(0, initialCaps, cUsed)) {
                    SymExpr replaced = substituteCaptures(target, finalCaptures);
                    std::vector<std::shared_ptr<SymNode>> remaining;
                    for (size_t i = 0; i < N; ++i) {
                        if (!finalCUsed[i]) remaining.push_back(cArgs[i]);
                    }
                    if (current.ptr->getType() == SymType::ADD) {
                        SymExpr res = replaced;
                        for (auto& rem : remaining) res = res + SymExpr(rem);
                        return res;
                    } else {
                        SymExpr res = replaced;
                        for (auto& rem : remaining) res = res * SymExpr(rem);
                        return res;
                    }
                }
            }
        }
        
        return current;
    }

    // =================================================================
// 符号展开：带有防爆截断额度 maxPowTerms
// =================================================================
    SymExpr expand(const SymExpr& expr, int64_t maxPowTerms) {
        if (!expr.ptr) return expr;
        if (maxPowTerms <= 0) maxPowTerms = SymConfig::maxExpandTerms;

        switch (expr.ptr->getType()) {
        case SymType::NUM:
        case SymType::VAR:
            return expr;

        case SymType::ADD: {
            SymExpr result(BigInt(0));
            // 向下传递 maxPowTerms
            for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) {
                result = result + expand(SymExpr(arg), maxPowTerms);
            }
            return result;
        }

        case SymType::MUL: {
            auto mulNode = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            
            for (auto& arg : mulNode->args) {
                SymExpr factor = expand(SymExpr(arg), maxPowTerms);
                
                std::vector<std::shared_ptr<SymNode>> leftTerms;
                if (result.ptr->getType() == SymType::ADD) {
                    for (auto& a : std::static_pointer_cast<SymAdd>(result.ptr)->args) leftTerms.push_back(a);
                } else if (!result.isZero()) {
                    leftTerms.push_back(result.ptr);
                }
                
                std::vector<std::shared_ptr<SymNode>> rightTerms;
                if (factor.ptr->getType() == SymType::ADD) {
                    for (auto& a : std::static_pointer_cast<SymAdd>(factor.ptr)->args) rightTerms.push_back(a);
                } else if (!factor.isZero()) {
                    rightTerms.push_back(factor.ptr);
                }
                
                if (leftTerms.empty() || rightTerms.empty()) {
                    result = SymExpr(BigInt(0));
                    continue;
                }
                
                std::vector<std::shared_ptr<SymNode>> nextTerms;
                nextTerms.reserve(leftTerms.size() * rightTerms.size());
                for (auto& l : leftTerms) {
                    for (auto& r : rightTerms) {
                        nextTerms.push_back((SymExpr(l) * SymExpr(r)).ptr);
                    }
                }
                
                if (nextTerms.size() > static_cast<size_t>(maxPowTerms)) {
                    throw std::runtime_error("Math Error: Expansion exceeded max terms limit.");
                }
                
                // 过滤零项并利用 operator+ 的展平与合并机制，一次性合并所有项！
                std::vector<std::shared_ptr<SymNode>> nonZeroTerms;
                nonZeroTerms.reserve(nextTerms.size());
                for (auto& t : nextTerms) {
                    if (!SymExpr(t).isZero()) nonZeroTerms.push_back(t);
                }
                
                if (nonZeroTerms.empty()) {
                    result = SymExpr(BigInt(0));
                } else if (nonZeroTerms.size() == 1) {
                    result = SymExpr(nonZeroTerms[0]);
                } else {
                    SymExpr firstTerm(nonZeroTerms[0]);
                    nonZeroTerms.erase(nonZeroTerms.begin());
                    SymExpr restAdd(std::make_shared<SymAdd>(std::move(nonZeroTerms)));
                    // 强制触发 operator+ 的 O(N) 同类项合并
                    result = firstTerm + restAdd;
                }
            }
            return result;
        }
        // ─────────────────────────────────────────────
        // 幂次展开：多项式定理极限优化版 (去除一切多余冗余)
        // ─────────────────────────────────────────────
        case SymType::POW: {
            auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
            SymExpr baseExp = expand(SymExpr(powNode->base), maxPowTerms);
            SymExpr expExp = expand(SymExpr(powNode->exp), maxPowTerms);

            if (expExp.ptr->getType() == SymType::NUM) {
                auto numNode = std::static_pointer_cast<SymNum>(expExp.ptr);
                auto [isInt, n] = extractExactInt(numNode->value);

                if (isInt && n >= 0 && n <= 1000) {
                    if (baseExp.ptr->getType() == SymType::ADD) {
                        auto addNode = std::static_pointer_cast<SymAdd>(baseExp.ptr);
                        int m = static_cast<int>(addNode->args.size());

                        BigInt T(1);
                        for (int i = 1; i <= m - 1; ++i) {
                            T = (T * BigInt(n + m - i)) / BigInt(i);
                        }

                        if (T <= BigInt(maxPowTerms)) {
                            // ★ 优化 1：使用容器集中暂存节点，彻底避开 operator+ 的 O(N^2) 合并风暴
                            std::vector<std::shared_ptr<SymNode>> finalTerms;
                            finalTerms.reserve(static_cast<size_t>(T.toDouble()));

                            // ★ 优化 2：针对二项式的光速直通车 (m = 2)，0 次大数阶乘运算
                            if (m == 2) {
                                SymExpr A(addNode->args[0]);
                                SymExpr B(addNode->args[1]);
                                BigInt C(1);

                                for (int64_t k = 0; k <= n; ++k) {
                                    SymExpr term(C);
                                    if (n - k > 0) term = term * (A ^ SymExpr(BigInt(n - k)));
                                    if (k > 0)     term = term * (B ^ SymExpr(BigInt(k)));

                                    finalTerms.push_back(term.ptr); // 直接存入底层节点

                                    // O(1) 光速增量更新组合系数
                                    C = (C * BigInt(n - k)) / BigInt(k + 1);
                                }
                            }
                            // ★ 优化 3：高级多元多项式引擎 (m > 2)，引入预备阶乘查表优化
                            else {
                                // 一次性准备好所需的所有阶乘数据，彻底消灭重复劳动
                                std::vector<BigInt> facts(n + 1, BigInt(1));
                                for (int64_t i = 1; i <= n; ++i) {
                                    facts[i] = facts[i - 1] * BigInt(i);
                                }

                                std::vector<int64_t> ks;
                                ks.reserve(m);

                                std::function<void(int, int64_t)> generateMulti = [&](int varIdx, int64_t remainN) {
                                    if (varIdx == m - 1) {
                                        ks.push_back(remainN);

                                        // 查表级极速获取系数：n! / (k1! * k2! ...)
                                        BigInt coeff = facts[n];
                                        for (int64_t k : ks) {
                                            if (k > 1) coeff = coeff / facts[k];
                                        }

                                        SymExpr term(coeff);
                                        for (int i = 0; i < m; ++i) {
                                            if (ks[i] > 0) {
                                                term = term * (SymExpr(addNode->args[i]) ^ SymExpr(BigInt(ks[i])));
                                            }
                                        }

                                        finalTerms.push_back(term.ptr); // 直入节点池
                                        ks.pop_back();
                                        return;
                                    }

                                    for (int64_t i = 0; i <= remainN; ++i) {
                                        ks.push_back(i);
                                        generateMulti(varIdx + 1, remainN - i);
                                        ks.pop_back();
                                    }
                                    };
                                generateMulti(0, n);
                            }

                            // 极速组装：一口气将几十上百个节点封入一把加法树中，省去全部树并排开销！
                            std::sort(finalTerms.begin(), finalTerms.end(), [](const auto& a, const auto& b) {
                                return compareSymNodes(a, b) < 0;
                            });
                            if (finalTerms.size() == 1) return SymExpr(finalTerms[0]);
                            return SymExpr(std::make_shared<SymAdd>(std::move(finalTerms)));
                        }
                    }

                    if (baseExp.ptr->getType() == SymType::MUL) {
                        auto mulNode = std::static_pointer_cast<SymMul>(baseExp.ptr);
                        SymExpr result(BigInt(1));
                        for (auto& arg : mulNode->args) {
                            result = result * expand(SymExpr(arg) ^ expExp, maxPowTerms);
                        }
                        return result;
                    }
                }
            }
            return baseExp ^ expExp;
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> expArgs;
            for (auto& arg : func->args) {
                expArgs.push_back(expand(SymExpr(arg), maxPowTerms).ptr); // 传递
            }

            if ((func->name == "sin" || func->name == "cos") && expArgs.size() == 1) {
                SymExpr inner(expArgs[0]);
                
                // 1. 和差公式展开: sin(A + B) = sin(A)cos(B) + cos(A)sin(B)
                if (inner.ptr->getType() == SymType::ADD) {
                    auto add = std::static_pointer_cast<SymAdd>(inner.ptr);
                    SymExpr A(add->args[0]);
                    std::vector<std::shared_ptr<SymNode>> restArgs(add->args.begin() + 1, add->args.end());
                    SymExpr B = (restArgs.size() == 1) ? SymExpr(restArgs[0]) : SymExpr(std::make_shared<SymAdd>(restArgs));
                    
                    SymExpr sinA(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{A.ptr}));
                    SymExpr cosA(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{A.ptr}));
                    SymExpr sinB(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                    SymExpr cosB(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                    
                    if (func->name == "sin") return expand(sinA * cosB + cosA * sinB, maxPowTerms);
                    if (func->name == "cos") return expand(cosA * cosB - sinA * sinB, maxPowTerms);
                }
                
                // 2. 倍角公式展开: sin(n*x)
                if (inner.ptr->getType() == SymType::MUL) {
                    auto mul = std::static_pointer_cast<SymMul>(inner.ptr);
                    if (mul->args[0]->getType() == SymType::NUM) {
                        auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                        if (isInt) {
                            if (n < 0) {
                                SymExpr posInner = inner / SymExpr(BigInt(-1));
                                if (func->name == "sin") return expand(-SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{posInner.ptr})), maxPowTerms);
                                if (func->name == "cos") return expand(SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{posInner.ptr})), maxPowTerms);
                            }
                            else if (n > 1) {
                                SymExpr X = inner / SymExpr(BigInt(n));
                                SymExpr A = SymExpr(BigInt(n - 1)) * X;
                                SymExpr B = X;
                                
                                SymExpr sinA(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{A.ptr}));
                                SymExpr cosA(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{A.ptr}));
                                SymExpr sinB(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                                SymExpr cosB(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                                
                                if (func->name == "sin") return expand(sinA * cosB + cosA * sinB, maxPowTerms);
                                if (func->name == "cos") return expand(cosA * cosB - sinA * sinB, maxPowTerms);
                            }
                        }
                    }
                }
            }

            if (func->name == "log" && expArgs.size() == 1) {
                SymExpr inner(expArgs[0]);

                if (inner.ptr->getType() == SymType::MUL) {
                    auto mul = std::static_pointer_cast<SymMul>(inner.ptr);
                    SymExpr sum(BigInt(0));
                    for (auto& factor : mul->args) {
                        SymExpr subLog(std::make_shared<SymFunc>(func->name, std::vector<std::shared_ptr<SymNode>>{ factor }));
                        sum = sum + expand(subLog, maxPowTerms); // 传递
                    }
                    return sum;
                }

                if (inner.ptr->getType() == SymType::POW) {
                    auto powN = std::static_pointer_cast<SymPow>(inner.ptr);
                    SymExpr logA(std::make_shared<SymFunc>(func->name, std::vector<std::shared_ptr<SymNode>>{ powN->base }));
                    return expand(SymExpr(powN->exp) * logA, maxPowTerms); // 传递
                }
            }
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(expArgs)));
        }
        }
        return expr;
    }

    static std::pair<bool, Value> tryEvalConst(const SymExpr& expr) {
        if (!expr.ptr) return {false, Value()};
        switch (expr.ptr->getType()) {
            case SymType::NUM:
                return {true, casValToValue(std::static_pointer_cast<SymNum>(expr.ptr)->value)};
            case SymType::VAR: {
                auto name = std::static_pointer_cast<SymVar>(expr.ptr)->name;
                if (name == "PI") return {true, Value(3.14159265358979323846)};
                if (name == "E") return {true, Value(2.71828182845904523536)};
                if (name == "i" || name == "I") return {true, Value(Complex(0.0, 1.0))};
                return {false, Value()};
            }
            case SymType::ADD: {
                Value sum(0.0);
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) {
                    auto [ok, v] = tryEvalConst(SymExpr(arg));
                    if (!ok) return {false, Value()};
                    sum = sum + v;
                }
                return {true, sum};
            }
            case SymType::MUL: {
                Value prod(1.0);
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) {
                    auto [ok, v] = tryEvalConst(SymExpr(arg));
                    if (!ok) return {false, Value()};
                    prod = prod * v;
                }
                return {true, prod};
            }
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                auto [ok1, b] = tryEvalConst(SymExpr(p->base));
                if (!ok1) return {false, Value()};
                auto [ok2, e] = tryEvalConst(SymExpr(p->exp));
                if (!ok2) return {false, Value()};
                try {
                    return {true, b ^ e};
                } catch (...) {
                    return {false, Value()};
                }
            }
            case SymType::FUNC:
                return {false, Value()};
        }
        return {false, Value()};
    }

    // ==========================================
    // 代入引擎 (Substitution Engine)
    // ==========================================
    SymExpr subs(const SymExpr& expr, const std::string& var, const SymExpr& val) {
        if (!expr.ptr) return expr;

        switch (expr.ptr->getType()) {
        case SymType::NUM:
            return expr;

        case SymType::VAR: {
            auto v = std::static_pointer_cast<SymVar>(expr.ptr);
            return (v->name == var) ? val : expr;
        }

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(BigInt(0));
            for (auto& arg : add->args)
                result = result + subs(SymExpr(arg), var, val);
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            for (auto& arg : mul->args)
                result = result * subs(SymExpr(arg), var, val);
            return result;
        }

        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            return subs(SymExpr(pow->base), var, val) ^ subs(SymExpr(pow->exp), var, val);
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> newArgs;
            for (auto& arg : func->args)
                newArgs.push_back(subs(SymExpr(arg), var, val).ptr);
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
        }
        }
        return expr;
    }

    // ==========================================
// evalFloat: 全部强转 double（evalf 语义）
// ==========================================
    SymExpr evalFloat(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        auto [ok, val] = tryEvalConst(expr);
        if (ok) {
            try {
                if (val.isComplex()) {
                    Complex c = val.asComplex();
                    if (Tol::isEq(c.imag, 0.0)) return SymExpr(c.real);
                    return SymExpr(c);
                } else {
                    return SymExpr(val.asDouble());
                }
            } catch (...) {
                // 如果 asDouble 失败（例如 val 是 Symbolic 符号表达式），则回退到 AST 遍历
            }
        }

        switch (expr.ptr->getType()) {
        case SymType::NUM: {
            auto num = std::static_pointer_cast<SymNum>(expr.ptr);
            return SymExpr(casValToValue(num->value).asDouble());
        }

        case SymType::VAR: {
            auto v = std::static_pointer_cast<SymVar>(expr.ptr);
            if (v->name == "PI") return SymExpr(3.14159265358979323846);
            if (v->name == "E") return SymExpr(2.71828182845904523536);
            if (v->name == "i" || v->name == "I") return SymExpr(Complex(0.0, 1.0));
            return expr;
        }

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(0.0);
            for (auto& arg : add->args)
                result = result + evalFloat(SymExpr(arg));
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(1.0);
            for (auto& arg : mul->args)
                result = result * evalFloat(SymExpr(arg));
            return result;
        }

        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            SymExpr base = evalFloat(SymExpr(pow->base));
            SymExpr expn = evalFloat(SymExpr(pow->exp));
            return base ^ expn;
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> newArgs;
            for (auto& arg : func->args)
                newArgs.push_back(evalFloat(SymExpr(arg)).ptr);
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
        }
        }
        return expr;
    }

    // ==========================================
    // evalValue: 保留完整类型（Complex 不丢失）
    // ==========================================
    SymExpr evalValue(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        auto [ok, val] = tryEvalConst(expr);
        if (ok) {
            return val.asSymbolic();
        }

        switch (expr.ptr->getType()) {
        case SymType::NUM:
            return expr;  // 保留原始类型

        case SymType::VAR:
            return expr;

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(BigInt(0));
            for (auto& arg : add->args)
                result = result + evalValue(SymExpr(arg));
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            for (auto& arg : mul->args)
                result = result * evalValue(SymExpr(arg));
            return result;
        }

        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            SymExpr base = evalValue(SymExpr(pow->base));
            SymExpr expn = evalValue(SymExpr(pow->exp));
            return base ^ expn;
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> newArgs;
            for (auto& arg : func->args)
                newArgs.push_back(evalValue(SymExpr(arg)).ptr);
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
        }
        }
        return expr;
    }

    // =================================================================
// 微积分引擎 (Calculus Engine)
// =================================================================
    SymExpr diff(const SymExpr& expr, const std::string& var) {
        if (!expr.ptr) return expr;

        switch (expr.ptr->getType()) {
        case SymType::NUM:
            return SymExpr(BigInt(0)); // 常数导数为 0

        case SymType::VAR: {
            auto v = std::static_pointer_cast<SymVar>(expr.ptr);
            // dx/dx = 1,  dy/dx = 0
            return (v->name == var) ? SymExpr(BigInt(1)) : SymExpr(BigInt(0));
        }

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(BigInt(0));
            // 和的导数：(u + v)' = u' + v'
            for (auto& arg : add->args) {
                result = result + diff(SymExpr(arg), var);
            }
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(0));
            // 乘积法则扩展 (u v w)' = u'vw + uv'w + uvw'
            for (size_t i = 0; i < mul->args.size(); ++i) {
                SymExpr term(BigInt(1));
                for (size_t j = 0; j < mul->args.size(); ++j) {
                    if (i == j) term = term * diff(SymExpr(mul->args[j]), var);
                    else        term = term * SymExpr(mul->args[j]);
                }
                result = result + term;
            }
            return result;
        }

        case SymType::POW: {
            auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
            SymExpr u = SymExpr(powNode->base);
            SymExpr v = SymExpr(powNode->exp);
            SymExpr du = diff(u, var);
            SymExpr dv = diff(v, var);

            // 特例 1：底数和指数都没有 var (即常数的常数次幂) -> 会在常数折叠被干掉，但以防万一
            if (du.isZero() && dv.isZero()) return SymExpr(BigInt(0));

            // 特例 2：指数为常数 (幂法则): (u^n)' = n * u^(n-1) * u'
            if (dv.isZero()) {
                return v * (u ^ (v - SymExpr(BigInt(1)))) * du;
            }

            // 一般情况 (广义指数法则): u^v = e^(v * log(u))
            // (u^v)' = u^v * (v' * log(u) + v * u' / u)
            SymExpr log_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
            return (u ^ v) * (dv * log_u + v * du / u);
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            if (func->args.empty()) return SymExpr(BigInt(0));

            std::string name = func->name;
            size_t arity = func->args.size();

            // =========================================================
            // 一元函数 f(u) 链式法则：f(u)' = f'(u) * u'
            // =========================================================
            if (arity == 1) {
                SymExpr u = SymExpr(func->args[0]);
                SymExpr du = diff(u, var);
                if (du.isZero()) return SymExpr(BigInt(0)); // 性能优化：内层导数为0，外层无须计算

                if (name == "sin") {
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return cos_u * du;
                }
                if (name == "cos") {
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return -sin_u * du;
                }
                if (name == "tan") {
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (SymExpr(BigInt(1)) / (cos_u * cos_u)) * du; // sec^2(u)
                }
                if (name == "cot") {
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return -(SymExpr(BigInt(1)) / (sin_u * sin_u)) * du; // -csc^2(u)
                }
                if (name == "sec") {
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (sin_u / (cos_u * cos_u)) * du; // sec(u)tan(u)
                }
                if (name == "csc") {
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return -(cos_u / (sin_u * sin_u)) * du; // -csc(u)cot(u)
                }
                if (name == "exp") {
                    return expr * du; // exp(u)' = exp(u) * u'
                }
                if (name == "log") {
                    return du / u; // log(u)' = u' / u
                }
                if (name == "sqrt") {
                    return du / (SymExpr(BigInt(2)) * expr); // sqrt(u)' = u' / (2*sqrt(u))
                }
                if (name == "abs") {
                    return (u / expr) * du; // |u|' = u/|u| * u' (当 u!=0)
                }
                if (name == "asin") {
                    return du / ((SymExpr(BigInt(1)) - u * u) ^ SymExpr(Fraction(1, 2))); // 1 / sqrt(1-u^2)
                }
                if (name == "acos") {
                    return -du / ((SymExpr(BigInt(1)) - u * u) ^ SymExpr(Fraction(1, 2))); // -1 / sqrt(1-u^2)
                }
                if (name == "atan") {
                    return du / (SymExpr(BigInt(1)) + u * u); // 1 / (1+u^2)
                }
                if (name == "asinh") {
                    return du / ((u * u + SymExpr(BigInt(1))) ^ SymExpr(Fraction(1, 2)));
                }
                if (name == "acosh") {
                    return du / ((u * u - SymExpr(BigInt(1))) ^ SymExpr(Fraction(1, 2)));
                }
                if (name == "atanh") {
                    return du / (SymExpr(BigInt(1)) - u * u);
                }
                if (name == "sinh") {
                    SymExpr cosh_u(std::make_shared<SymFunc>("cosh", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return cosh_u * du;
                }
                if (name == "cosh") {
                    SymExpr sinh_u(std::make_shared<SymFunc>("sinh", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return sinh_u * du;
                }
                if (name == "tanh") {
                    SymExpr cosh_u(std::make_shared<SymFunc>("cosh", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (SymExpr(BigInt(1)) / (cosh_u * cosh_u)) * du;
                }
                if (name == "cbrt") {
                    return du / (SymExpr(BigInt(3)) * (expr ^ SymExpr(BigInt(2))));
                }
                if (name == "sgn" || name == "round" || name == "floor" || name == "ceil" || name == "trunc") {
                    return SymExpr(BigInt(0));
                }
                if (name == "deg") {
                    return (SymExpr(BigInt(180)) / SymExpr::makeVar("PI")) * du;
                }
                if (name == "rad") {
                    return (SymExpr::makeVar("PI") / SymExpr(BigInt(180))) * du;
                }
                if (name == "erf") {
                    SymExpr pi = SymExpr::makeVar("PI");
                    SymExpr minus_u2 = -(u ^ SymExpr(BigInt(2)));
                    SymExpr exp_u(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{minus_u2.ptr}));
                    return (SymExpr(BigInt(2)) / (pi ^ SymExpr(Fraction(1, 2)))) * exp_u * du;
                }
                if (name == "fresnel_s") {
                    SymExpr pi = SymExpr::makeVar("PI");
                    SymExpr arg = (pi / SymExpr(BigInt(2))) * (u ^ SymExpr(BigInt(2)));
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                    return sin_u * du;
                }
                if (name == "fresnel_c") {
                    SymExpr pi = SymExpr::makeVar("PI");
                    SymExpr arg = (pi / SymExpr(BigInt(2))) * (u ^ SymExpr(BigInt(2)));
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                    return cos_u * du;
                }
                if (name == "Si") {
                    SymExpr sin_u(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (sin_u / u) * du;
                }
                if (name == "Ci") {
                    SymExpr cos_u(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (cos_u / u) * du;
                }
                if (name == "Ei") {
                    SymExpr exp_u(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return (exp_u / u) * du;
                }
                if (name == "Li") {
                    SymExpr log_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return du / log_u;
                }
            }
            // =========================================================
            // 二元函数 f(u, v) 偏导分配律：f_x = f_u * u_x + f_v * v_x
            // =========================================================
            else if (arity == 2) {
                SymExpr u = SymExpr(func->args[0]);
                SymExpr v = SymExpr(func->args[1]);
                SymExpr du = diff(u, var);
                SymExpr dv = diff(v, var);

                if (du.isZero() && dv.isZero()) return SymExpr(BigInt(0));

                if (name == "pow") {
                    // POW 作为函数：(u^v)' = u^v * (v'*log(u) + v*u'/u)
                    if (dv.isZero()) {
                        SymExpr power_down(std::make_shared<SymFunc>("pow", std::vector<std::shared_ptr<SymNode>>{u.ptr, (v - SymExpr(1)).ptr}));
                        return v * power_down * du;
                    }
                    SymExpr log_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return expr * (dv * log_u + v * du / u);
                }
                if (name == "root") {
                    // root(u, v) 实际上是 u^(1/v)。将其转换到底层幂节点然后递归求导即可！
                    SymExpr p = u ^ (SymExpr(BigInt(1)) / v);
                    return diff(p, var);
                }
                if (name == "log") {
                    // 指定底数的对数 log(u, v) = log(v) / log(u) (u 为底，v 为真数)
                    // 运用商的导数法则: (f/g)' = (f'g - fg')/g^2
                    SymExpr log_v(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v.ptr}));
                    SymExpr log_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    SymExpr df = dv / v;
                    SymExpr dg = du / u;
                    return (df * log_u - log_v * dg) / (log_u * log_u);
                }
                if (name == "atan2") {
                    // atan2(y, x) => atan2(u, v)
                    // d/dx atan2(u, v) = (u'*v - u*v') / (u^2 + v^2)
                    return (du * v - u * dv) / (u * u + v * v);
                }
            }

            throw std::runtime_error("Calculus Error: Derivative of function '" + name + "' with " + std::to_string(arity) + " argument(s) is not implemented yet.");
        }
        }
        return SymExpr(BigInt(0));
    }

    // =================================================================
// 聚拢引擎 (Contraction Engine)
// log(x) + log(y) → log(x*y)
// c * log(x)       → log(x^c)
// exp(a) * exp(b)  → exp(a+b)
// =================================================================
    SymExpr contract(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        switch (expr.ptr->getType()) {

            // ─────────────────────────────────────────────
            // 加法节点：寻找所有 log 项，合并为一个 log
            // ─────────────────────────────────────────────
        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr logInside(BigInt(1));
            SymExpr otherTerms(BigInt(0));
            int logCount = 0;

            for (auto& arg : add->args) {
                SymExpr term = contract(SymExpr(arg));
                SymExpr coeff(BigInt(1));
                std::shared_ptr<SymFunc> logNode = nullptr;

                // 嗅探：纯 log(x)
                if (term.ptr->getType() == SymType::FUNC) {
                    auto fn = std::static_pointer_cast<SymFunc>(term.ptr);
                    if (fn->name == "log" && fn->args.size() == 1) {
                        logNode = fn;
                    }
                }
                // 嗅探：c * log(x)（包括 -1 * log(x) 即减法）
                else if (term.ptr->getType() == SymType::MUL) {
                    auto mul = std::static_pointer_cast<SymMul>(term.ptr);
                    SymExpr subCoeff(BigInt(1));
                    std::shared_ptr<SymFunc> foundLog = nullptr;
                    for (auto& f : mul->args) {
                        if (!foundLog && f->getType() == SymType::FUNC) {
                            auto fn = std::static_pointer_cast<SymFunc>(f);
                            if (fn->name == "log" && fn->args.size() == 1) {
                                foundLog = fn;
                                continue;
                            }
                        }
                        subCoeff = subCoeff * SymExpr(f);
                    }
                    if (foundLog) {
                        coeff = subCoeff;
                        logNode = foundLog;
                    }
                }

                if (logNode) {
                    logCount++;
                    SymExpr inside(logNode->args[0]);
                    // c * log(x) = log(x^c)，所以乘入 inside^coeff
                    logInside = logInside * (inside ^ coeff);
                }
                else {
                    otherTerms = otherTerms + term;
                }
            }

            if (logCount > 1) {
                SymExpr combinedLog(std::make_shared<SymFunc>(
                    "log",
                    std::vector<std::shared_ptr<SymNode>>{logInside.ptr}));
                return otherTerms + combinedLog;
            }

            // 没有足够的 log 可合并，重建加法节点
            SymExpr rebuilt(BigInt(0));
            for (auto& arg : add->args)
                rebuilt = rebuilt + contract(SymExpr(arg));
            return rebuilt;
        }

                         // ─────────────────────────────────────────────
                         // 乘法节点：寻找所有 exp 项，合并为一个 exp
                         // ─────────────────────────────────────────────
        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr expArg(BigInt(0));
            SymExpr otherFactors(BigInt(1));
            int expCount = 0;

            for (auto& arg : mul->args) {
                SymExpr factor = contract(SymExpr(arg));
                std::shared_ptr<SymFunc> expNode = nullptr;
                SymExpr coeff(BigInt(1));

                // 嗅探：纯 exp(x)
                if (factor.ptr->getType() == SymType::FUNC) {
                    auto fn = std::static_pointer_cast<SymFunc>(factor.ptr);
                    if (fn->name == "exp" && fn->args.size() == 1)
                        expNode = fn;
                }
                // 嗅探：exp(x)^c（如 1/exp(x) = exp(x)^(-1)）
                else if (factor.ptr->getType() == SymType::POW) {
                    auto powN = std::static_pointer_cast<SymPow>(factor.ptr);
                    if (powN->base->getType() == SymType::FUNC) {
                        auto fn = std::static_pointer_cast<SymFunc>(powN->base);
                        if (fn->name == "exp" && fn->args.size() == 1) {
                            expNode = fn;
                            coeff = SymExpr(powN->exp);
                        }
                    }
                }

                if (expNode) {
                    expCount++;
                    expArg = expArg + (SymExpr(expNode->args[0]) * coeff);
                }
                else {
                    otherFactors = otherFactors * factor;
                }
            }

            if (expCount > 1) {
                SymExpr combinedExp(std::make_shared<SymFunc>(
                    "exp",
                    std::vector<std::shared_ptr<SymNode>>{expArg.ptr}));
                return otherFactors * combinedExp;
            }

            // 没有足够的 exp 可合并，重建乘法节点
            SymExpr rebuilt(BigInt(1));
            for (auto& arg : mul->args)
                rebuilt = rebuilt * contract(SymExpr(arg));
            return rebuilt;
        }

        case SymType::POW: {
            auto p = std::static_pointer_cast<SymPow>(expr.ptr);
            return contract(SymExpr(p->base)) ^ contract(SymExpr(p->exp));
        }

        case SymType::FUNC: {
            auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> nArgs;
            for (auto& a : f->args)
                nArgs.push_back(contract(SymExpr(a)).ptr);
            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
        }

        default: break;
        }
        return expr;
    }

    // =================================================================
// 检测 AST 中是否包含指定变量
// =================================================================
    static bool containsVarImpl(const std::shared_ptr<SymNode>& node, const std::string& var, std::unordered_set<const SymNode*>& visited) {
        if (!node) return false;
        if (!visited.insert(node.get()).second) return false;
        switch (node->getType()) {
        case SymType::NUM:  return false;
        case SymType::VAR:  return std::static_pointer_cast<SymVar>(node)->name == var;
        case SymType::ADD:
            for (auto& a : std::static_pointer_cast<SymAdd>(node)->args)
                if (containsVarImpl(a, var, visited)) return true;
            return false;
        case SymType::MUL:
            for (auto& a : std::static_pointer_cast<SymMul>(node)->args)
                if (containsVarImpl(a, var, visited)) return true;
            return false;
        case SymType::POW:
            return containsVarImpl(std::static_pointer_cast<SymPow>(node)->base, var, visited) ||
                containsVarImpl(std::static_pointer_cast<SymPow>(node)->exp, var, visited);
        case SymType::FUNC:
            for (auto& a : std::static_pointer_cast<SymFunc>(node)->args)
                if (containsVarImpl(a, var, visited)) return true;
            return false;
        }
        return false;
    }

    bool containsVar(const std::shared_ptr<SymNode>& node, const std::string& var) {
        std::unordered_set<const SymNode*> visited;
        return containsVarImpl(node, var, visited);
    }

    // =================================================================
// 收集 AST 中出现的所有变量名
// =================================================================
    static void collectAllVarsImpl(const std::shared_ptr<SymNode>& node, std::set<std::string>& vars, std::unordered_set<const SymNode*>& visited) {
        if (!node) return;
        if (!visited.insert(node.get()).second) return;
        switch (node->getType()) {
        case SymType::NUM: break;
        case SymType::VAR:
            vars.insert(std::static_pointer_cast<SymVar>(node)->name);
            break;
        case SymType::ADD:
            for (auto& a : std::static_pointer_cast<SymAdd>(node)->args) collectAllVarsImpl(a, vars, visited);
            break;
        case SymType::MUL:
            for (auto& a : std::static_pointer_cast<SymMul>(node)->args) collectAllVarsImpl(a, vars, visited);
            break;
        case SymType::POW:
            collectAllVarsImpl(std::static_pointer_cast<SymPow>(node)->base, vars, visited);
            collectAllVarsImpl(std::static_pointer_cast<SymPow>(node)->exp, vars, visited);
            break;
        case SymType::FUNC:
            for (auto& a : std::static_pointer_cast<SymFunc>(node)->args) collectAllVarsImpl(a, vars, visited);
            break;
        }
    }

    void collectAllVars(const std::shared_ptr<SymNode>& node, std::set<std::string>& vars) {
        std::unordered_set<const SymNode*> visited;
        collectAllVarsImpl(node, vars, visited);
    }

    // =================================================================
// 基础化简：身份吸收 + expand/contract 博弈
// ★ 不调用 factor，专门用于 factor 内部，防止循环递归
// =================================================================
    SymExpr simplifyCore(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        // 递归化简内层 + 身份吸收法则
        std::shared_ptr<SymNode> newNode = expr.ptr;
        switch (expr.ptr->getType()) {
        case SymType::ADD: {
            SymExpr res(BigInt(0));
            for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args)
                res = res + simplifyCore(SymExpr(arg));
            newNode = res.ptr;
            break;
        }
        case SymType::MUL: {
            SymExpr res(BigInt(1));
            for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args)
                res = res * simplifyCore(SymExpr(arg));
            newNode = res.ptr;
            break;
        }
        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            newNode = (simplifyCore(SymExpr(pow->base)) ^ simplifyCore(SymExpr(pow->exp))).ptr;
            break;
        }
        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> nArgs;
            for (auto& arg : func->args)
                nArgs.push_back(simplifyCore(SymExpr(arg)).ptr);

            if (nArgs.size() == 1) {
                SymExpr inner(nArgs[0]);
                
                if (func->name == "log") {
                    if (inner.isOne()) return SymExpr(BigInt(0));
                    if (inner.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(inner.ptr)->name == "E") {
                        return SymExpr(BigInt(1));
                    }
                    if (inner.ptr->getType() == SymType::FUNC) {
                        auto innerFn = std::static_pointer_cast<SymFunc>(inner.ptr);
                        if (innerFn->name == "exp") return SymExpr(innerFn->args[0]);
                    }
                    if (inner.ptr->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(inner.ptr);
                        if (powNode->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(powNode->base)->name == "E") {
                            return SymExpr(powNode->exp);
                        }
                    }
                }
                
                if (func->name == "exp") {
                    if (inner.isZero()) return SymExpr(BigInt(1));
                    if (inner.ptr->getType() == SymType::FUNC) {
                        auto innerFn = std::static_pointer_cast<SymFunc>(inner.ptr);
                        if (innerFn->name == "log")
                            return SymExpr(innerFn->args[0]);
                    }
                    if (inner.ptr->getType() == SymType::MUL) {
                        auto mul = std::static_pointer_cast<SymMul>(inner.ptr);
                        SymExpr coeff(BigInt(1));
                        std::shared_ptr<SymNode> logArg = nullptr;
                        for (auto& arg : mul->args) {
                            if (arg->getType() == SymType::FUNC) {
                                auto fn = std::static_pointer_cast<SymFunc>(arg);
                                if (fn->name == "log" && fn->args.size() == 1) {
                                    logArg = fn->args[0];
                                    continue;
                                }
                            }
                            coeff = coeff * SymExpr(arg);
                        }
                        if (logArg) {
                            return SymExpr(logArg) ^ coeff;
                        }
                    }
                }
                
                if (func->name == "sin" || func->name == "cos" || func->name == "tan") {
                    if (inner.isZero()) {
                        if (func->name == "sin" || func->name == "tan") return SymExpr(BigInt(0));
                        if (func->name == "cos") return SymExpr(BigInt(1));
                    }
                    
                    auto getPiCoeff = [](const SymExpr& e) -> std::pair<bool, Fraction> {
                        if (e.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(e.ptr)->name == "PI") {
                            return {true, Fraction(1)};
                        }
                        if (e.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(e.ptr);
                            bool hasPi = false;
                            Fraction coeff(1);
                            bool valid = true;
                            for (auto& arg : mul->args) {
                                if (arg->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(arg)->name == "PI") {
                                    hasPi = true;
                                } else if (arg->getType() == SymType::NUM) {
                                    auto num = std::static_pointer_cast<SymNum>(arg);
                                    if (std::holds_alternative<BigInt>(num->value)) coeff = coeff * Fraction(std::get<BigInt>(num->value));
                                    else if (std::holds_alternative<Fraction>(num->value)) coeff = coeff * std::get<Fraction>(num->value);
                                    else valid = false;
                                } else {
                                    valid = false;
                                }
                            }
                            if (valid && hasPi) return {true, coeff};
                        }
                        return {false, Fraction(0)};
                    };
                    
                    auto [isPiMul, piCoeff] = getPiCoeff(inner);
                    if (isPiMul) {
                        Fraction two(2);
                        Fraction c = piCoeff;
                        while (c < Fraction(0)) c = c + two;
                        while (c >= two) c = c - two;
                        
                        if (func->name == "sin") {
                            if (c == Fraction(0) || c == Fraction(1)) return SymExpr(BigInt(0));
                            if (c == Fraction(BigInt(1), BigInt(2))) return SymExpr(BigInt(1));
                            if (c == Fraction(BigInt(3), BigInt(2))) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(5), BigInt(6))) return SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(7), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return SymExpr(Fraction(BigInt(-1), BigInt(2)));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(3), BigInt(4))) return SymExpr(Fraction(BigInt(1), BigInt(2))) * (SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(5), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return SymExpr(Fraction(BigInt(-1), BigInt(2))) * (SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(2), BigInt(3))) return SymExpr(Fraction(BigInt(1), BigInt(2))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(4), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return SymExpr(Fraction(BigInt(-1), BigInt(2))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        } else if (func->name == "cos") {
                            if (c == Fraction(BigInt(1), BigInt(2)) || c == Fraction(BigInt(3), BigInt(2))) return SymExpr(BigInt(0));
                            if (c == Fraction(0)) return SymExpr(BigInt(1));
                            if (c == Fraction(1)) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(2), BigInt(3)) || c == Fraction(BigInt(4), BigInt(3))) return SymExpr(Fraction(BigInt(-1), BigInt(2)));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return SymExpr(Fraction(BigInt(1), BigInt(2))) * (SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(3), BigInt(4)) || c == Fraction(BigInt(5), BigInt(4))) return SymExpr(Fraction(BigInt(-1), BigInt(2))) * (SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return SymExpr(Fraction(BigInt(1), BigInt(2))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(5), BigInt(6)) || c == Fraction(BigInt(7), BigInt(6))) return SymExpr(Fraction(BigInt(-1), BigInt(2))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        } else if (func->name == "tan") {
                            if (c == Fraction(0) || c == Fraction(1)) return SymExpr(BigInt(0));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(5), BigInt(4))) return SymExpr(BigInt(1));
                            if (c == Fraction(BigInt(3), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(7), BigInt(6))) return SymExpr(Fraction(BigInt(1), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(5), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return SymExpr(Fraction(BigInt(-1), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(4), BigInt(3))) return SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(2), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return -(SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        } else if (func->name == "cot") {
                            if (c == Fraction(BigInt(1), BigInt(2)) || c == Fraction(BigInt(3), BigInt(2))) return SymExpr(BigInt(0));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(5), BigInt(4))) return SymExpr(BigInt(1));
                            if (c == Fraction(BigInt(3), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(7), BigInt(6))) return SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(5), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return -(SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(4), BigInt(3))) return SymExpr(Fraction(BigInt(1), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(2), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return SymExpr(Fraction(BigInt(-1), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        } else if (func->name == "sec") {
                            if (c == Fraction(0)) return SymExpr(BigInt(1));
                            if (c == Fraction(1)) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return SymExpr(BigInt(2));
                            if (c == Fraction(BigInt(2), BigInt(3)) || c == Fraction(BigInt(4), BigInt(3))) return SymExpr(BigInt(-2));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(3), BigInt(4)) || c == Fraction(BigInt(5), BigInt(4))) return -(SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return SymExpr(Fraction(BigInt(2), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(5), BigInt(6)) || c == Fraction(BigInt(7), BigInt(6))) return SymExpr(Fraction(BigInt(-2), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        } else if (func->name == "csc") {
                            if (c == Fraction(BigInt(1), BigInt(2))) return SymExpr(BigInt(1));
                            if (c == Fraction(BigInt(3), BigInt(2))) return SymExpr(BigInt(-1));
                            if (c == Fraction(BigInt(1), BigInt(6)) || c == Fraction(BigInt(5), BigInt(6))) return SymExpr(BigInt(2));
                            if (c == Fraction(BigInt(7), BigInt(6)) || c == Fraction(BigInt(11), BigInt(6))) return SymExpr(BigInt(-2));
                            if (c == Fraction(BigInt(1), BigInt(4)) || c == Fraction(BigInt(3), BigInt(4))) return SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2)));
                            if (c == Fraction(BigInt(5), BigInt(4)) || c == Fraction(BigInt(7), BigInt(4))) return -(SymExpr(BigInt(2)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(1), BigInt(3)) || c == Fraction(BigInt(2), BigInt(3))) return SymExpr(Fraction(BigInt(2), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                            if (c == Fraction(BigInt(4), BigInt(3)) || c == Fraction(BigInt(5), BigInt(3))) return SymExpr(Fraction(BigInt(-2), BigInt(3))) * (SymExpr(BigInt(3)) ^ SymExpr(Fraction(BigInt(1), BigInt(2))));
                        }
                    }
                }
                
                if (inner.isZero()) {
                    if (func->name == "erf" || func->name == "fresnel_s" || func->name == "fresnel_c" || func->name == "Si" || func->name == "Li") return SymExpr(BigInt(0));
                }
                if (func->name == "sqrt") {
                    return inner ^ SymExpr(Fraction(BigInt(1), BigInt(2)));
                }
                if (func->name == "cbrt") {
                    return inner ^ SymExpr(Fraction(BigInt(1), BigInt(3)));
                }
            }
            newNode = SymExpr(std::make_shared<SymFunc>(func->name, std::move(nArgs))).ptr;
            break;
        }
        default: break;
        }

        SymExpr current(newNode);

        // expand/contract 二路博弈（不含 factor）
        SymExpr c_expand = current;
        SymExpr c_contract = current;
        SymExpr c_both = current;

        try { c_expand = expand(current, 30); }
        catch (const std::runtime_error&) {}
        try { c_contract = contract(current); }
        catch (const std::runtime_error&) {}
        try {
            if (c_expand.ptr != current.ptr)
                c_both = contract(c_expand);
        }
        catch (const std::runtime_error&) {}

        SymExpr best = current;
        int minSize = getAstNodeCount(current);
        auto tryC = [&](const SymExpr& cand) {
            int sz = getAstNodeCount(cand);
            if (sz < minSize) { minSize = sz; best = cand; }
            };
        tryC(c_expand);
        tryC(c_contract);
        tryC(c_both);

        // 激进的代数数化简：尝试将 best 再次 expand，以强制合并隐藏的同类项（如展开后的根式乘积）
        if (best.ptr->getType() == SymType::ADD || best.ptr->getType() == SymType::MUL) {
            try {
                SymExpr ultra_expand = expand(best, 100);
                if (getAstNodeCount(ultra_expand) < minSize) {
                    best = ultra_expand;
                }
            } catch (...) {}
        }

        return best;
    }


    // =================================================================
    // 轻量化多项式探针 (Polynomial Probe)
    // 机制说明：如果含有非积分变量 (如 y, z)，由于它们不等于 var，
    // 且 containsVar(..., var) 会返回 false，因此它们会被自然地
    // 视为常数 (0次多项式)，从而完美支持多元多项式的判定。
    // =================================================================
    bool isPolynomialIn(const SymExpr& expr, const std::string& var) {
        if (!expr.ptr) return false;
        switch (expr.ptr->getType()) {
            case SymType::NUM:
            case SymType::VAR:
                return true; // 非 var 的其他变量 (如 y) 在这里返回 true，视为常数
            case SymType::ADD: {
                auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
                for (auto& arg : add->args) {
                    if (!isPolynomialIn(SymExpr(arg), var)) return false;
                }
                return true;
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
                for (auto& arg : mul->args) {
                    if (!isPolynomialIn(SymExpr(arg), var)) return false;
                }
                return true;
            }
            case SymType::POW: {
                auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
                if (containsVar(powNode->exp, var)) return false;
                if (containsVar(powNode->base, var)) {
                    if (powNode->exp->getType() != SymType::NUM) return false;
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (!isInt || n < 0) return false;
                }
                return isPolynomialIn(SymExpr(powNode->base), var);
            }
            case SymType::FUNC: {
                auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
                for (auto& arg : func->args) {
                    if (containsVar(arg, var)) return false;
                }
                return true;
            }
        }
        return false;
    }

    // =================================================================
    // 动态多项式系数提取器：无限次 (消除 int64_t 转型警告修正版)
    // =================================================================
    std::vector<SymExpr> extractCoeffs(const SymExpr& expr, const std::string& var) {
        // 探针拦截：如果根本不是多项式，直接拒绝进入昂贵的 expand 展开
        if (!isPolynomialIn(expr, var)) return {};

        SymExpr expanded;
        try { expanded = expand(expr, SymConfig::maxExpandTerms); }
        catch (const std::runtime_error&) { return {}; }

        std::map<int, SymExpr> degreeMap;

        auto processTerm = [&](const SymExpr& term) -> bool {
            // 如果该项完全不包含目标变量 (例如 y^2, sin(y), 或纯数字)，
            // 则整体作为 0 次项系数 (常数项)
            if (!containsVar(term.ptr, var)) {
                degreeMap[0] = degreeMap.count(0) ? degreeMap[0] + term : term;
                return true;
            }
            // 单变量情况：x
            if (term.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(term.ptr)->name == var) {
                degreeMap[1] = degreeMap.count(1) ? degreeMap[1] + SymExpr(BigInt(1)) : SymExpr(BigInt(1));
                return true;
            }
            // 单变量带幂情况：x^n
            if (term.ptr->getType() == SymType::POW) {
                auto p = std::static_pointer_cast<SymPow>(term.ptr);
                if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var && p->exp->getType() == SymType::NUM) {
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                    if (isInt && n >= 0 && n <= 1000) {
                        int deg = static_cast<int>(n); // 安全转换
                        degreeMap[deg] = degreeMap.count(deg) ? degreeMap[deg] + SymExpr(BigInt(1)) : SymExpr(BigInt(1));
                        return true;
                    }
                }
                return false;
            }
            // 乘积混合情况：A * B * x^n
            if (term.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(term.ptr);
                int degree = 0;
                SymExpr coeff(BigInt(1));
                bool foundVarPart = false;

                for (auto& f : mul->args) {
                    // 如果该因子不包含 x (例如 y, z^2, sin(y))，视为系数乘入
                    if (!containsVar(f, var)) {
                        coeff = coeff * SymExpr(f);
                    }
                    // 如果该因子恰好等于 x
                    else if (f->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(f)->name == var) {
                        if (foundVarPart) return false;
                        foundVarPart = true;
                        degree = 1;
                    }
                    // 如果该因子等于 x^n
                    else if (f->getType() == SymType::POW) {
                        auto p = std::static_pointer_cast<SymPow>(f);
                        if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var && p->exp->getType() == SymType::NUM) {
                            if (foundVarPart) return false;
                            foundVarPart = true;

                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                            if (isInt && n >= 0 && n <= 1000) {
                                degree = static_cast<int>(n); // 安全转换
                            }
                            else {
                                return false; // 幂次超标或不是整数，不视作多项式
                            }
                        }
                        else {
                            return false;
                        }
                    }
                    else {
                        return false;
                    }
                }

                degreeMap[degree] = degreeMap.count(degree) ? degreeMap[degree] + coeff : coeff;
                return true;
            }
            return false;
            };

        if (expanded.ptr->getType() == SymType::ADD) {
            for (auto& arg : std::static_pointer_cast<SymAdd>(expanded.ptr)->args) {
                if (!processTerm(SymExpr(arg))) return {};
            }
        }
        else {
            if (!processTerm(expanded)) return {};
        }

        int maxDeg = -1;
        for (auto& kv : degreeMap) {
            maxDeg = std::max(maxDeg, kv.first);
        }

        if (maxDeg < 0) return {};

        // 构造连续的系数数组，从 x^0 填到 x^MAX
        std::vector<SymExpr> coeffs(static_cast<size_t>(maxDeg + 1), SymExpr(BigInt(0)));
        for (auto& kv : degreeMap) {
            coeffs[static_cast<size_t>(kv.first)] = simplifyCore(kv.second);
        }

        return coeffs;
    }

    // =================================================================
    // 多项式代数底座 (Polynomial Algebra)
    // =================================================================
    static void trimCoeffs(std::vector<SymExpr>& a) {
        while (!a.empty() && a.back().isZero()) a.pop_back();
    }

    static std::pair<std::vector<SymExpr>, std::vector<SymExpr>> polyDivCoeffs(std::vector<SymExpr> A, const std::vector<SymExpr>& B) {
        trimCoeffs(A);
        if (B.empty()) throw std::runtime_error("Math Error: Division by zero polynomial.");
        int degA = static_cast<int>(A.size()) - 1;
        int degB = static_cast<int>(B.size()) - 1;
        
        if (degA < degB) return {{}, A};
        
        std::vector<SymExpr> Q(degA - degB + 1, SymExpr(BigInt(0)));
        SymExpr leadB = B.back();
        
        for (int i = degA - degB; i >= 0; --i) {
            if (A[i + degB].isZero()) continue;
            SymExpr q = simplifyCore(expand(A[i + degB] / leadB, SymConfig::maxExpandTerms));
            Q[i] = q;
            for (int j = 0; j <= degB; ++j) {
                A[i + j] = simplifyCore(expand(A[i + j] - q * B[j], SymConfig::maxExpandTerms));
            }
        }
        trimCoeffs(Q);
        trimCoeffs(A);
        return {Q, A};
    }

    static std::vector<SymExpr> polyPseudoRemCoeffs(std::vector<SymExpr> A, const std::vector<SymExpr>& B) {
        trimCoeffs(A);
        if (B.empty()) throw std::runtime_error("Math Error: Division by zero polynomial.");
        int degA = static_cast<int>(A.size()) - 1;
        int degB = static_cast<int>(B.size()) - 1;
        
        int d = degA - degB + 1;
        if (d <= 0) return A;
        
        SymExpr leadB = B.back();
        
        while (degA >= degB) {
            SymExpr leadA = A.back();
            
            for (int i = 0; i <= degA; ++i) {
                A[i] = simplifyCore(expand(A[i] * leadB, SymConfig::maxExpandTerms));
            }
            
            int shift = degA - degB;
            for (int i = 0; i <= degB; ++i) {
                A[i + shift] = simplifyCore(expand(A[i + shift] - B[i] * leadA, SymConfig::maxExpandTerms));
            }
            
            trimCoeffs(A);
            degA = static_cast<int>(A.size()) - 1;
            d--;
        }
        
        if (d > 0) {
            SymExpr multiplier = simplifyCore(expand(leadB ^ SymExpr(BigInt(d)), SymConfig::maxExpandTerms));
            for (auto& c : A) c = simplifyCore(expand(c * multiplier, SymConfig::maxExpandTerms));
        }
        
        return A;
    }

    int getDegree(const SymExpr& expr, const std::string& var) {
        auto coeffs = extractCoeffs(expr, var);
        if (coeffs.empty()) return -1;
        return static_cast<int>(coeffs.size()) - 1;
    }

    std::pair<SymExpr, SymExpr> polyDiv(const SymExpr& dividend, const SymExpr& divisor, const std::string& var) {
        auto coeffsA = extractCoeffs(dividend, var);
        auto coeffsB = extractCoeffs(divisor, var);
        
        if (coeffsB.empty()) throw std::runtime_error("Math Error: Divisor is not a polynomial in " + var);
        if (coeffsA.empty()) return {SymExpr(BigInt(0)), dividend};
        
        auto [coeffsQ, coeffsR] = polyDivCoeffs(coeffsA, coeffsB);
        
        auto toExpr = [&](const std::vector<SymExpr>& coeffs) {
            SymExpr res(BigInt(0));
            SymExpr X = SymExpr::makeVar(var);
            for (size_t i = 0; i < coeffs.size(); ++i) {
                if (!coeffs[i].isZero()) {
                    if (i == 0) res = res + coeffs[i];
                    else if (i == 1) res = res + coeffs[i] * X;
                    else res = res + coeffs[i] * (X ^ SymExpr(BigInt(i)));
                }
            }
            return res;
        };
        
        return {toExpr(coeffsQ), toExpr(coeffsR)};
    }

    SymExpr polyPseudoRem(const SymExpr& dividend, const SymExpr& divisor, const std::string& var) {
        auto coeffsA = extractCoeffs(dividend, var);
        auto coeffsB = extractCoeffs(divisor, var);
        
        if (coeffsB.empty()) throw std::runtime_error("Math Error: Divisor is not a polynomial in " + var);
        if (coeffsA.empty()) return dividend;
        
        auto coeffsR = polyPseudoRemCoeffs(coeffsA, coeffsB);
        
        SymExpr res(BigInt(0));
        SymExpr X = SymExpr::makeVar(var);
        for (size_t i = 0; i < coeffsR.size(); ++i) {
            if (!coeffsR[i].isZero()) {
                if (i == 0) res = res + coeffsR[i];
                else if (i == 1) res = res + coeffsR[i] * X;
                else res = res + coeffsR[i] * (X ^ SymExpr(BigInt(i)));
            }
        }
        return res;
    }

    SymExpr polyGCD(const SymExpr& a, const SymExpr& b, const std::string& var) {
        if (a.isZero()) return b;
        if (b.isZero()) return a;

        auto coeffsA = extractCoeffs(a, var);
        auto coeffsB = extractCoeffs(b, var);
        
        if (coeffsA.empty() || coeffsB.empty()) return SymExpr(BigInt(1));
        
        if (coeffsA.size() < coeffsB.size()) {
            std::swap(coeffsA, coeffsB);
        }

        SymExpr g(BigInt(1));
        SymExpr h(BigInt(1));
        
        while (!coeffsB.empty()) {
            int degA = static_cast<int>(coeffsA.size()) - 1;
            int degB = static_cast<int>(coeffsB.size()) - 1;
            int delta = degA - degB;
            
            auto coeffsR = polyPseudoRemCoeffs(coeffsA, coeffsB);
            if (coeffsR.empty()) {
                coeffsA = coeffsB;
                break;
            }
            
            SymExpr leadB = coeffsB.back();
            
            SymExpr divisor = simplifyCore(expand(g * (h ^ SymExpr(BigInt(delta))), SymConfig::maxExpandTerms));
            
            coeffsA = coeffsB;
            coeffsB = coeffsR;
            for (auto& c : coeffsB) c = simplifyCore(expand(c / divisor, SymConfig::maxExpandTerms));
            trimCoeffs(coeffsB);
            
            g = leadB;
            if (delta > 0) {
                SymExpr h_pow = simplifyCore(expand(h ^ SymExpr(BigInt(delta - 1)), SymConfig::maxExpandTerms));
                h = simplifyCore(expand((g ^ SymExpr(BigInt(delta))) / h_pow, SymConfig::maxExpandTerms));
            }
        }

        if (!coeffsA.empty()) {
            SymExpr lead = coeffsA.back();
            if (!lead.isZero() && !lead.isOne()) {
                for (auto& c : coeffsA) c = simplifyCore(expand(c / lead, SymConfig::maxExpandTerms));
            }
        }
        
        SymExpr res(BigInt(0));
        SymExpr X = SymExpr::makeVar(var);
        for (size_t i = 0; i < coeffsA.size(); ++i) {
            if (!coeffsA[i].isZero()) {
                if (i == 0) res = res + coeffsA[i];
                else if (i == 1) res = res + coeffsA[i] * X;
                else res = res + coeffsA[i] * (X ^ SymExpr(BigInt(i)));
            }
        }
        return res;
    }

    std::vector<std::pair<SymExpr, int>> polySquareFree(const SymExpr& p, const std::string& var) {
        std::vector<std::pair<SymExpr, int>> result;
        SymExpr P = simplifyCore(expand(p, SymConfig::maxExpandTerms));
        if (P.isZero()) return result;
        int maxI = getDegree(P, var);
        if (maxI <= 0) {
            result.push_back({P, 1});
            return result;
        }

        auto coeffs = extractCoeffs(P, var);
        SymExpr lead = coeffs.back();
        SymExpr c = lead;
        P = simplifyCore(expand(P / c, SymConfig::maxExpandTerms));

        SymExpr dP = diff(P, var);
        SymExpr R = polyGCD(P, dP, var);
        SymExpr V = polyDiv(P, R, var).first;
        SymExpr W = polyDiv(dP, R, var).first;

        int i = 1;
        while (getDegree(V, var) > 0) {
            if (i > maxI + 2 || i > SymConfig::maxIterations) {
                throw std::runtime_error("Math Error: polySquareFree failed due to algebraic deadlock.");
            }
            SymExpr dV = diff(V, var);
            SymExpr W_minus_dV = simplifyCore(expand(W - dV, SymConfig::maxExpandTerms));
            SymExpr Y = polyGCD(V, W_minus_dV, var);
            
            if (getDegree(Y, var) > 0) {
                result.push_back({Y, i});
            }
            
            V = polyDiv(V, Y, var).first;
            W = polyDiv(W_minus_dV, Y, var).first;
            i++;
        }
        
        if (!c.isOne()) {
            if (!result.empty()) {
                result[0].first = simplifyCore(expand(result[0].first * c, SymConfig::maxExpandTerms));
            } else {
                result.push_back({c, 1});
            }
        }
        return result;
    }

    std::tuple<SymExpr, SymExpr, SymExpr> polyEGCD(const SymExpr& a, const SymExpr& b, const std::string& var) {
        SymExpr r0 = a, r1 = b;
        SymExpr s0(BigInt(1)), s1(BigInt(0));
        SymExpr t0(BigInt(0)), t1(BigInt(1));

        int iter = 0;
        while (!r1.isZero()) {
            if (++iter > SymConfig::maxIterations) {
                throw std::runtime_error("Math Error: polyEGCD infinite loop detected.");
            }
            auto [q, r] = polyDiv(r0, r1, var);
            r0 = r1; r1 = r;
            SymExpr s_temp = simplifyCore(expand(s0 - q * s1, SymConfig::maxExpandTerms));
            s0 = s1; s1 = s_temp;
            SymExpr t_temp = simplifyCore(expand(t0 - q * t1, SymConfig::maxExpandTerms));
            t0 = t1; t1 = t_temp;
            
            if (getAstNodeCount(s1) > SymConfig::maxAstNodes || getAstNodeCount(t1) > SymConfig::maxAstNodes) {
                throw std::runtime_error("Math Error: polyEGCD failed due to coefficient explosion.");
            }
        }

        auto coeffs = extractCoeffs(r0, var);
        if (!coeffs.empty()) {
            SymExpr lead = coeffs.back();
            if (!lead.isZero() && !lead.isOne()) {
                r0 = simplifyCore(expand(r0 / lead, SymConfig::maxExpandTerms));
                s0 = simplifyCore(expand(s0 / lead, SymConfig::maxExpandTerms));
                t0 = simplifyCore(expand(t0 / lead, SymConfig::maxExpandTerms));
            }
        }
        
        // 最终清理，防止 Bezout 系数中残留未合并的代数数
        return { simplifyCore(expand(r0, SymConfig::maxExpandTerms)), simplifyCore(expand(s0, SymConfig::maxExpandTerms)), simplifyCore(expand(t0, SymConfig::maxExpandTerms)) };
    }

    SymExpr polyResultant(const SymExpr& a, const SymExpr& b, const std::string& var) {
        auto coeffsA = extractCoeffs(a, var);
        auto coeffsB = extractCoeffs(b, var);
        
        if (coeffsA.empty() || coeffsB.empty()) return SymExpr(BigInt(0));
        
        int degA = static_cast<int>(coeffsA.size()) - 1;
        int degB = static_cast<int>(coeffsB.size()) - 1;
        
        if (degA < degB) {
            SymExpr res = polyResultant(b, a, var);
            if ((degA * degB) % 2 != 0) return simplifyCore(expand(-res, SymConfig::maxExpandTerms));
            return res;
        }
        
        if (degB == 0) {
            SymExpr leadB = coeffsB[0];
            return simplifyCore(expand(leadB ^ SymExpr(BigInt(degA)), SymConfig::maxExpandTerms));
        }
        
        SymExpr g(BigInt(1));
        SymExpr h(BigInt(1));
        
        while (true) {
            degA = static_cast<int>(coeffsA.size()) - 1;
            degB = static_cast<int>(coeffsB.size()) - 1;
            if (degB < 0) return SymExpr(BigInt(0));
            
            int delta = degA - degB;
            
            if (degB == 0) {
                if (delta == 0) {
                    return coeffsB[0];
                } else {
                    SymExpr divisor = simplifyCore(expand(h ^ SymExpr(BigInt(delta - 1)), SymConfig::maxExpandTerms));
                    return simplifyCore(expand((coeffsB[0] ^ SymExpr(BigInt(delta))) / divisor, SymConfig::maxExpandTerms));
                }
            }
        
            auto coeffsR = polyPseudoRemCoeffs(coeffsA, coeffsB);
            if (coeffsR.empty()) return SymExpr(BigInt(0));
        
            SymExpr leadB = coeffsB.back();
        
            SymExpr divisor = simplifyCore(expand(g * (h ^ SymExpr(BigInt(delta))), SymConfig::maxExpandTerms));
        
            coeffsA = coeffsB;
            coeffsB = coeffsR;
            for (auto& c : coeffsB) c = simplifyCore(expand(c / divisor, SymConfig::maxExpandTerms));
            trimCoeffs(coeffsB);
        
            g = leadB;
            if (delta > 0) {
                SymExpr h_pow = simplifyCore(expand(h ^ SymExpr(BigInt(delta - 1)), SymConfig::maxExpandTerms));
                h = simplifyCore(expand((g ^ SymExpr(BigInt(delta))) / h_pow, SymConfig::maxExpandTerms));
            }
        }
    }

    // =================================================================
// 尝试对表达式开精确平方根
// 仅处理 NUM, POW(偶数幂), MUL(逐因子开根) 三类结构
// 返回 {是否成功, 平方根表达式}
// =================================================================
    std::pair<bool, SymExpr> trySquareRoot(const SymExpr& expr, bool allowPartial) {
        if (!expr.ptr) return { false, expr };

        // 情况 1：纯常数
        if (expr.ptr->getType() == SymType::NUM) {
            auto numNode = std::static_pointer_cast<SymNum>(expr.ptr);
            auto [isInt, n] = extractExactInt(numNode->value);
            if (isInt) {
                if (n < 0) return { false, expr }; // 负数无实平方根
                if (n == 0) return { true, SymExpr(BigInt(0)) };
                int64_t s = static_cast<int64_t>(std::round(std::sqrt(static_cast<double>(n))));
                // 精度保护：回代验证
                if (s * s == n) return { true, SymExpr(BigInt(s)) };
                
                if (allowPartial) {
                    int64_t outside = 1;
                    int64_t inside = n;
                    for (int64_t i = 2; i * i <= inside; ++i) {
                        while (inside % (i * i) == 0) {
                            outside *= i;
                            inside /= (i * i);
                        }
                    }
                    if (outside > 1) {
                        return { true, SymExpr(BigInt(outside)) * (SymExpr(BigInt(inside)) ^ SymExpr(Fraction(1, 2))) };
                    }
                }
            } else if (std::holds_alternative<Fraction>(numNode->value)) {
                Fraction f = std::get<Fraction>(numNode->value);
                if (f.getNum().isNegative()) return { false, expr };
                auto [numOk, numSqrt] = trySquareRoot(SymExpr(f.getNum()), allowPartial);
                auto [denOk, denSqrt] = trySquareRoot(SymExpr(f.getDen()), allowPartial);
                if (numOk && denOk) {
                    return { true, numSqrt / denSqrt };
                }
            }
            return { false, expr };
        }

        // 情况 2：var（单变量不能整数开根）
        if (expr.ptr->getType() == SymType::VAR) {
            return { false, expr };
        }

        // 情况 3：幂次 base^exp，若 exp 是偶数正整数，sqrt(base^exp) = base^(exp/2)
        if (expr.ptr->getType() == SymType::POW) {
            auto p = std::static_pointer_cast<SymPow>(expr.ptr);
            if (p->exp->getType() == SymType::NUM) {
                auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                if (isInt && n > 0 && n % 2 == 0) {
                    return { true, SymExpr(p->base) ^ SymExpr(BigInt(n / 2)) };
                }
            }
            return { false, expr };
        }

        // 情况 4：乘积，逐因子开根（全部成功才算成功）
        if (expr.ptr->getType() == SymType::MUL) {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            for (auto& arg : mul->args) {
                auto [ok, sqrtArg] = trySquareRoot(SymExpr(arg), allowPartial);
                if (!ok) return { false, expr };
                result = result * sqrtArg;
            }
            return { true, result };
        }

        // 情况 5：加法或函数，结构太复杂，放弃
        return { false, expr };
    }

    // =================================================================
    // 提取有理分式的分子和分母 (Get Numerator and Denominator)
    // =================================================================
    std::pair<SymExpr, SymExpr> getFraction(const SymExpr& expr) {
        if (!expr.ptr) return {expr, SymExpr(BigInt(1))};
        switch (expr.ptr->getType()) {
            case SymType::NUM:
            case SymType::VAR:
                return {expr, SymExpr(BigInt(1))};
            case SymType::ADD: {
                auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
                std::vector<std::pair<SymExpr, SymExpr>> nds;
                for (auto& arg : add->args) nds.push_back(getFraction(SymExpr(arg)));
                SymExpr den(BigInt(1));
                for (auto& nd : nds) den = simplifyCore(den * nd.second);
                SymExpr num(BigInt(0));
                for (size_t i = 0; i < nds.size(); ++i) {
                    SymExpr termNum = nds[i].first;
                    for (size_t j = 0; j < nds.size(); ++j) {
                        if (i != j) termNum = simplifyCore(expand(termNum * nds[j].second, SymConfig::maxExpandTerms));
                    }
                    num = simplifyCore(expand(num + termNum, SymConfig::maxExpandTerms));
                }
                return {num, den};
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
                SymExpr num(BigInt(1));
                SymExpr den(BigInt(1));
                for (auto& arg : mul->args) {
                    auto nd = getFraction(SymExpr(arg));
                    num = simplifyCore(expand(num * nd.first, SymConfig::maxExpandTerms));
                    den = simplifyCore(den * nd.second);
                }
                return {num, den};
            }
            case SymType::POW: {
                auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt) {
                        auto nd = getFraction(SymExpr(powNode->base));
                        if (n >= 0) {
                            return {simplifyCore(expand(nd.first ^ SymExpr(BigInt(n)), SymConfig::maxExpandTerms)), simplifyCore(nd.second ^ SymExpr(BigInt(n)))};
                        } else {
                            return {simplifyCore(expand(nd.second ^ SymExpr(BigInt(-n)), SymConfig::maxExpandTerms)), simplifyCore(nd.first ^ SymExpr(BigInt(-n)))};
                        }
                    }
                }
                return {expr, SymExpr(BigInt(1))};
            }
            case SymType::FUNC:
                return {expr, SymExpr(BigInt(1))};
        }
        return {expr, SymExpr(BigInt(1))};
    }

    // =================================================================
    // 有理分式化简 (Rational Fraction Simplification)
    // =================================================================
    static SymExpr simplifyRational(const SymExpr& expr) {
        if (!expr.ptr) return expr;
        
        auto [num, den] = getFraction(expr);
        if (den.isOne()) {
            // 如果分母为 1，说明它本身就是多项式，但 getFraction 可能会展开它
            // 为了防止过度展开导致体积膨胀，我们比较一下体积
            if (getAstNodeCount(num) < getAstNodeCount(expr)) return num;
            return expr;
        }

        std::set<std::string> vars;
        collectAllVars(num.ptr, vars);
        collectAllVars(den.ptr, vars);
        
        if (vars.size() == 1) {
            std::string var = *vars.begin();
            SymExpr numExp = simplifyCore(expand(num, SymConfig::maxExpandTerms));
            SymExpr denExp = simplifyCore(expand(den, SymConfig::maxExpandTerms));
            
            if (getDegree(numExp, var) >= 0 && getDegree(denExp, var) >= 0) {
                SymExpr g = polyGCD(numExp, denExp, var);
                if (!g.isOne() && !g.isZero()) {
                    SymExpr newNum = polyDiv(numExp, g, var).first;
                    SymExpr newDen = polyDiv(denExp, g, var).first;
                    
                    SymExpr canceled = simplifyCore(newNum / newDen);
                    if (canceled != expr) {
                        return canceled;
                    }
                }
            }
        }
        
        SymExpr factNum = factorReal(num);
        SymExpr factDen = factorReal(den);
        SymExpr canceled = simplifyCore(factNum / factDen);
        
        if (canceled != expr) {
            return canceled;
        }
        
        return expr;
    }

    // =================================================================
// 智能启发式化简：多重宇宙博弈（含 factor 路线）
// =================================================================
    SymExpr simplify(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        // 第一阶段：递归化简 + 身份吸收（与 simplifyCore 相同）
        SymExpr current = simplifyCore(expr);
        
        // 强制进行一次三角化简，消除反三角嵌套等，将超越函数转化为代数式
        try { current = trigsimp(current); } catch (...) {}

        // 第二阶段：多重宇宙博弈
        SymExpr c_expand = current;
        SymExpr c_contract = current;
        SymExpr c_factor = current;
        SymExpr c_both = current;
        SymExpr c_factor_expand = current;
        SymExpr c_rational = current;

        try { c_expand = expand(current, 30); }
        catch (const std::runtime_error&) {}

        try { c_rational = simplifyRational(current); }
        catch (const std::runtime_error&) {}

        try { c_contract = contract(current); }
        catch (const std::runtime_error&) {}

        try { c_factor = factor(current); }
        catch (const std::runtime_error&) {}

        try {
            if (c_expand.ptr != current.ptr) {
                c_both = contract(c_expand);
                c_factor_expand = factor(c_expand);
            }
        }
        catch (const std::runtime_error&) {}

        // 选出体积最小的宇宙
        SymExpr best = current;
        int minSize = getAstNodeCount(current);

        auto tryCandidate = [&](const SymExpr& cand) {
            int sz = getAstNodeCount(cand);
            if (sz < minSize) {
                minSize = sz;
                best = cand;
            }
            };

        tryCandidate(c_expand);
        tryCandidate(c_contract);
        tryCandidate(c_factor);
        tryCandidate(c_both);
        tryCandidate(c_factor_expand);
        tryCandidate(c_rational);

        return best;
    }

    // =================================================================
    // 🚀 符号方程求解 (Symbolic Equation Solver)
    // 求解 expr == 0 关于 var 的根
    // =================================================================
    std::vector<SymExpr> solveEq(const SymExpr& expr, const std::string& var) {
        SymExpr factored = factor(expr);
        std::vector<SymExpr> roots;

        std::function<void(const SymExpr&)> processFactor = [&](const SymExpr& f) {
            if (!containsVar(f.ptr, var)) return;

            if (f.ptr->getType() == SymType::MUL) {
                for (auto& arg : std::static_pointer_cast<SymMul>(f.ptr)->args) {
                    processFactor(SymExpr(arg));
                }
                return;
            }
            if (f.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(f.ptr);
                if (containsVar(powNode->base, var) && !containsVar(powNode->exp, var)) {
                    processFactor(SymExpr(powNode->base));
                }
                return;
            }

            auto coeffs = extractCoeffs(f, var);
            if (coeffs.empty()) {
                if (containsVar(f.ptr, var)) {
                    throw std::runtime_error("Solver Error: Transcendental or non-polynomial equation is not supported yet.");
                }
                return;
            }

            int degree = static_cast<int>(coeffs.size()) - 1;
            if (degree == 1) {
                // ax + b = 0 => x = -b/a
                SymExpr a = coeffs[1];
                SymExpr b = coeffs[0];
                if (!a.isZero()) {
                    roots.push_back(simplify(-b / a));
                }
            } else if (degree == 2) {
                // ax^2 + bx + c = 0
                SymExpr a = coeffs[2];
                SymExpr b = coeffs[1];
                SymExpr c = coeffs[0];
                if (!a.isZero()) {
                    SymExpr delta = simplify(b * b - SymExpr(BigInt(4)) * a * c);
                    SymExpr twoA = SymExpr(BigInt(2)) * a;
                    auto [ok, sqrtDelta] = trySquareRoot(delta, true);
                    if (!ok) {
                        bool isNeg = false;
                        if (delta.ptr->getType() == SymType::NUM) {
                            isNeg = isCasNegative(std::static_pointer_cast<SymNum>(delta.ptr)->value);
                        } else if (delta.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(delta.ptr);
                            if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                            }
                        }
                        if (isNeg) {
                            SymExpr I = SymExpr::makeVar("i");
                            auto [ok2, sqrtPosDelta] = trySquareRoot(-delta, true);
                            if (ok2) {
                                sqrtDelta = simplify(I * sqrtPosDelta);
                            } else {
                                sqrtDelta = simplify(I * ((-delta) ^ SymExpr(Fraction(1, 2))));
                            }
                        } else {
                            sqrtDelta = delta ^ SymExpr(Fraction(1, 2));
                        }
                    }
                    roots.push_back(simplify((-b + sqrtDelta) / twoA));
                    roots.push_back(simplify((-b - sqrtDelta) / twoA));
                }
            } else if (degree == 3) {
                SymExpr a = coeffs[3];
                SymExpr b = coeffs[2];
                SymExpr c = coeffs[1];
                SymExpr d = coeffs[0];
                if (!a.isZero()) {
                    SymExpr p = simplify((SymExpr(BigInt(3)) * a * c - b * b) / (SymExpr(BigInt(3)) * a * a));
                    SymExpr q = simplify((SymExpr(BigInt(2)) * (b ^ SymExpr(BigInt(3))) - SymExpr(BigInt(9)) * a * b * c + SymExpr(BigInt(27)) * a * a * d) / (SymExpr(BigInt(27)) * (a ^ SymExpr(BigInt(3)))));
                    SymExpr delta = simplify((q * q) / SymExpr(BigInt(4)) + (p ^ SymExpr(BigInt(3))) / SymExpr(BigInt(27)));
                    
                    SymExpr sqrtDelta = simplify(delta ^ SymExpr(Fraction(1, 2)));
                    SymExpr u = simplify((-q / SymExpr(BigInt(2)) + sqrtDelta) ^ SymExpr(Fraction(1, 3)));
                    SymExpr v = simplify((-q / SymExpr(BigInt(2)) - sqrtDelta) ^ SymExpr(Fraction(1, 3)));
                    
                    SymExpr I = SymExpr::makeVar("i");
                    SymExpr sqrt3 = SymExpr(BigInt(3)) ^ SymExpr(Fraction(1, 2));
                    SymExpr omega = simplify((SymExpr(BigInt(-1)) + I * sqrt3) / SymExpr(BigInt(2)));
                    SymExpr omega2 = simplify((SymExpr(BigInt(-1)) - I * sqrt3) / SymExpr(BigInt(2)));
                    
                    SymExpr shift = simplify(b / (SymExpr(BigInt(3)) * a));
                    
                    roots.push_back(simplify(u + v - shift));
                    roots.push_back(simplify(omega * u + omega2 * v - shift));
                    roots.push_back(simplify(omega2 * u + omega * v - shift));
                }
            } else if (degree == 4) {
                SymExpr a = coeffs[4];
                SymExpr b = coeffs[3];
                SymExpr c = coeffs[2];
                SymExpr d = coeffs[1];
                SymExpr e = coeffs[0];
                if (!a.isZero()) {
                    SymExpr p = simplify((SymExpr(BigInt(8)) * a * c - SymExpr(BigInt(3)) * (b ^ SymExpr(BigInt(2)))) / (SymExpr(BigInt(8)) * a * a));
                    SymExpr q = simplify(((b ^ SymExpr(BigInt(3))) - SymExpr(BigInt(4)) * a * b * c + SymExpr(BigInt(8)) * a * a * d) / (SymExpr(BigInt(8)) * (a ^ SymExpr(BigInt(3)))));
                    SymExpr r = simplify((SymExpr(BigInt(-3)) * (b ^ SymExpr(BigInt(4))) + SymExpr(BigInt(256)) * (a ^ SymExpr(BigInt(3))) * e - SymExpr(BigInt(64)) * a * a * b * d + SymExpr(BigInt(16)) * a * b * b * c) / (SymExpr(BigInt(256)) * (a ^ SymExpr(BigInt(4)))));
                    
                    SymExpr shift = simplify(b / (SymExpr(BigInt(4)) * a));
                    
                    if (q.isZero()) {
                        SymExpr delta2 = simplify(p * p - SymExpr(BigInt(4)) * r);
                        SymExpr sqrtDelta2 = simplify(delta2 ^ SymExpr(Fraction(1, 2)));
                        SymExpr t2_1 = simplify((-p + sqrtDelta2) / SymExpr(BigInt(2)));
                        SymExpr t2_2 = simplify((-p - sqrtDelta2) / SymExpr(BigInt(2)));
                        
                        SymExpr t1 = simplify(t2_1 ^ SymExpr(Fraction(1, 2)));
                        SymExpr t2 = simplify(-t1);
                        SymExpr t3 = simplify(t2_2 ^ SymExpr(Fraction(1, 2)));
                        SymExpr t4 = simplify(-t3);
                        
                        roots.push_back(simplify(t1 - shift));
                        roots.push_back(simplify(t2 - shift));
                        roots.push_back(simplify(t3 - shift));
                        roots.push_back(simplify(t4 - shift));
                    } else {
                        SymExpr A3 = SymExpr(BigInt(1));
                        SymExpr B3 = SymExpr(BigInt(2)) * p;
                        SymExpr C3 = p * p - SymExpr(BigInt(4)) * r;
                        SymExpr D3 = -(q * q);
                        
                        SymExpr p3 = simplify((SymExpr(BigInt(3)) * A3 * C3 - B3 * B3) / (SymExpr(BigInt(3)) * A3 * A3));
                        SymExpr q3 = simplify((SymExpr(BigInt(2)) * (B3 ^ SymExpr(BigInt(3))) - SymExpr(BigInt(9)) * A3 * B3 * C3 + SymExpr(BigInt(27)) * A3 * A3 * D3) / (SymExpr(BigInt(27)) * (A3 ^ SymExpr(BigInt(3)))));
                        SymExpr delta3 = simplify((q3 * q3) / SymExpr(BigInt(4)) + (p3 ^ SymExpr(BigInt(3))) / SymExpr(BigInt(27)));
                        
                        SymExpr sqrtDelta3 = simplify(delta3 ^ SymExpr(Fraction(1, 2)));
                        SymExpr u3 = simplify((-q3 / SymExpr(BigInt(2)) + sqrtDelta3) ^ SymExpr(Fraction(1, 3)));
                        SymExpr v3 = simplify((-q3 / SymExpr(BigInt(2)) - sqrtDelta3) ^ SymExpr(Fraction(1, 3)));
                        
                        SymExpr y1 = simplify(u3 + v3 - B3 / (SymExpr(BigInt(3)) * A3));
                        SymExpr sqrtY1 = simplify(y1 ^ SymExpr(Fraction(1, 2)));
                        
                        SymExpr C1 = simplify((p + y1 - q / sqrtY1) / SymExpr(BigInt(2)));
                        SymExpr C2 = simplify((p + y1 + q / sqrtY1) / SymExpr(BigInt(2)));
                        
                        SymExpr delta1 = simplify(y1 - SymExpr(BigInt(4)) * C1);
                        SymExpr delta2 = simplify(y1 - SymExpr(BigInt(4)) * C2);
                        
                        SymExpr sqrtDelta1 = simplify(delta1 ^ SymExpr(Fraction(1, 2)));
                        SymExpr sqrtDelta2 = simplify(delta2 ^ SymExpr(Fraction(1, 2)));
                        
                        roots.push_back(simplify((-sqrtY1 + sqrtDelta1) / SymExpr(BigInt(2)) - shift));
                        roots.push_back(simplify((-sqrtY1 - sqrtDelta1) / SymExpr(BigInt(2)) - shift));
                        roots.push_back(simplify((sqrtY1 + sqrtDelta2) / SymExpr(BigInt(2)) - shift));
                        roots.push_back(simplify((sqrtY1 - sqrtDelta2) / SymExpr(BigInt(2)) - shift));
                    }
                }
            } else if (degree > 4) {
                // 尝试求解高次二项式方程 ax^n + b = 0 (返回所有复数根)
                bool isBinomial = true;
                for (int i = 1; i < degree; ++i) {
                    if (!coeffs[i].isZero()) {
                        isBinomial = false;
                        break;
                    }
                }
                if (isBinomial) {
                    SymExpr a = coeffs[degree];
                    SymExpr b = coeffs[0];
                    if (!a.isZero()) {
                        SymExpr rhs = simplify(-b / a);
                        SymExpr principal = simplify(rhs ^ SymExpr(Fraction(1, degree)));
                        for (int k = 0; k < degree; ++k) {
                            if (k == 0) {
                                roots.push_back(principal);
                            } else {
                                SymExpr E = SymExpr::makeVar("E");
                                SymExpr PI = SymExpr::makeVar("PI");
                                SymExpr I = SymExpr::makeVar("i");
                                SymExpr exponent = SymExpr(Fraction(BigInt(2 * k), BigInt(degree))) * PI * I;
                                SymExpr unity = E ^ exponent;
                                roots.push_back(simplify(principal * unity));
                            }
                        }
                    }
                }
            }
        };

        processFactor(factored);

        // 去重
        std::vector<SymExpr> uniqueRoots;
        for (const auto& r : roots) {
            bool found = false;
            for (const auto& ur : uniqueRoots) {
                if (r == ur) {
                    found = true;
                    break;
                }
            }
            if (!found) uniqueRoots.push_back(r);
        }

        return uniqueRoots;
    }

    // =================================================================
    // 静默代入 (Quiet Substitution) - 避免除零异常的控制流开销
    // =================================================================
    static std::optional<SymExpr> trySubsQuiet(const SymExpr& expr, const std::string& var, const SymExpr& val) {
        if (!expr.ptr) return expr;

        switch (expr.ptr->getType()) {
        case SymType::NUM:
            return expr;

        case SymType::VAR: {
            auto v = std::static_pointer_cast<SymVar>(expr.ptr);
            return (v->name == var) ? val : expr;
        }

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(BigInt(0));
            for (auto& arg : add->args) {
                auto subArg = trySubsQuiet(SymExpr(arg), var, val);
                if (!subArg) return std::nullopt;
                result = result + *subArg;
            }
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            for (auto& arg : mul->args) {
                auto subArg = trySubsQuiet(SymExpr(arg), var, val);
                if (!subArg) return std::nullopt;
                result = result * *subArg;
            }
            return result;
        }

        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            auto baseSub = trySubsQuiet(SymExpr(pow->base), var, val);
            if (!baseSub) return std::nullopt;
            auto expSub = trySubsQuiet(SymExpr(pow->exp), var, val);
            if (!expSub) return std::nullopt;

            auto [bOk, bVal] = tryEvalConst(*baseSub);
            auto [eOk, eVal] = tryEvalConst(*expSub);
            bool bIsZero = baseSub->isZero() || (bOk && !bVal.truthy());
            bool eIsZero = expSub->isZero() || (eOk && !eVal.truthy());

            if (bIsZero) {
                if (eIsZero) return std::nullopt; // 0^0
                bool eIsNeg = false;
                if (eOk) {
                    try { eIsNeg = eVal.asDouble() < 0.0; } catch(...) {}
                } else if (expSub->ptr->getType() == SymType::NUM) {
                    eIsNeg = isCasNegative(std::static_pointer_cast<SymNum>(expSub->ptr)->value);
                }
                if (eIsNeg) return std::nullopt; // Division by zero
            }
            
            try {
                return (*baseSub) ^ (*expSub);
            } catch (...) {
                return std::nullopt;
            }
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> newArgs;
            for (auto& arg : func->args) {
                auto subArg = trySubsQuiet(SymExpr(arg), var, val);
                if (!subArg) return std::nullopt;
                newArgs.push_back(subArg->ptr);
            }
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
        }
        }
        return expr;
    }

    // =================================================================
    // 🚀 泰勒展开 (Taylor Series)
    // =================================================================
    SymExpr taylor(const SymExpr& expr, const std::string& var, const SymExpr& a, int order) {
        if (order < 0) throw std::runtime_error("Math Error: Taylor expansion order must be non-negative.");
        if (!expr.ptr) return SymExpr(BigInt(0));
        SymExpr result(BigInt(0));
        SymExpr current_deriv = expr;
        BigInt fact(1);
        SymExpr term_base = SymExpr::makeVar(var) - a;

        for (int n = 0; n <= order; ++n) {
            if (n > 0) {
                current_deriv = diff(current_deriv, var);
                fact = fact * BigInt(n);
            }
            SymExpr coeff;
            if (auto subbed = trySubsQuiet(current_deriv, var, a)) {
                try {
                    coeff = simplify(*subbed);
                } catch (...) {
                    throw std::runtime_error("Math Error: Cannot compute Taylor expansion (derivative undefined at expansion point).");
                }
            } else {
                throw std::runtime_error("Math Error: Cannot compute Taylor expansion (derivative undefined at expansion point).");
            }
            
            if (!coeff.isZero()) {
                SymExpr term = coeff / SymExpr(fact);
                if (n > 0) {
                    term = term * (term_base ^ SymExpr(BigInt(n)));
                }
                result = result + term;
            }
        }
        return simplify(result);
    }

    // =================================================================
    // 🚀 极限计算 (Limit) - 洛必达法则与泰勒截断
    // =================================================================
    static SymExpr limitCore(const SymExpr& expr, const std::string& var, const SymExpr& val, int depth) {
        if (depth > SymConfig::maxDepth) throw std::runtime_error("Calculus Error: Limit evaluation depth exceeded.");
        if (!expr.ptr) return expr;

        // 尝试直接代入 (静默模式，不抛出除零异常)
        if (auto subbed = trySubsQuiet(expr, var, val)) {
            try {
                SymExpr simp = simplify(*subbed);
                // 如果结果中不再包含该变量，说明代入成功
                if (!containsVar(simp.ptr, var)) return simp;
            } catch (const std::runtime_error& e) {
                std::string msg = e.what();
                if (msg.find("Division by zero") == std::string::npos && 
                    msg.find("0^0") == std::string::npos) {
                    throw;
                }
            }
        }

        // 提取分子和分母
        auto [num, den] = getFraction(expr);

        if (den.isOne()) {
            // 不是分式，但代入失败，可能含有复杂奇点，直接返回化简后的代入形式
            if (auto subbed = trySubsQuiet(expr, var, val)) {
                try { return simplify(*subbed); } catch (...) { return *subbed; }
            }
            return expr;
        }

        // 检查是否为 0/0 型
        bool numZero = false, denZero = false;
        if (auto subNum = trySubsQuiet(num, var, val)) {
            try { numZero = simplify(*subNum).isZero(); } catch (...) {}
        }
        if (auto subDen = trySubsQuiet(den, var, val)) {
            try { denZero = simplify(*subDen).isZero(); } catch (...) {}
        }

        if (numZero && denZero) {
            if (depth > 3) {
                // 泰勒展开截断求极限 (Taylor Series Truncation)
                // 寻找分子分母的最低非零导数 (即泰勒展开的首个非零项)
                SymExpr dNum = num, dDen = den;
                SymExpr coeffNum(BigInt(0)), coeffDen(BigInt(0));
                int orderNum = -1, orderDen = -1;
                
                for (int i = 0; i <= 10; ++i) {
                    if (auto subNum = trySubsQuiet(dNum, var, val)) {
                        try { coeffNum = simplify(*subNum); } catch (...) {}
                    }
                    if (!coeffNum.isZero()) { orderNum = i; break; }
                    dNum = diff(dNum, var);
                }
                for (int i = 0; i <= 10; ++i) {
                    if (auto subDen = trySubsQuiet(dDen, var, val)) {
                        try { coeffDen = simplify(*subDen); } catch (...) {}
                    }
                    if (!coeffDen.isZero()) { orderDen = i; break; }
                    dDen = diff(dDen, var);
                }
                
                if (orderNum != -1 && orderDen != -1) {
                    if (orderNum > orderDen) return SymExpr(BigInt(0));
                    if (orderNum < orderDen) throw std::runtime_error("Math Error: Limit is infinite (pole).");
                    // 阶数相同时，阶乘 n! 会被约掉，极限即为导数值之比
                    return simplify(coeffNum / coeffDen);
                }
            }

            // 洛必达法则：lim (f/g) = lim (f'/g')
            SymExpr dNum = diff(num, var);
            SymExpr dDen = diff(den, var);
            SymExpr lhopitalExpr = simplify(dNum / dDen);
            return limitCore(lhopitalExpr, var, val, depth + 1);
        }

        // 如果分母为0但分子不为0，则是无穷大（这里简化抛出异常或保留符号）
        if (denZero && !numZero) {
            throw std::runtime_error("Math Error: Limit is infinite (division by zero).");
        }

        if (auto subbed = trySubsQuiet(expr, var, val)) {
            try { return simplify(*subbed); } catch (...) { return *subbed; }
        }
        return expr;
    }

    SymExpr limit(const SymExpr& expr, const std::string& var, const SymExpr& val) {
        return limitCore(expr, var, val, 0);
    }


    // =================================================================
    // 🚀 三角化简 (Trigonometric Simplification)
    // =================================================================
    SymExpr trigsimp(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        // 从规则库获取三角化简规则
        const std::vector<std::pair<SymExpr, SymExpr>>& rules = getTrigRules();

        SymExpr current = expr;
        bool changed = true;
        
        // 循环尝试应用规则，直到表达式不再发生变化
        int iter = 0;
        while (changed && iter++ < SymConfig::maxIterations) {
            changed = false;
            for (const auto& rule : rules) {
                SymExpr next = applyRule(current, rule.first, rule.second);
                if (next.ptr != current.ptr) {
                    SymExpr simplifiedNext = simplifyCore(next);
                    if (simplifiedNext != current) {
                        current = simplifiedNext;
                        changed = true;
                        break;
                    }
                }
            }
        }

        return current;
    }

    // =================================================================
    // 🚀 刘维尔域规范化 (Liouvillian Field Normalization)
    // =================================================================
    static Fraction gcdFraction(const Fraction& f1, const Fraction& f2) {
        if (f1.getNum().isZero()) return f2;
        if (f2.getNum().isZero()) return f1;
        BigInt num = BigInt::gcd(f1.getNum(), f2.getNum());
        BigInt den = BigInt::lcm(f1.getDen(), f2.getDen());
        return Fraction(num, den);
    }

    static std::pair<Fraction, SymExpr> extractRationalCoeff(const SymExpr& expr) {
        if (expr.ptr->getType() == SymType::NUM) {
            auto num = std::static_pointer_cast<SymNum>(expr.ptr);
            if (std::holds_alternative<BigInt>(num->value)) {
                return {Fraction(std::get<BigInt>(num->value), BigInt(1)), SymExpr(BigInt(1))};
            } else if (std::holds_alternative<Fraction>(num->value)) {
                return {std::get<Fraction>(num->value), SymExpr(BigInt(1))};
            }
        } else if (expr.ptr->getType() == SymType::MUL) {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            BigInt num_val(1);
            BigInt den_val(1);
            std::vector<std::shared_ptr<SymNode>> rest;
            for (auto& arg : mul->args) {
                if (arg->getType() == SymType::NUM) {
                    auto num = std::static_pointer_cast<SymNum>(arg);
                    if (std::holds_alternative<BigInt>(num->value)) {
                        num_val = num_val * std::get<BigInt>(num->value);
                    } else if (std::holds_alternative<Fraction>(num->value)) {
                        Fraction f = std::get<Fraction>(num->value);
                        num_val = num_val * f.getNum();
                        den_val = den_val * f.getDen();
                    } else {
                        rest.push_back(arg);
                    }
                } else {
                    rest.push_back(arg);
                }
            }
            Fraction q(num_val, den_val);
            if (rest.empty()) return {q, SymExpr(BigInt(1))};
            if (rest.size() == 1) return {q, SymExpr(rest[0])};
            return {q, SymExpr(std::make_shared<SymMul>(rest))};
        }
        return {Fraction(BigInt(1), BigInt(1)), expr};
    }

    static SymExpr flattenLogExp(const SymExpr& expr) {
        if (!expr.ptr) return expr;
        switch (expr.ptr->getType()) {
            case SymType::ADD: {
                SymExpr res(BigInt(0));
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args)
                    res = res + flattenLogExp(SymExpr(arg));
                return res;
            }
            case SymType::MUL: {
                SymExpr res(BigInt(1));
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args)
                    res = res * flattenLogExp(SymExpr(arg));
                return res;
            }
            case SymType::POW: {
                auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
                return flattenLogExp(SymExpr(powNode->base)) ^ flattenLogExp(SymExpr(powNode->exp));
            }
            case SymType::FUNC: {
                auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
                if (func->name == "log" && func->args.size() == 1) {
                    SymExpr inner = flattenLogExp(SymExpr(func->args[0]));
                    if (inner.ptr->getType() == SymType::MUL) {
                        SymExpr res(BigInt(0));
                        for (auto& arg : std::static_pointer_cast<SymMul>(inner.ptr)->args) {
                            res = res + flattenLogExp(SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{arg})));
                        }
                        return res;
                    }
                    if (inner.ptr->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(inner.ptr);
                        SymExpr baseLog = flattenLogExp(SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{powNode->base})));
                        return SymExpr(powNode->exp) * baseLog;
                    }
                    return SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{inner.ptr}));
                }
                if (func->name == "exp" && func->args.size() == 1) {
                    SymExpr inner = flattenLogExp(SymExpr(func->args[0]));
                    if (inner.ptr->getType() == SymType::ADD) {
                        SymExpr res(BigInt(1));
                        for (auto& arg : std::static_pointer_cast<SymAdd>(inner.ptr)->args) {
                            res = res * flattenLogExp(SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{arg})));
                        }
                        return res;
                    }
                    return SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{inner.ptr}));
                }
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : func->args) newArgs.push_back(flattenLogExp(SymExpr(arg)).ptr);
                return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
            }
            default: return expr;
        }
    }

    SymExpr rischNormalize(const SymExpr& expr) {
        SymExpr flat = flattenLogExp(expand(expr, SymConfig::maxExpandTerms));
        
        std::vector<SymExpr> allExps;
        std::function<void(const SymExpr&)> collectExps = [&](const SymExpr& e) {
            if (!e.ptr) return;
            if (e.ptr->getType() == SymType::FUNC) {
                auto func = std::static_pointer_cast<SymFunc>(e.ptr);
                if (func->name == "exp" && func->args.size() == 1) {
                    allExps.push_back(e);
                }
                for (auto& arg : func->args) collectExps(SymExpr(arg));
            } else if (e.ptr->getType() == SymType::ADD) {
                for (auto& arg : std::static_pointer_cast<SymAdd>(e.ptr)->args) collectExps(SymExpr(arg));
            } else if (e.ptr->getType() == SymType::MUL) {
                for (auto& arg : std::static_pointer_cast<SymMul>(e.ptr)->args) collectExps(SymExpr(arg));
            } else if (e.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(e.ptr);
                collectExps(SymExpr(powNode->base));
                collectExps(SymExpr(powNode->exp));
            }
        };
        collectExps(flat);

        std::map<std::string, std::pair<SymExpr, Fraction>> expGroups;
        for (const auto& expNode : allExps) {
            auto func = std::static_pointer_cast<SymFunc>(expNode.ptr);
            SymExpr inner(func->args[0]);
            auto [q, prim] = extractRationalCoeff(inner);
            std::string sig = prim.ptr->getSignature();
            if (expGroups.count(sig)) {
                expGroups[sig].second = gcdFraction(expGroups[sig].second, q);
            } else {
                expGroups[sig] = {prim, q};
            }
        }

        std::function<SymExpr(const SymExpr&)> replaceExps = [&](const SymExpr& e) -> SymExpr {
            if (!e.ptr) return e;
            if (e.ptr->getType() == SymType::FUNC) {
                auto func = std::static_pointer_cast<SymFunc>(e.ptr);
                if (func->name == "exp" && func->args.size() == 1) {
                    SymExpr inner = replaceExps(SymExpr(func->args[0]));
                    auto [q, prim] = extractRationalCoeff(inner);
                    std::string sig = prim.ptr->getSignature();
                    if (expGroups.count(sig)) {
                        Fraction g = expGroups[sig].second;
                        if (g.getNum() > BigInt(0)) {
                            Fraction power(q.getNum() * g.getDen(), q.getDen() * g.getNum());
                            SymExpr baseExp(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(SymExpr(g) * prim).ptr}));
                            if (power.getNum() == power.getDen()) return baseExp;
                            return baseExp ^ SymExpr(power);
                        }
                    }
                    return SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{inner.ptr}));
                }
                std::vector<std::shared_ptr<SymNode>> newArgs;
                for (auto& arg : func->args) newArgs.push_back(replaceExps(SymExpr(arg)).ptr);
                return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
            } else if (e.ptr->getType() == SymType::ADD) {
                SymExpr res(BigInt(0));
                for (auto& arg : std::static_pointer_cast<SymAdd>(e.ptr)->args) res = res + replaceExps(SymExpr(arg));
                return res;
            } else if (e.ptr->getType() == SymType::MUL) {
                SymExpr res(BigInt(1));
                for (auto& arg : std::static_pointer_cast<SymMul>(e.ptr)->args) res = res * replaceExps(SymExpr(arg));
                return res;
            } else if (e.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(e.ptr);
                return replaceExps(SymExpr(powNode->base)) ^ replaceExps(SymExpr(powNode->exp));
            }
            return e;
        };

        return replaceExps(flat);
    }

    // =================================================================
// 快速数值求值 (C++ 原生 Double 极限狂飙 + 依赖注入解耦)
// =================================================================
    double fastEval(const std::shared_ptr<SymNode>& node, const std::map<std::string, double>& env, const SymbolicFuncResolver& resolver) {
        if (!node) return 0.0;

        switch (node->getType()) {
        case SymType::NUM: {
            auto num = std::static_pointer_cast<SymNum>(node);
            return casValToValue(num->value).asDouble();
        }
        case SymType::VAR: {
            auto varName = std::static_pointer_cast<SymVar>(node)->name;
            auto it = env.find(varName);
            if (it != env.end()) return it->second;
            if (varName == "PI") return 3.14159265358979323846;
            if (varName == "E") return 2.71828182845904523536;
            return 0.0;
        }
        case SymType::ADD: {
            double sum = 0.0;
            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) sum += fastEval(arg, env, resolver);
            return sum;
        }
        case SymType::MUL: {
            double prod = 1.0;
            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) prod *= fastEval(arg, env, resolver);
            return prod;
        }
        case SymType::POW: {
            auto p = std::static_pointer_cast<SymPow>(node);
            return std::pow(fastEval(p->base, env, resolver), fastEval(p->exp, env, resolver));
        }
        case SymType::FUNC: {
            auto f = std::static_pointer_cast<SymFunc>(node);
            if (!resolver) throw std::runtime_error("JIT Error: No function resolver provided for '" + f->name + "'.");

            // 将双精度打包丢给 VM 宿主的注册表处理
            std::vector<Value> callArgs;
            callArgs.reserve(f->args.size());
            for (auto& arg : f->args) {
                callArgs.push_back(Value(fastEval(arg, env, resolver)));
            }
            // 拿到宿主的 Value 后光速拆包回 double！
            return resolver(f->name, callArgs).asDouble();
        }
        }
        return 0.0;
    }

    // =================================================================
// 万能多态求值 (高维张量/复平面支援 + 依赖注入解耦)
// =================================================================
    Value evalUniversal(const std::shared_ptr<SymNode>& node, const std::map<std::string, Value>& env, const SymbolicFuncResolver& resolver) {
        if (!node) return Value(0.0);

        switch (node->getType()) {
        case SymType::NUM: {
            auto num = std::static_pointer_cast<SymNum>(node);
            return casValToValue(num->value);
        }
        case SymType::VAR: {
            auto varName = std::static_pointer_cast<SymVar>(node)->name;
            auto it = env.find(varName);
            if (it != env.end()) return it->second;
            if (varName == "PI") return Value(3.14159265358979323846);
            if (varName == "E") return Value(2.71828182845904523536);
            if (varName == "i" || varName == "I") return Value(Complex(0.0, 1.0));
            return Value(0.0);
        }
        case SymType::ADD: {
            Value sum(0.0);
            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) sum = sum + evalUniversal(arg, env, resolver);
            return sum;
        }
        case SymType::MUL: {
            Value prod(1.0);
            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) prod = prod * evalUniversal(arg, env, resolver);
            return prod;
        }
        case SymType::POW: {
            auto p = std::static_pointer_cast<SymPow>(node);
            return evalUniversal(p->base, env, resolver) ^ evalUniversal(p->exp, env, resolver);
        }
        case SymType::FUNC: {
            auto f = std::static_pointer_cast<SymFunc>(node);
            if (!resolver) throw std::runtime_error("Universal Error: No function resolver provided for '" + f->name + "'.");

            // 同构打包发送给宿主环境处理
            std::vector<Value> callArgs;
            callArgs.reserve(f->args.size());
            for (auto& arg : f->args) {
                callArgs.push_back(evalUniversal(arg, env, resolver));
            }
            return resolver(f->name, callArgs);
        }
        }
        return Value(0.0);
    }
} // namespace jc
