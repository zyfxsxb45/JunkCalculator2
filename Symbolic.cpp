// Symbolic.cpp
#include "Symbolic.h"
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

namespace jc {

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
    static bool isCasNegative(const CASVal& v) {
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
    std::string SymAdd::toString() const {
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
    std::string SymMul::toString() const {
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

    std::string SymPow::toString() const {
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

    std::string SymFunc::toString() const {
        std::string res = name + "(";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) res += ", ";
            res += args[i]->toString();
        }
        return res + ")";
    }

    // =================================================================
// 整数类型的终极脱壳器：
// 无论它是 BigInt, Fraction(分母为1), double(形如2.0), Complex(形如2+0i)
// 只要它数学上是个精确整数，统统榨出其 int64_t 的灵魂！
// =================================================================
    static std::pair<bool, int64_t> extractExactInt(const CASVal& cval) {
        return std::visit([](auto&& arg) -> std::pair<bool, int64_t> {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, BigInt>) {
                // 仅支持 64 位整数范围的大整数，避免强制转换溢出
                if (arg >= BigInt(std::numeric_limits<int64_t>::min()) &&
                    arg <= BigInt(std::numeric_limits<int64_t>::max())) {
                    return { true, static_cast<int64_t>(arg.toDouble()) };
                }
            }
            else if constexpr (std::is_same_v<T, Fraction>) {
                if (arg.getDen() == BigInt(1)) {
                    BigInt num = arg.getNum();
                    if (num >= BigInt(std::numeric_limits<int64_t>::min()) &&
                        num <= BigInt(std::numeric_limits<int64_t>::max())) {
                        return { true, static_cast<int64_t>(num.toDouble()) };
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
    // SymExpr 构造
    // ==========================================
    SymExpr::SymExpr() : ptr(std::make_shared<SymNum>(BigInt(0))) {}
    SymExpr::SymExpr(double v) : ptr(std::make_shared<SymNum>(v)) {}
    SymExpr::SymExpr(const BigInt& v) : ptr(std::make_shared<SymNum>(v)) {}
    SymExpr::SymExpr(const Fraction& v) : ptr(std::make_shared<SymNum>(v)) {}
    SymExpr::SymExpr(const Complex& v) {
        if (Tol::isEq(v.imag, 0.0)) {
            ptr = std::make_shared<SymNum>(v.real);
        } else if (Tol::isEq(v.real, 0.0)) {
            SymExpr imagPart(v.imag);
            SymExpr iVar = SymExpr::makeVar("i");
            ptr = (imagPart * iVar).ptr;
        } else {
            SymExpr realPart(v.real);
            SymExpr imagPart(v.imag);
            SymExpr iVar = SymExpr::makeVar("i");
            ptr = (realPart + imagPart * iVar).ptr;
        }
    }
    SymExpr::SymExpr(const CASVal& v) : ptr(std::make_shared<SymNum>(v)) {}

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
                    std::string key = rem.toString();
                    if (symTerms.count(key))
                        symTerms[key].coeff = casAdd(symTerms[key].coeff, coeff);
                    else
                        symTerms[key] = { coeff, rem.ptr };
                }
            }
            else {
                std::string key = node->toString();
                if (symTerms.count(key))
                    symTerms[key].coeff = casAdd(symTerms[key].coeff, BigInt(1));
                else
                    symTerms[key] = { BigInt(1), node };
            }
        }
        // ★ 纯净输出：不做任何负号提取，直接组装 ADD 节点
        std::vector<std::shared_ptr<SymNode>> newArgs;
        if (!isCasZero(sumConst))
            newArgs.push_back(std::make_shared<SymNum>(sumConst));
        for (auto& [key, data] : symTerms) {
            if (isCasZero(data.coeff)) continue;
            if (isCasOne(data.coeff)) {
                newArgs.push_back(data.baseNode);
            }
            else {
                std::vector<std::shared_ptr<SymNode>> mArgs;
                mArgs.push_back(std::make_shared<SymNum>(data.coeff));
                if (data.baseNode->getType() == SymType::MUL) {
                    auto inner = std::static_pointer_cast<SymMul>(data.baseNode);
                    mArgs.insert(mArgs.end(), inner->args.begin(), inner->args.end());
                }
                else {
                    mArgs.push_back(data.baseNode);
                }
                newArgs.push_back(std::make_shared<SymMul>(mArgs));
            }
        }
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
            std::string key = base->toString();
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
                    std::string key = node->toString();
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

        std::vector<std::shared_ptr<SymNode>> newArgs;
        for (auto& [key, data] : symFactors) {
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
            newArgs.insert(newArgs.begin(), std::make_shared<SymNum>(prodConst));
        }
        if (newArgs.size() == 1) return SymExpr(newArgs[0]);
        return SymExpr(std::make_shared<SymMul>(std::move(newArgs)));
    }

    // ==========================================
    // 单目与二元减法
    // ==========================================
    SymExpr SymExpr::operator-() const { return (*this) * SymExpr(-1); }
    SymExpr operator-(const SymExpr& a, const SymExpr& b) { return a + (-b); }

    // ==========================================
    // operator/（除法 → 委托 Value 求倒数）
    // ==========================================
    SymExpr operator/(const SymExpr& a, const SymExpr& b) {
        if (!a.ptr) return SymExpr(BigInt(0));
        if (!b.ptr || b.isZero()) throw std::runtime_error("CAS Error: Division by zero.");

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
        if (a.isZero()) {
            if (b.isZero()) throw std::runtime_error("CAS Error: 0^0 is undefined.");
            if (b.ptr->getType() == SymType::NUM && isCasNegative(std::static_pointer_cast<SymNum>(b.ptr)->value)) {
                throw std::runtime_error("CAS Error: Division by zero.");
            }
            return SymExpr(0);
        }
        if (a.isOne() || b.isZero()) return SymExpr(BigInt(1));
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
            } else {
                if (isCasNegative(baseNum->value)) {
                    if (std::holds_alternative<Fraction>(expNum->value)) {
                        Fraction expF = std::get<Fraction>(expNum->value);
                        if (expF.getNum() == BigInt(1) || expF.getNum() == BigInt(-1)) {
                            int64_t den = 0;
                            try { den = static_cast<int64_t>(expF.getDen().toDouble()); } catch(...) {}
                            if (den > 0) {
                                if (den % 2 != 0) {
                                    // 奇数次根：(-A)^(1/n) = - (A^(1/n))
                                    CASVal posBase = casMul(baseNum->value, BigInt(-1));
                                    return -(SymExpr(posBase) ^ b);
                                } else if (den == 2 && expF.getNum() == BigInt(1)) {
                                    // 平方根：(-A)^(1/2) = i * A^(1/2)
                                    CASVal posBase = casMul(baseNum->value, BigInt(-1));
                                    return SymExpr::makeVar("i") * (SymExpr(posBase) ^ b);
                                }
                            }
                        }
                    }
                } else if (std::holds_alternative<Fraction>(baseNum->value)) {
                    Fraction f = std::get<Fraction>(baseNum->value);
                    if (f.getNum() > BigInt(0)) {
                        if (isCasNegative(expNum->value)) {
                            // 负分数指数: 仅当分母 > 1 时翻转 (避免 2^(-3/2) 变成 (1/2)^(3/2))
                            if (f.getDen() > BigInt(1)) {
                                Fraction invF(f.getDen(), f.getNum());
                                CASVal posExp = casMul(expNum->value, BigInt(-1));
                                return SymExpr(invF) ^ SymExpr(posExp);
                            }
                        } else {
                            // 正分数指数: 如果是 1/n，翻转为 n^(-c)
                            if (f.getNum() == BigInt(1) && f.getDen() > BigInt(1)) {
                                Fraction invF(f.getDen(), f.getNum());
                                CASVal negExp = casMul(expNum->value, BigInt(-1));
                                return SymExpr(invF) ^ SymExpr(negExp);
                            }
                        }
                    }
                }
            }
            // 非整数指数保留符号形式（由 Value 的升维机制自动保障）
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
    static int countNodes(const std::shared_ptr<SymNode>& node) {
        if (!node) return 0;
        int count = 1;
        switch (node->getType()) {
        case SymType::ADD:
            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args)
                count += countNodes(arg);
            break;
        case SymType::MUL:
            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args)
                count += countNodes(arg);
            break;
        case SymType::POW:
            count += countNodes(std::static_pointer_cast<SymPow>(node)->base);
            count += countNodes(std::static_pointer_cast<SymPow>(node)->exp);
            break;
        case SymType::FUNC:
            for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args)
                count += countNodes(arg);
            break;
        default: break;
        }
        return count;
    }

    int getAstNodeCount(const SymExpr& expr) {
        return countNodes(expr.ptr);
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

    // 精确匹配 AST 并记录捕获变量 (支持 ADD/MUL 的全排列交换律检测)
    static bool matchAST(const std::shared_ptr<SymNode>& node, const std::shared_ptr<SymNode>& pat, std::map<std::string, SymExpr>& captures) {
        if (!node || !pat) return false;
        
        std::string wcName;
        // 1. 若 pat 是通配符
        if (isWildcard(pat, wcName)) {
            auto it = captures.find(wcName);
            if (it != captures.end()) {
                return it->second.toString() == node->toString();
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
                
                std::vector<size_t> indices(nArgs.size());
                std::iota(indices.begin(), indices.end(), 0);
                
                do {
                    std::map<std::string, SymExpr> tempCaptures = captures;
                    bool allMatch = true;
                    for (size_t i = 0; i < nArgs.size(); ++i) {
                        if (!matchAST(nArgs[indices[i]], pArgs[i], tempCaptures)) {
                            allMatch = false;
                            break;
                        }
                    }
                    if (allMatch) {
                        captures = tempCaptures;
                        return true;
                    }
                } while (std::next_permutation(indices.begin(), indices.end()));
                
                return false;
            }
        }
        return false;
    }

    // 将捕获的 AST 塞回目标模板中
    static SymExpr substituteCaptures(const SymExpr& target, const std::map<std::string, SymExpr>& captures) {
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
                std::vector<bool> v(N);
                std::fill(v.end() - K, v.end(), true);
                
                do {
                    std::vector<std::shared_ptr<SymNode>> selected;
                    std::vector<std::shared_ptr<SymNode>> remaining;
                    for (size_t i = 0; i < N; ++i) {
                        if (v[i]) selected.push_back(cArgs[i]);
                        else remaining.push_back(cArgs[i]);
                    }
                    
                    std::vector<size_t> indices(K);
                    std::iota(indices.begin(), indices.end(), 0);
                    bool subsetMatched = false;
                    std::map<std::string, SymExpr> subCaptures;
                    
                    do {
                        subCaptures.clear();
                        bool allMatch = true;
                        for (size_t i = 0; i < K; ++i) {
                            if (!matchAST(selected[indices[i]], pArgs[i], subCaptures)) {
                                allMatch = false;
                                break;
                            }
                        }
                        if (allMatch) {
                            subsetMatched = true;
                            break;
                        }
                    } while (std::next_permutation(indices.begin(), indices.end()));
                    
                    if (subsetMatched) {
                        SymExpr replaced = substituteCaptures(target, subCaptures);
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
                } while (std::next_permutation(v.begin(), v.end()));
            }
        }
        
        return current;
    }

    // =================================================================
// 符号展开：带有防爆截断额度 maxPowTerms
// =================================================================
    SymExpr expand(const SymExpr& expr, int64_t maxPowTerms) {
        if (!expr.ptr) return expr;

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

                if (result.ptr->getType() == SymType::ADD) {
                    auto addNode = std::static_pointer_cast<SymAdd>(result.ptr);
                    SymExpr distributedSum(BigInt(0));
                    for (auto& addTerm : addNode->args) {
                        distributedSum = distributedSum + expand(SymExpr(addTerm) * factor, maxPowTerms);
                    }
                    result = distributedSum;
                }
                else if (factor.ptr->getType() == SymType::ADD) {
                    auto addNode = std::static_pointer_cast<SymAdd>(factor.ptr);
                    SymExpr distributedSum(BigInt(0));
                    for (auto& addTerm : addNode->args) {
                        distributedSum = distributedSum + expand(result * SymExpr(addTerm), maxPowTerms);
                    }
                    result = distributedSum;
                }
                else {
                    result = result * factor;
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

            if ((func->name == "log" || func->name == "ln") && expArgs.size() == 1) {
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
            if (val.isComplex()) {
                Complex c = val.asComplex();
                if (Tol::isEq(c.imag, 0.0)) return SymExpr(c.real);
                return SymExpr(c);
            } else {
                return SymExpr(val.asDouble());
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

            // 一般情况 (广义指数法则): u^v = e^(v * ln(u))
            // (u^v)' = u^v * (v' * ln(u) + v * u' / u)
            SymExpr ln_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
            return (u ^ v) * (dv * ln_u + v * du / u);
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
                if (name == "exp") {
                    return expr * du; // exp(u)' = exp(u) * u'
                }
                if (name == "log" || name == "ln") {
                    return du / u; // ln(u)' = u' / u
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
                    // POW 作为函数：(u^v)' = u^v * (v'*ln(u) + v*u'/u)
                    if (dv.isZero()) {
                        SymExpr power_down(std::make_shared<SymFunc>("pow", std::vector<std::shared_ptr<SymNode>>{u.ptr, (v - SymExpr(1)).ptr}));
                        return v * power_down * du;
                    }
                    SymExpr ln_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    return expr * (dv * ln_u + v * du / u);
                }
                if (name == "root") {
                    // root(u, v) 实际上是 u^(1/v)。将其转换到底层幂节点然后递归求导即可！
                    SymExpr p = u ^ (SymExpr(BigInt(1)) / v);
                    return diff(p, var);
                }
                if (name == "log") {
                    // 指定底数的对数 log(u, v) = ln(v) / ln(u) (u 为底，v 为真数)
                    // 运用商的导数法则: (f/g)' = (f'g - fg')/g^2
                    SymExpr ln_v(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v.ptr}));
                    SymExpr ln_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{u.ptr}));
                    SymExpr df = dv / v;
                    SymExpr dg = du / u;
                    return (df * ln_u - ln_v * dg) / (ln_u * ln_u);
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
                    if ((fn->name == "log" || fn->name == "ln") && fn->args.size() == 1) {
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
                            if ((fn->name == "log" || fn->name == "ln") && fn->args.size() == 1) {
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
    static bool containsVar(const std::shared_ptr<SymNode>& node, const std::string& var) {
        if (!node) return false;
        switch (node->getType()) {
        case SymType::NUM:  return false;
        case SymType::VAR:  return std::static_pointer_cast<SymVar>(node)->name == var;
        case SymType::ADD:
            for (auto& a : std::static_pointer_cast<SymAdd>(node)->args)
                if (containsVar(a, var)) return true;
            return false;
        case SymType::MUL:
            for (auto& a : std::static_pointer_cast<SymMul>(node)->args)
                if (containsVar(a, var)) return true;
            return false;
        case SymType::POW:
            return containsVar(std::static_pointer_cast<SymPow>(node)->base, var) ||
                containsVar(std::static_pointer_cast<SymPow>(node)->exp, var);
        case SymType::FUNC:
            for (auto& a : std::static_pointer_cast<SymFunc>(node)->args)
                if (containsVar(a, var)) return true;
            return false;
        }
        return false;
    }

    // =================================================================
// 收集 AST 中出现的所有变量名
// =================================================================
    static void collectAllVars(const std::shared_ptr<SymNode>& node, std::set<std::string>& vars) {
        if (!node) return;
        switch (node->getType()) {
        case SymType::NUM: break;
        case SymType::VAR:
            vars.insert(std::static_pointer_cast<SymVar>(node)->name);
            break;
        case SymType::ADD:
            for (auto& a : std::static_pointer_cast<SymAdd>(node)->args) collectAllVars(a, vars);
            break;
        case SymType::MUL:
            for (auto& a : std::static_pointer_cast<SymMul>(node)->args) collectAllVars(a, vars);
            break;
        case SymType::POW:
            collectAllVars(std::static_pointer_cast<SymPow>(node)->base, vars);
            collectAllVars(std::static_pointer_cast<SymPow>(node)->exp, vars);
            break;
        case SymType::FUNC:
            for (auto& a : std::static_pointer_cast<SymFunc>(node)->args) collectAllVars(a, vars);
            break;
        }
    }

    // =================================================================
// 基础化简：身份吸收 + expand/contract 博弈
// ★ 不调用 factor，专门用于 factor 内部，防止循环递归
// =================================================================
    static SymExpr simplifyCore(const SymExpr& expr) {
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
                if ((func->name == "log" || func->name == "ln") && inner.isOne())
                    return SymExpr(BigInt(0));
                if (func->name == "exp" && inner.isZero())
                    return SymExpr(BigInt(1));
                if ((func->name == "log" || func->name == "ln") && inner.ptr->getType() == SymType::FUNC) {
                    auto innerFn = std::static_pointer_cast<SymFunc>(inner.ptr);
                    if (innerFn->name == "exp") return SymExpr(innerFn->args[0]);
                }
                if (func->name == "exp" && inner.ptr->getType() == SymType::FUNC) {
                    auto innerFn = std::static_pointer_cast<SymFunc>(inner.ptr);
                    if (innerFn->name == "log" || innerFn->name == "ln")
                        return SymExpr(innerFn->args[0]);
                }
                if (inner.isZero()) {
                    if (func->name == "sin") return SymExpr(BigInt(0));
                    if (func->name == "cos") return SymExpr(BigInt(1));
                    if (func->name == "erf" || func->name == "fresnel_s" || func->name == "fresnel_c" || func->name == "Si") return SymExpr(BigInt(0));
                }
                if (func->name == "sqrt") {
                    return inner ^ SymExpr(Fraction(1, 2));
                }
                if (func->name == "cbrt") {
                    return inner ^ SymExpr(Fraction(1, 3));
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
    // 动态多项式系数提取器：无限次 (消除 int64_t 转型警告修正版)
    // =================================================================
    static std::vector<SymExpr> extractCoeffs(const SymExpr& expr, const std::string& var) {
        SymExpr expanded;
        try { expanded = expand(expr, 500); }
        catch (const std::runtime_error&) { return {}; }

        std::map<int, SymExpr> degreeMap;

        auto processTerm = [&](const SymExpr& term) -> bool {
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
                    // 如果该因子不包含 x，视为系数
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
    int getDegree(const SymExpr& expr, const std::string& var) {
        auto coeffs = extractCoeffs(expr, var);
        if (coeffs.empty()) return -1;
        return static_cast<int>(coeffs.size()) - 1;
    }

    std::pair<SymExpr, SymExpr> polyDiv(const SymExpr& dividend, const SymExpr& divisor, const std::string& var) {
        SymExpr Q(BigInt(0));
        SymExpr R = dividend;
        
        auto divCoeffs = extractCoeffs(divisor, var);
        if (divCoeffs.empty()) throw std::runtime_error("Math Error: Divisor is not a polynomial in " + var);
        int degDiv = static_cast<int>(divCoeffs.size()) - 1;
        SymExpr leadDiv = divCoeffs.back();
        if (leadDiv.isZero()) throw std::runtime_error("Math Error: Division by zero polynomial.");

        SymExpr X = SymExpr::makeVar(var);

        while (!R.isZero()) {
            auto rCoeffs = extractCoeffs(R, var);
            if (rCoeffs.empty()) break; // R 不是多项式，无法继续除
            int degR = static_cast<int>(rCoeffs.size()) - 1;
            if (degR < degDiv) break;

            SymExpr leadR = rCoeffs.back();
            SymExpr termCoeff = simplifyCore(expand(leadR / leadDiv, 100)); // 强制展开以消除分母中的隐藏根式
            
            if (getAstNodeCount(termCoeff) > 5000) {
                throw std::runtime_error("Math Error: Polynomial division failed due to coefficient explosion.");
            }
            
            SymExpr term = termCoeff;
            if (degR - degDiv > 0) {
                term = term * (X ^ SymExpr(BigInt(degR - degDiv)));
            }

            Q = Q + term;
            R = simplifyCore(expand(R - term * divisor, 500));
            
            if (getAstNodeCount(R) > 5000) {
                throw std::runtime_error("Math Error: Polynomial division failed due to remainder explosion.");
            }
        }

        return { simplifyCore(expand(Q, 100)), simplifyCore(expand(R, 100)) };
    }

    SymExpr polyGCD(const SymExpr& a, const SymExpr& b, const std::string& var) {
        SymExpr u = a;
        SymExpr v = b;

        while (!v.isZero()) {
            auto [q, r] = polyDiv(u, v, var);
            u = v;
            v = r;
        }

        // 首一化 (Monic)
        auto coeffs = extractCoeffs(u, var);
        if (!coeffs.empty()) {
            SymExpr lead = coeffs.back();
            if (!lead.isZero() && !lead.isOne()) {
                u = simplifyCore(expand(u / lead, 500));
            }
        }
        return u;
    }

    std::tuple<SymExpr, SymExpr, SymExpr> polyEGCD(const SymExpr& a, const SymExpr& b, const std::string& var) {
        SymExpr r0 = a, r1 = b;
        SymExpr s0(BigInt(1)), s1(BigInt(0));
        SymExpr t0(BigInt(0)), t1(BigInt(1));

        while (!r1.isZero()) {
            auto [q, r] = polyDiv(r0, r1, var);
            r0 = r1; r1 = r;
            SymExpr s_temp = simplifyCore(expand(s0 - q * s1, 500));
            s0 = s1; s1 = s_temp;
            SymExpr t_temp = simplifyCore(expand(t0 - q * t1, 500));
            t0 = t1; t1 = t_temp;
            
            if (getAstNodeCount(s1) > 5000 || getAstNodeCount(t1) > 5000) {
                throw std::runtime_error("Math Error: polyEGCD failed due to coefficient explosion.");
            }
        }

        auto coeffs = extractCoeffs(r0, var);
        if (!coeffs.empty()) {
            SymExpr lead = coeffs.back();
            if (!lead.isZero() && !lead.isOne()) {
                r0 = simplifyCore(expand(r0 / lead, 500));
                s0 = simplifyCore(expand(s0 / lead, 500));
                t0 = simplifyCore(expand(t0 / lead, 500));
            }
        }
        
        // 最终清理，防止 Bezout 系数中残留未合并的代数数
        return { simplifyCore(expand(r0, 100)), simplifyCore(expand(s0, 100)), simplifyCore(expand(t0, 100)) };
    }

    // =================================================================
// 尝试对表达式开精确平方根
// 仅处理 NUM, POW(偶数幂), MUL(逐因子开根) 三类结构
// 返回 {是否成功, 平方根表达式}
// =================================================================
    static std::pair<bool, SymExpr> trySquareRoot(const SymExpr& expr, bool allowPartial = false) {
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
    // 全域多元全次因式分解 (Multivariate Polynomial Factorization)
    // 包含: 二次求解 + N次完全平方式提取
    // =================================================================
        // =================================================================
    // 全域多元全次因式分解 (递归闭环威力加强版)
    // =================================================================
    static SymExpr multivariatePolynomialFactor(const SymExpr& expr, int depth) {
        if (depth > 4) return expr; // 极限深度保护，防止复杂多元死锁

        std::set<std::string> vars;
        collectAllVars(expr.ptr, vars);
        if (vars.empty()) return expr;

        for (const std::string& mainVar : vars) {
            auto coeffs = extractCoeffs(expr, mainVar);
            if (coeffs.empty()) continue;

            int N = static_cast<int>(coeffs.size()) - 1;
            if (N < 2) continue;

            SymExpr lead = coeffs[N];
            if (lead.isZero()) continue;

            SymExpr X = SymExpr::makeVar(mainVar);

            // =======================================================
            // 策略 1：N次完全平方式嗅探
            // =======================================================
            SymExpr nextCoeff = coeffs[N - 1];
            SymExpr R = simplifyCore(nextCoeff / (SymExpr(BigInt(N)) * lead));
            SymExpr rawCandidate = simplifyCore(lead * ((X + R) ^ SymExpr(BigInt(N))));

            SymExpr checkZero;
            try { checkZero = simplifyCore(expr - rawCandidate); }
            catch (...) { checkZero = SymExpr(BigInt(1)); }
            try { checkZero = simplifyCore(expand(checkZero, 500)); }
            catch (...) {}

            if (checkZero.isZero()) {
                auto [sqrtOk, rootLead] = trySquareRoot(lead);
                if (sqrtOk) {
                    return simplifyCore((rootLead * X + simplifyCore(rootLead * R)) ^ SymExpr(BigInt(N)));
                }
                return rawCandidate;
            }

            // =======================================================
            // 策略 2：二次方程降维打击 (极简优雅版，不再胡乱加补丁)
            // =======================================================
            if (N == 2) {
                SymExpr C = coeffs[0];
                SymExpr B = coeffs[1];
                SymExpr A = coeffs[2];

                SymExpr delta = B * B - SymExpr(BigInt(4)) * A * C;
                // 让化简引擎算出判别式的多项式，然后扔给 factor
                SymExpr simpDelta = simplifyCore(delta);
                SymExpr factoredDelta = factor(simpDelta, depth + 1);

                auto [sqrtOk, sqrtDelta] = trySquareRoot(factoredDelta);
                if (!sqrtOk) continue;

                // 依靠系统底层卓越的 operator* 和 operator/ 来负责数学相消！
                SymExpr twoA = SymExpr(BigInt(2)) * A;
                SymExpr num1 = factor(simplifyCore(-B + sqrtDelta), depth + 1);
                SymExpr num2 = factor(simplifyCore(-B - sqrtDelta), depth + 1);

                // 让底层的同底数幂相加法则自然地处理 2*(y-z) 和 2^-1 * (y-z)^-1 的相消
                SymExpr r1 = num1 / twoA;
                SymExpr r2 = num2 / twoA;

                if (A.isOne()) return (X - r1) * (X - r2);
                return A * (X - r1) * (X - r2);
            }
        }
        return expr;
    }

    // =================================================================
    // 因式分解主入口
    // =================================================================
    SymExpr factor(const SymExpr& expr, int depth) {      // 增加 depth 签名
        if (!expr.ptr || depth > 10) return expr;          // 极限保险
        SymExpr quadResult = multivariatePolynomialFactor(expr, depth);  // 接入 depth
        if (quadResult.ptr != expr.ptr) return quadResult;
        // ══════════════════════════════════════════
        // 递归处理非加法节点
        // ══════════════════════════════════════════
        if (expr.ptr->getType() == SymType::MUL) {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr res(BigInt(1));
            for (auto& arg : mul->args) res = res * factor(SymExpr(arg));
            return res;
        }
        if (expr.ptr->getType() == SymType::POW) {
            auto p = std::static_pointer_cast<SymPow>(expr.ptr);
            return factor(SymExpr(p->base)) ^ factor(SymExpr(p->exp));
        }
        if (expr.ptr->getType() == SymType::FUNC) {
            auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<std::shared_ptr<SymNode>> nArgs;
            for (auto& arg : f->args) nArgs.push_back(factor(SymExpr(arg)).ptr);
            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
        }

        // ══════════════════════════════════════════
        // 阶段 2：公因式提取（对加法节点）
        // ══════════════════════════════════════════
        if (expr.ptr->getType() == SymType::ADD) {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            if (add->args.size() < 2) return expr;

            BigInt numGcd(0);
            bool firstTerm = true;
            // 记录结构：因子字面量 -> {因子表达式, 最小指数}
            std::map<std::string, std::pair<SymExpr, int64_t>> commonSym;

            struct ParsedTerm {
                BigInt coeff;
                std::map<std::string, std::pair<SymExpr, int64_t>> syms;
            };
            std::vector<ParsedTerm> parsedTerms;

            // ── 2a. 解析加法中的每一项 ──
            for (auto& arg : add->args) {
                ParsedTerm pt;
                pt.coeff = BigInt(1);

                auto processFactor = [&](const SymExpr& f) {
                    if (f.ptr->getType() == SymType::NUM) {
                        auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(f.ptr)->value);
                        if (isInt) pt.coeff = pt.coeff * BigInt(n);
                        else pt.syms[f.toString()] = { f, 1 };
                    }
                    else if (f.ptr->getType() == SymType::POW) {
                        auto p = std::static_pointer_cast<SymPow>(f.ptr);
                        if (p->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                            if (isInt && n > 0) {
                                SymExpr base(p->base);
                                pt.syms[base.toString()] = { base, n };
                            }
                            else {
                                pt.syms[f.toString()] = { f, 1 };
                            }
                        }
                        else {
                            pt.syms[f.toString()] = { f, 1 };
                        }
                    }
                    else {
                        pt.syms[f.toString()] = { f, 1 };
                    }
                    };

                if (arg->getType() == SymType::MUL) {
                    for (auto& f : std::static_pointer_cast<SymMul>(arg)->args)
                        processFactor(SymExpr(f));
                }
                else {
                    processFactor(SymExpr(arg));
                }

                parsedTerms.push_back(pt);

                // ── 2b. 与总公因式做交集 ──
                if (firstTerm) {
                    numGcd = pt.coeff.abs();
                    commonSym = pt.syms;
                    firstTerm = false;
                }
                else {
                    if (!pt.coeff.isZero() && !numGcd.isZero())
                        numGcd = BigInt::gcd(numGcd, pt.coeff.abs());
                    else
                        numGcd = BigInt(1);

                    for (auto it = commonSym.begin(); it != commonSym.end(); ) {
                        auto fnd = pt.syms.find(it->first);
                        if (fnd == pt.syms.end()) {
                            it = commonSym.erase(it);
                        }
                        else {
                            it->second.second = std::min(it->second.second, fnd->second.second);
                            ++it;
                        }
                    }
                }
            }

            // ── 2c. 构造公因式 ──
            SymExpr commonFactor(numGcd);
            for (auto& kv : commonSym) {
                int64_t p = kv.second.second;
                if (p == 1) commonFactor = commonFactor * kv.second.first;
                else if (p > 1) commonFactor = commonFactor * (kv.second.first ^ SymExpr(BigInt(p)));
            }

            // 如果公因式就是 1，无法提取，老老实实退回
            if (commonFactor.isOne()) {
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) res = res + factor(SymExpr(arg));
                return res;
            }

            // ── 2d. 剥离公因式，组装括号内的剩余加法 ──
            SymExpr newAdd(BigInt(0));
            for (auto& pt : parsedTerms) {
                SymExpr rem = numGcd.isZero() ? SymExpr(pt.coeff) : SymExpr(pt.coeff / numGcd);
                for (auto& kv : pt.syms) {
                    int64_t origP = kv.second.second;
                    int64_t commonP = 0;
                    auto fnd = commonSym.find(kv.first);
                    if (fnd != commonSym.end()) commonP = fnd->second.second;
                    int64_t remainP = origP - commonP;
                    if (remainP == 1) rem = rem * kv.second.first;
                    else if (remainP > 1) rem = rem * (kv.second.first ^ SymExpr(BigInt(remainP)));
                }
                newAdd = newAdd + rem;
            }

            // ★ 递归闭环：对括号内的剩余部分继续分解！
            return commonFactor * factor(newAdd);
        }

        return expr;
    }

    // =================================================================
    // 实数域完全因式分解 (Real Domain Factorization)
    // =================================================================
    SymExpr factorReal(const SymExpr& expr) {
        if (!expr.ptr) return expr;

        // 1. 先进行有理域因式分解
        SymExpr factored = factor(expr);

        // 2. 遍历因子，在实数域上进一步分解
        std::function<SymExpr(const SymExpr&)> process = [&](const SymExpr& e) -> SymExpr {
            if (e.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(e.ptr);
                SymExpr res(BigInt(1));
                for (auto& arg : mul->args) res = res * process(SymExpr(arg));
                return res;
            }
            if (e.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(e.ptr);
                return process(SymExpr(powNode->base)) ^ SymExpr(powNode->exp);
            }

            std::set<std::string> vars;
            collectAllVars(e.ptr, vars);
            if (vars.size() == 1) {
                std::string var = *vars.begin();
                auto coeffs = extractCoeffs(e, var);
                if (coeffs.size() > 2) {
                    int degree = static_cast<int>(coeffs.size()) - 1;
                    SymExpr X = SymExpr::makeVar(var);

                    // 二次多项式：直接使用求根公式判断实数域可约性
                    if (degree == 2) {
                        SymExpr C = coeffs[0];
                        SymExpr B = coeffs[1];
                        SymExpr A = coeffs[2];
                        SymExpr delta = simplify(B * B - SymExpr(BigInt(4)) * A * C);

                        bool isNeg = false;
                        if (delta.ptr->getType() == SymType::NUM) {
                            isNeg = isCasNegative(std::static_pointer_cast<SymNum>(delta.ptr)->value);
                        } else if (delta.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(delta.ptr);
                            if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                            }
                        }

                        if (!isNeg) {
                            SymExpr sqrtDelta;
                            auto [ok, exactSqrt] = trySquareRoot(delta, true);
                            if (ok) sqrtDelta = exactSqrt;
                            else sqrtDelta = delta ^ SymExpr(Fraction(1, 2));

                            SymExpr twoA = SymExpr(BigInt(2)) * A;
                            SymExpr r1 = simplify((-B + sqrtDelta) / twoA);
                            SymExpr r2 = simplify((-B - sqrtDelta) / twoA);

                            if (A.isOne()) return (X - r1) * (X - r2);
                            return A * (X - r1) * (X - r2);
                        }
                        return e; // 实数域不可约
                    }

                    // 检查是否为双二次多项式 Ax^4 + Bx^2 + C
                    if (degree == 4 && coeffs[3].isZero() && coeffs[1].isZero()) {
                        SymExpr A = coeffs[4];
                        SymExpr B = coeffs[2];
                        SymExpr C = coeffs[0];
                        SymExpr delta = simplify(B * B - SymExpr(BigInt(4)) * A * C);
                        
                        bool isNeg = false;
                        if (delta.ptr->getType() == SymType::NUM) {
                            isNeg = isCasNegative(std::static_pointer_cast<SymNum>(delta.ptr)->value);
                        } else if (delta.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(delta.ptr);
                            if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                            }
                        }
                        
                        if (!isNeg) {
                            SymExpr sqrtDelta;
                            auto [ok, exactSqrt] = trySquareRoot(delta, true);
                            if (ok) sqrtDelta = exactSqrt;
                            else sqrtDelta = delta ^ SymExpr(Fraction(1, 2));

                            SymExpr twoA = SymExpr(BigInt(2)) * A;
                            SymExpr u1 = simplify((-B + sqrtDelta) / twoA);
                            SymExpr u2 = simplify((-B - sqrtDelta) / twoA);
                            
                            SymExpr f1 = X * X - u1;
                            SymExpr f2 = X * X - u2;
                            
                            if (A.isOne()) return process(f1) * process(f2);
                            return A * process(f1) * process(f2);
                        } else {
                            // delta < 0, 必定有 A 和 C 同号，可配方为平方差
                            SymExpr CA = simplify(C / A);
                            auto [ok, sqrtCA] = trySquareRoot(CA, true);
                            SymExpr rootCA = ok ? sqrtCA : (CA ^ SymExpr(Fraction(1, 2)));
                            
                            SymExpr middle = simplify(SymExpr(BigInt(2)) * rootCA - B / A);
                            auto [ok2, sqrtMiddle] = trySquareRoot(middle, true);
                            SymExpr rootMiddle = ok2 ? sqrtMiddle : (middle ^ SymExpr(Fraction(1, 2)));
                            
                            SymExpr f1 = X * X - rootMiddle * X + rootCA;
                            SymExpr f2 = X * X + rootMiddle * X + rootCA;
                            
                            if (A.isOne()) return process(f1) * process(f2);
                            return A * process(f1) * process(f2);
                        }
                    }

                    // 高次多项式：先检查是否为二项式 Ax^n + B
                    bool isBinomial = true;
                    for (int i = 1; i < degree; ++i) {
                        if (!coeffs[i].isZero()) {
                            isBinomial = false;
                            break;
                        }
                    }
                    if (isBinomial && degree > 2) {
                        SymExpr A = coeffs[degree];
                        SymExpr B = coeffs[0];
                        SymExpr BA = simplify(B / A);
                        
                        bool isPos = false;
                        bool isNeg = false;
                        if (BA.ptr->getType() == SymType::NUM) {
                            isNeg = isCasNegative(std::static_pointer_cast<SymNum>(BA.ptr)->value);
                            isPos = !isNeg && !BA.isZero();
                        } else if (BA.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(BA.ptr);
                            if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                                isPos = !isNeg;
                            } else {
                                isPos = true;
                            }
                        } else {
                            isPos = true;
                        }

                        SymExpr absBA = isNeg ? simplify(-BA) : BA;
                        SymExpr R = simplify(absBA ^ SymExpr(Fraction(1, degree)));
                        SymExpr res = A;
                        int n = degree;

                        auto getExactCos = [](int m, int n_val) -> SymExpr {
                            int g = static_cast<int>(BigInt::gcd(BigInt(std::abs(m)), BigInt(n_val)).toDouble());
                            m /= g;
                            n_val /= g;
                            if (m < 0) m = -m;
                            m %= (2 * n_val);
                            if (m > n_val) m = 2 * n_val - m;
                            
                            if (m == 0) return SymExpr(BigInt(1));
                            if (m == n_val) return SymExpr(BigInt(-1));
                            if (2 * m == n_val) return SymExpr(BigInt(0));
                            
                            if (n_val == 3 && m == 1) return SymExpr(Fraction(1, 2));
                            if (n_val == 4 && m == 1) return SymExpr(BigInt(2)) ^ SymExpr(Fraction(-1, 2));
                            if (n_val == 4 && m == 3) return -(SymExpr(BigInt(2)) ^ SymExpr(Fraction(-1, 2)));
                            if (n_val == 6 && m == 1) return (SymExpr(BigInt(3)) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                            if (n_val == 6 && m == 5) return -(SymExpr(BigInt(3)) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                            if (n_val == 5) {
                                SymExpr sqrt5 = SymExpr(BigInt(5)) ^ SymExpr(Fraction(1, 2));
                                if (m == 1) return (sqrt5 + SymExpr(BigInt(1))) / SymExpr(BigInt(4));
                                if (m == 2) return (sqrt5 - SymExpr(BigInt(1))) / SymExpr(BigInt(4));
                                if (m == 3) return (SymExpr(BigInt(1)) - sqrt5) / SymExpr(BigInt(4));
                                if (m == 4) return -(sqrt5 + SymExpr(BigInt(1))) / SymExpr(BigInt(4));
                            }
                            if (n_val == 8) {
                                SymExpr sqrt2 = SymExpr(BigInt(2)) ^ SymExpr(Fraction(1, 2));
                                if (m == 1) return ((SymExpr(BigInt(2)) + sqrt2) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                                if (m == 3) return ((SymExpr(BigInt(2)) - sqrt2) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                                if (m == 5) return -((SymExpr(BigInt(2)) - sqrt2) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                                if (m == 7) return -((SymExpr(BigInt(2)) + sqrt2) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(2));
                            }
                            if (n_val == 10) {
                                SymExpr sqrt5 = SymExpr(BigInt(5)) ^ SymExpr(Fraction(1, 2));
                                if (m == 1) return ((SymExpr(BigInt(10)) + SymExpr(BigInt(2)) * sqrt5) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(4));
                                if (m == 3) return ((SymExpr(BigInt(10)) - SymExpr(BigInt(2)) * sqrt5) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(4));
                                if (m == 7) return -((SymExpr(BigInt(10)) - SymExpr(BigInt(2)) * sqrt5) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(4));
                                if (m == 9) return -((SymExpr(BigInt(10)) + SymExpr(BigInt(2)) * sqrt5) ^ SymExpr(Fraction(1, 2))) / SymExpr(BigInt(4));
                            }
                            if (n_val == 12) {
                                SymExpr sqrt6 = SymExpr(BigInt(6)) ^ SymExpr(Fraction(1, 2));
                                SymExpr sqrt2 = SymExpr(BigInt(2)) ^ SymExpr(Fraction(1, 2));
                                if (m == 1) return (sqrt6 + sqrt2) / SymExpr(BigInt(4));
                                if (m == 5) return (sqrt6 - sqrt2) / SymExpr(BigInt(4));
                                if (m == 7) return -(sqrt6 - sqrt2) / SymExpr(BigInt(4));
                                if (m == 11) return -(sqrt6 + sqrt2) / SymExpr(BigInt(4));
                            }
                            
                            SymExpr theta = simplify(SymExpr(Fraction(m, n_val)) * SymExpr::makeVar("PI"));
                            return simplify(SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{theta.ptr})));
                        };

                        if (isPos) {
                            if (n % 2 != 0) {
                                res = res * (X + R);
                                for (int k = 1; k <= (n - 1) / 2; ++k) {
                                    SymExpr cosTheta = getExactCos(2 * k - 1, n);
                                    res = res * (X * X - SymExpr(BigInt(2)) * R * X * cosTheta + R * R);
                                }
                            } else {
                                for (int k = 1; k <= n / 2; ++k) {
                                    SymExpr cosTheta = getExactCos(2 * k - 1, n);
                                    res = res * (X * X - SymExpr(BigInt(2)) * R * X * cosTheta + R * R);
                                }
                            }
                        } else {
                            if (n % 2 != 0) {
                                res = res * (X - R);
                                for (int k = 1; k <= (n - 1) / 2; ++k) {
                                    SymExpr cosTheta = getExactCos(2 * k, n);
                                    res = res * (X * X - SymExpr(BigInt(2)) * R * X * cosTheta + R * R);
                                }
                            } else {
                                res = res * (X - R) * (X + R);
                                for (int k = 1; k <= (n - 2) / 2; ++k) {
                                    SymExpr cosTheta = getExactCos(2 * k, n);
                                    res = res * (X * X - SymExpr(BigInt(2)) * R * X * cosTheta + R * R);
                                }
                            }
                        }
                        return res;
                    }

                    // 其他高次多项式：尝试求根并提取实数一次因式
                    std::vector<SymExpr> roots = solveEq(e, var);
                    if (!roots.empty()) {
                        std::vector<SymExpr> realRoots;
                        for (const auto& r : roots) {
                            bool hasComplex = false;
                            std::function<void(const std::shared_ptr<SymNode>&)> checkComplex = [&](const std::shared_ptr<SymNode>& node) {
                                if (!node || hasComplex) return;
                                if (node->getType() == SymType::VAR) {
                                    auto varNode = std::static_pointer_cast<SymVar>(node);
                                    if (varNode->name == "i" || varNode->name == "I") hasComplex = true;
                                } else if (node->getType() == SymType::ADD) {
                                    for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) checkComplex(arg);
                                } else if (node->getType() == SymType::MUL) {
                                    for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) checkComplex(arg);
                                } else if (node->getType() == SymType::POW) {
                                    auto powNode = std::static_pointer_cast<SymPow>(node);
                                    if (powNode->base->getType() == SymType::NUM && powNode->exp->getType() == SymType::NUM) {
                                        auto baseNum = std::static_pointer_cast<SymNum>(powNode->base);
                                        auto expNum = std::static_pointer_cast<SymNum>(powNode->exp);
                                        if (isCasNegative(baseNum->value)) {
                                            auto [isInt, n_val] = extractExactInt(expNum->value);
                                            if (!isInt) hasComplex = true;
                                        }
                                    }
                                    checkComplex(powNode->base);
                                    checkComplex(powNode->exp);
                                } else if (node->getType() == SymType::FUNC) {
                                    for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) checkComplex(arg);
                                }
                            };
                            checkComplex(r.ptr);
                            if (!hasComplex) realRoots.push_back(r);
                        }

                        if (!realRoots.empty()) {
                            SymExpr rem = e;
                            SymExpr res(BigInt(1));

                            for (const auto& r : realRoots) {
                                SymExpr factorX = X - r;
                                while (true) {
                                    auto [q, rem_new] = polyDiv(rem, factorX, var);
                                    if (rem_new.isZero()) {
                                        res = res * factorX;
                                        rem = q;
                                    } else {
                                        break;
                                    }
                                }
                            }

                            if (!rem.isOne() && rem.toString() != e.toString()) {
                                // 对剩余部分递归处理（可能降次为二次，从而被上面的二次逻辑处理）
                                res = res * process(rem);
                            } else if (!rem.isOne()) {
                                res = res * rem;
                            }

                            if (res.toString() != e.toString()) {
                                return res;
                            }
                        }
                    }
                }
            }
            return e;
        };

        return process(factored);
    }

    // =================================================================
    // 有理分式化简 (Rational Fraction Simplification)
    // =================================================================
    static SymExpr simplifyRational(const SymExpr& expr) {
        if (!expr.ptr) return expr;
        if (expr.ptr->getType() == SymType::MUL) {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr num(BigInt(1));
            SymExpr den(BigInt(1));
            for (auto& arg : mul->args) {
                if (arg->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(arg);
                    if (powNode->exp->getType() == SymType::NUM) {
                        auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                        if (isInt && n < 0) {
                            den = den * (SymExpr(powNode->base) ^ SymExpr(BigInt(-n)));
                            continue;
                        }
                    }
                }
                num = num * SymExpr(arg);
            }
            
            if (!den.isOne()) {
                SymExpr factNum = factorReal(num);
                SymExpr factDen = factorReal(den);
                SymExpr canceled = simplifyCore(factNum / factDen);
                
                if (canceled.toString() != expr.toString()) {
                    return canceled;
                }
            }
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
            if (coeffs.empty()) return;

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
                if (r.toString() == ur.toString()) {
                    found = true;
                    break;
                }
            }
            if (!found) uniqueRoots.push_back(r);
        }

        return uniqueRoots;
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
            try {
                coeff = simplify(subs(current_deriv, var, a));
            } catch (...) {
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
        if (depth > 10) throw std::runtime_error("Calculus Error: Limit evaluation depth exceeded.");
        if (!expr.ptr) return expr;

        // 尝试直接代入
        try {
            SymExpr subbed = subs(expr, var, val);
            SymExpr simp = simplify(subbed);
            // 如果没有抛出除零异常，且结果中不再包含该变量，说明代入成功
            if (!containsVar(simp.ptr, var)) return simp;
        } catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Division by zero") == std::string::npos && 
                msg.find("0^0") == std::string::npos) {
                throw;
            }
        }

        // 提取分子和分母
        SymExpr num(BigInt(1)), den(BigInt(1));
        if (expr.ptr->getType() == SymType::MUL) {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            for (auto& arg : mul->args) {
                if (arg->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(arg);
                    if (powNode->exp->getType() == SymType::NUM) {
                        auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                        if (isInt && n < 0) {
                            den = den * (SymExpr(powNode->base) ^ SymExpr(BigInt(-n)));
                            continue;
                        }
                    }
                }
                num = num * SymExpr(arg);
            }
        } else if (expr.ptr->getType() == SymType::POW) {
            auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
            if (powNode->exp->getType() == SymType::NUM) {
                auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                if (isInt && n < 0) {
                    den = SymExpr(powNode->base) ^ SymExpr(BigInt(-n));
                    num = SymExpr(BigInt(1));
                } else {
                    num = expr;
                }
            } else {
                num = expr;
            }
        } else {
            num = expr;
        }

        if (den.isOne()) {
            // 不是分式，但代入失败，可能含有复杂奇点，直接返回化简后的代入形式
            try { return simplify(subs(expr, var, val)); } catch (...) { return expr; }
        }

        // 检查是否为 0/0 型
        bool numZero = false, denZero = false;
        try { numZero = simplify(subs(num, var, val)).isZero(); } catch (...) {}
        try { denZero = simplify(subs(den, var, val)).isZero(); } catch (...) {}

        if (numZero && denZero) {
            if (depth > 3) {
                // 泰勒展开截断求极限 (Taylor Series Truncation)
                // 寻找分子分母的最低非零导数 (即泰勒展开的首个非零项)
                SymExpr dNum = num, dDen = den;
                SymExpr coeffNum(BigInt(0)), coeffDen(BigInt(0));
                int orderNum = -1, orderDen = -1;
                
                for (int i = 0; i <= 10; ++i) {
                    try { coeffNum = simplify(subs(dNum, var, val)); } catch (...) {}
                    if (!coeffNum.isZero()) { orderNum = i; break; }
                    dNum = diff(dNum, var);
                }
                for (int i = 0; i <= 10; ++i) {
                    try { coeffDen = simplify(subs(dDen, var, val)); } catch (...) {}
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

        return simplify(subs(expr, var, val));
    }

    SymExpr limit(const SymExpr& expr, const std::string& var, const SymExpr& val) {
        return limitCore(expr, var, val, 0);
    }

    // =================================================================
    // 实数域有理分式积分模版 (Rational Function Integration Templates)
    // =================================================================
    static SymExpr integratePartialFraction(SymExpr N, SymExpr D, int k, const std::string& var) {
        int degD = getDegree(D, var);
        auto coeffsD = extractCoeffs(D, var);
        auto coeffsN = extractCoeffs(N, var);
        
        SymExpr X = SymExpr::makeVar(var);

        if (degD == 1) {
            SymExpr a = coeffsD[1];
            SymExpr b = coeffsD[0];
            SymExpr B = simplifyCore(b / a);
            
            SymExpr A_num = coeffsN.empty() ? SymExpr(BigInt(0)) : coeffsN[0];
            SymExpr A = simplifyCore(A_num / (a ^ SymExpr(BigInt(k))));

            if (A.isZero()) return SymExpr(BigInt(0));

            if (k == 1) {
                return A * SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{simplifyCore(X + B).ptr}));
            } else {
                return simplifyCore((A / SymExpr(BigInt(1 - k))) * ((X + B) ^ SymExpr(BigInt(1 - k))));
            }
        } else if (degD == 2) {
            SymExpr a = coeffsD[2];
            SymExpr b = coeffsD[1];
            SymExpr c = coeffsD[0];
            SymExpr p = simplifyCore(b / a);
            SymExpr q = simplifyCore(c / a);

            SymExpr A_num = coeffsN.size() > 1 ? coeffsN[1] : SymExpr(BigInt(0));
            SymExpr B_num = coeffsN.size() > 0 ? coeffsN[0] : SymExpr(BigInt(0));
            
            SymExpr ak = a ^ SymExpr(BigInt(k));
            SymExpr A = simplifyCore(A_num / ak);
            SymExpr B = simplifyCore(B_num / ak);

            SymExpr C = simplifyCore(B - (A * p) / SymExpr(BigInt(2)));
            SymExpr Delta = simplifyCore(q - (p * p) / SymExpr(BigInt(4)));
            
            SymExpr res(BigInt(0));
            SymExpr quadratic = simplifyCore((X ^ SymExpr(BigInt(2))) + p * X + q);

            if (!A.isZero()) {
                if (k == 1) {
                    res = res + (A / SymExpr(BigInt(2))) * SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{quadratic.ptr}));
                } else {
                    res = res + (A / (SymExpr(BigInt(2)) * SymExpr(BigInt(1 - k)))) * (quadratic ^ SymExpr(BigInt(1 - k)));
                }
            }

            if (!C.isZero()) {
                std::function<SymExpr(int)> integrateArctan = [&](int power) -> SymExpr {
                    if (power == 1) {
                        SymExpr sqrtDelta = simplifyCore(Delta ^ SymExpr(Fraction(1, 2)));
                        SymExpr atanArg = simplifyCore((X + p / SymExpr(BigInt(2))) / sqrtDelta);
                        return (SymExpr(BigInt(1)) / sqrtDelta) * SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atanArg.ptr}));
                    } else {
                        SymExpr u = simplifyCore(X + p / SymExpr(BigInt(2)));
                        SymExpr term1 = u / (SymExpr(BigInt(2)) * Delta * SymExpr(BigInt(power - 1)) * ((u * u + Delta) ^ SymExpr(BigInt(power - 1))));
                        SymExpr term2 = (SymExpr(BigInt(2 * power - 3)) / (SymExpr(BigInt(2)) * Delta * SymExpr(BigInt(power - 1)))) * integrateArctan(power - 1);
                        return simplifyCore(term1 + term2);
                    }
                };
                res = res + C * integrateArctan(k);
            }
            return res;
        }
        throw std::runtime_error("Calculus Error: Denominator degree > 2 not supported in partial fraction integration.");
    }

    // =================================================================
    // 提取有理分式的分子和分母 (Get Numerator and Denominator)
    // =================================================================
    static std::pair<SymExpr, SymExpr> getFraction(const SymExpr& expr) {
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
                        if (i != j) termNum = simplifyCore(expand(termNum * nds[j].second, 500));
                    }
                    num = simplifyCore(expand(num + termNum, 500));
                }
                return {num, den};
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
                SymExpr num(BigInt(1));
                SymExpr den(BigInt(1));
                for (auto& arg : mul->args) {
                    auto nd = getFraction(SymExpr(arg));
                    num = simplifyCore(expand(num * nd.first, 500));
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
                            return {simplifyCore(expand(nd.first ^ SymExpr(BigInt(n)), 500)), simplifyCore(nd.second ^ SymExpr(BigInt(n)))};
                        } else {
                            return {simplifyCore(expand(nd.second ^ SymExpr(BigInt(-n)), 500)), simplifyCore(nd.first ^ SymExpr(BigInt(-n)))};
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
    // 🚀 符号积分 (Symbolic Integration) - 基础多项式与初等函数
    // =================================================================
    SymExpr integrate(const SymExpr& expr, const std::string& var) {
        if (!expr.ptr) return expr;

        SymExpr x = SymExpr::makeVar(var);

        // 从规则库获取当前变量的积分规则
        std::vector<std::pair<SymExpr, SymExpr>> rules = getIntegRules(var);

        std::function<SymExpr(const SymExpr&, int)> doInteg = [&](const SymExpr& e, int depth) -> SymExpr {
            if (depth > 3) throw std::runtime_error("Calculus Error: Integration depth limit exceeded.");

            if (!containsVar(e.ptr, var)) {
                return e * x; // ∫ c dx = c * x
            }

            if (e.ptr->getType() == SymType::ADD) {
                auto add = std::static_pointer_cast<SymAdd>(e.ptr);
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) res = res + doInteg(SymExpr(arg), depth);
                return res;
            }

            // --- 提取系数和变量部分 ---
            SymExpr coeff(BigInt(1));
            SymExpr varPart(BigInt(1));

            if (e.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(e.ptr);
                for (auto& arg : mul->args) {
                    if (!containsVar(arg, var)) coeff = coeff * SymExpr(arg);
                    else varPart = varPart * SymExpr(arg);
                }
            } else {
                varPart = e;
            }

            // --- 0. 查表匹配 (Pattern Matching) ---
            SymExpr integratedPart = varPart;
            bool matched = false;
            for (const auto& rule : rules) {
                std::map<std::string, SymExpr> captures;
                if (matchAST(varPart.ptr, rule.first.ptr, captures)) {
                    integratedPart = substituteCaptures(rule.second, captures);
                    matched = true;
                    break;
                }
            }

            if (matched) {
                return coeff * integratedPart;
            }

            // --- 1. 线性换元法 (Linear Substitution) ---
            auto findLinearArg = [&](const std::shared_ptr<SymNode>& node, SymExpr& out_u, SymExpr& out_a) -> bool {
                if (!node) return false;
                if (node->getType() == SymType::FUNC) {
                    auto func = std::static_pointer_cast<SymFunc>(node);
                    for (auto& arg : func->args) {
                        SymExpr u(arg);
                        SymExpr du = simplifyCore(diff(u, var));
                        if (!du.isZero() && !containsVar(du.ptr, var)) {
                            if (u.ptr->getType() != SymType::VAR) {
                                out_u = u;
                                out_a = du;
                                return true;
                            }
                        }
                    }
                } else if (node->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(node);
                    SymExpr u(powNode->base);
                    SymExpr du = simplifyCore(diff(u, var));
                    if (!du.isZero() && !containsVar(du.ptr, var)) {
                        if (u.ptr->getType() != SymType::VAR) {
                            out_u = u;
                            out_a = du;
                            return true;
                        }
                    }
                }
                return false;
            };

            SymExpr sub_u, sub_a;
            if (findLinearArg(varPart.ptr, sub_u, sub_a)) {
                std::string u_var = "_u";
                SymExpr u_sym = SymExpr::makeVar(u_var);
                std::function<SymExpr(const SymExpr&)> replaceNode = [&](const SymExpr& expr_node) -> SymExpr {
                    if (!expr_node.ptr) return expr_node;
                    if (expr_node.toString() == sub_u.toString()) return u_sym;
                    switch (expr_node.ptr->getType()) {
                        case SymType::ADD: {
                            SymExpr res(BigInt(0));
                            for (auto& arg : std::static_pointer_cast<SymAdd>(expr_node.ptr)->args) res = res + replaceNode(SymExpr(arg));
                            return res;
                        }
                        case SymType::MUL: {
                            SymExpr res(BigInt(1));
                            for (auto& arg : std::static_pointer_cast<SymMul>(expr_node.ptr)->args) res = res * replaceNode(SymExpr(arg));
                            return res;
                        }
                        case SymType::POW: {
                            auto p = std::static_pointer_cast<SymPow>(expr_node.ptr);
                            return replaceNode(SymExpr(p->base)) ^ replaceNode(SymExpr(p->exp));
                        }
                        case SymType::FUNC: {
                            auto f = std::static_pointer_cast<SymFunc>(expr_node.ptr);
                            std::vector<std::shared_ptr<SymNode>> nArgs;
                            for (auto& arg : f->args) nArgs.push_back(replaceNode(SymExpr(arg)).ptr);
                            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                        }
                        default: return expr_node;
                    }
                };
                
                SymExpr f_u = replaceNode(varPart);
                if (!containsVar(f_u.ptr, var)) {
                    try {
                        SymExpr f_var = subs(f_u, u_var, SymExpr::makeVar(var));
                        SymExpr int_var = doInteg(simplify(expand(f_var / sub_a, 500)), depth + 1);
                        SymExpr res = subs(int_var, var, sub_u);
                        return coeff * res;
                    } catch (...) {}
                }
            }

            // --- 1.5 广义换元法 (凑微分法) ---
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                std::vector<SymExpr> candidate_us;
                for (auto& arg : mul->args) {
                    candidate_us.push_back(SymExpr(arg));
                    if (arg->getType() == SymType::POW) {
                        candidate_us.push_back(SymExpr(std::static_pointer_cast<SymPow>(arg)->base));
                    } else if (arg->getType() == SymType::FUNC) {
                        auto func = std::static_pointer_cast<SymFunc>(arg);
                        if (func->args.size() == 1) {
                            candidate_us.push_back(SymExpr(func->args[0]));
                        }
                    }
                }
                
                for (const auto& u : candidate_us) {
                    if (u.ptr->getType() == SymType::VAR || u.ptr->getType() == SymType::NUM) continue;
                    SymExpr du = simplifyCore(diff(u, var));
                    if (du.isZero()) continue;
                    
                    SymExpr rem = simplify(varPart / du);
                    
                    std::string u_var = "_u";
                    SymExpr u_sym = SymExpr::makeVar(u_var);
                    std::function<SymExpr(const SymExpr&)> replaceU = [&](const SymExpr& expr_node) -> SymExpr {
                        if (!expr_node.ptr) return expr_node;
                        if (expr_node.toString() == u.toString()) return u_sym;
                        switch (expr_node.ptr->getType()) {
                            case SymType::ADD: {
                                SymExpr res(BigInt(0));
                                for (auto& arg : std::static_pointer_cast<SymAdd>(expr_node.ptr)->args) res = res + replaceU(SymExpr(arg));
                                return res;
                            }
                            case SymType::MUL: {
                                SymExpr res(BigInt(1));
                                for (auto& arg : std::static_pointer_cast<SymMul>(expr_node.ptr)->args) res = res * replaceU(SymExpr(arg));
                                return res;
                            }
                            case SymType::POW: {
                                auto p = std::static_pointer_cast<SymPow>(expr_node.ptr);
                                return replaceU(SymExpr(p->base)) ^ replaceU(SymExpr(p->exp));
                            }
                            case SymType::FUNC: {
                                auto f = std::static_pointer_cast<SymFunc>(expr_node.ptr);
                                std::vector<std::shared_ptr<SymNode>> nArgs;
                                for (auto& arg : f->args) nArgs.push_back(replaceU(SymExpr(arg)).ptr);
                                return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                            }
                            default: return expr_node;
                        }
                    };
                    
                    SymExpr rem_u = replaceU(rem);
                    
                    if (containsVar(rem_u.ptr, var)) {
                        auto coeffs = extractCoeffs(u, var);
                        if (coeffs.size() > 1) {
                            bool isBinomial = true;
                            int n = static_cast<int>(coeffs.size()) - 1;
                            for (int i = 1; i < n; ++i) {
                                if (!coeffs[i].isZero()) { isBinomial = false; break; }
                            }
                            if (isBinomial && !coeffs[n].isZero()) {
                                SymExpr A = coeffs[n];
                                SymExpr B = coeffs[0];
                                SymExpr x_n = (u_sym - B) / A;
                                
                                std::function<SymExpr(const SymExpr&)> replaceXn = [&](const SymExpr& node) -> SymExpr {
                                    if (!node.ptr) return node;
                                    if (node.ptr->getType() == SymType::POW) {
                                        auto p = std::static_pointer_cast<SymPow>(node.ptr);
                                        if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var) {
                                            if (p->exp->getType() == SymType::NUM) {
                                                auto [isInt, exp_val] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                                                if (isInt && exp_val % n == 0) {
                                                    return x_n ^ SymExpr(BigInt(exp_val / n));
                                                }
                                            }
                                        }
                                    }
                                    if (n == 1 && node.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(node.ptr)->name == var) {
                                        return x_n;
                                    }
                                    switch (node.ptr->getType()) {
                                        case SymType::ADD: {
                                            SymExpr res(BigInt(0));
                                            for (auto& arg : std::static_pointer_cast<SymAdd>(node.ptr)->args) res = res + replaceXn(SymExpr(arg));
                                            return res;
                                        }
                                        case SymType::MUL: {
                                            SymExpr res(BigInt(1));
                                            for (auto& arg : std::static_pointer_cast<SymMul>(node.ptr)->args) res = res * replaceXn(SymExpr(arg));
                                            return res;
                                        }
                                        case SymType::POW: {
                                            auto p = std::static_pointer_cast<SymPow>(node.ptr);
                                            return replaceXn(SymExpr(p->base)) ^ replaceXn(SymExpr(p->exp));
                                        }
                                        case SymType::FUNC: {
                                            auto f = std::static_pointer_cast<SymFunc>(node.ptr);
                                            std::vector<std::shared_ptr<SymNode>> nArgs;
                                            for (auto& arg : f->args) nArgs.push_back(replaceXn(SymExpr(arg)).ptr);
                                            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                                        }
                                        default: return node;
                                    }
                                };
                                rem_u = simplify(expand(replaceXn(rem_u), 500));
                            }
                        }
                    }

                    if (!containsVar(rem_u.ptr, var)) {
                        try {
                            SymExpr f_var = subs(rem_u, u_var, SymExpr::makeVar(var));
                            SymExpr int_var = doInteg(simplify(expand(f_var, 500)), depth + 1);
                            SymExpr res = subs(int_var, var, u);
                            return coeff * res;
                        } catch (...) {}
                    }
                }
            }

            // --- 1.8 三角函数高次幂降幂与拆分 ---
            if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->base->getType() == SymType::FUNC && powNode->exp->getType() == SymType::NUM) {
                    auto func = std::static_pointer_cast<SymFunc>(powNode->base);
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt && n >= 2 && func->args.size() == 1) {
                        SymExpr arg(func->args[0]);
                        if (func->name == "sin") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) - _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), 500);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            } else {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr cosSq = SymExpr(BigInt(1)) - (_C ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (cosSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, 500);
                                SymExpr cosx = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_C", cosx);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            }
                        } else if (func->name == "cos") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) + _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), 500);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            } else {
                                SymExpr _S = SymExpr::makeVar("_S");
                                SymExpr sinSq = SymExpr(BigInt(1)) - (_S ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (sinSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, 500);
                                SymExpr sinx = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_S", sinx);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            }
                        }
                    }
                }
            }

            // --- 1.9 二次根式三角换元 (Trigonometric Substitution) ---
            if (var != "_t" && var != "_weierstrass_t") {
                SymExpr quadBase;
                bool foundQuad = false;
                
                std::function<void(const std::shared_ptr<SymNode>&)> findQuad = [&](const std::shared_ptr<SymNode>& node) {
                    if (!node || foundQuad) return;
                    if (node->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(node);
                        if (powNode->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                            if (!isInt) {
                                auto coeffs = extractCoeffs(SymExpr(powNode->base), var);
                                if (coeffs.size() == 3 && coeffs[1].isZero() && !coeffs[2].isZero() && !coeffs[0].isZero()) {
                                    quadBase = SymExpr(powNode->base);
                                    foundQuad = true;
                                    return;
                                }
                            }
                        }
                    }
                    if (node->getType() == SymType::ADD) {
                        for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) findQuad(arg);
                    } else if (node->getType() == SymType::MUL) {
                        for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) findQuad(arg);
                    } else if (node->getType() == SymType::FUNC) {
                        for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) findQuad(arg);
                    }
                };
                
                findQuad(varPart.ptr);
                
                if (foundQuad) {
                    auto coeffs = extractCoeffs(quadBase, var);
                    SymExpr A = coeffs[2];
                    SymExpr C = coeffs[0];
                    
                    auto isPos = [](const SymExpr& e) {
                        if (e.ptr->getType() == SymType::NUM) return !isCasNegative(std::static_pointer_cast<SymNum>(e.ptr)->value) && !e.isZero();
                        return false;
                    };
                    auto isNeg = [](const SymExpr& e) {
                        if (e.ptr->getType() == SymType::NUM) return isCasNegative(std::static_pointer_cast<SymNum>(e.ptr)->value);
                        return false;
                    };
                    
                    bool A_pos = isPos(A), A_neg = isNeg(A);
                    bool C_pos = isPos(C), C_neg = isNeg(C);
                    
                    SymExpr t = SymExpr::makeVar("_t");
                    SymExpr x_sub, dx_sub, t_back;
                    bool valid = false;
                    
                    if (A_pos && C_pos) {
                        SymExpr sqrtCA = simplifyCore((C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr tan_t(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA * tan_t;
                        dx_sub = sqrtCA / (cos_t * cos_t);
                        SymExpr atan_arg = simplifyCore(SymExpr::makeVar(var) / sqrtCA);
                        t_back = SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atan_arg.ptr}));
                        valid = true;
                    } else if (A_neg && C_pos) {
                        SymExpr sqrtCA = simplifyCore((-C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr sin_t(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA * sin_t;
                        dx_sub = sqrtCA * cos_t;
                        SymExpr asin_arg = simplifyCore(SymExpr::makeVar(var) / sqrtCA);
                        t_back = SymExpr(std::make_shared<SymFunc>("asin", std::vector<std::shared_ptr<SymNode>>{asin_arg.ptr}));
                        valid = true;
                    } else if (A_pos && C_neg) {
                        SymExpr sqrtCA = simplifyCore((-C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr sin_t(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA / cos_t;
                        dx_sub = sqrtCA * sin_t / (cos_t * cos_t);
                        SymExpr acos_arg = simplifyCore(sqrtCA / SymExpr::makeVar(var));
                        t_back = SymExpr(std::make_shared<SymFunc>("acos", std::vector<std::shared_ptr<SymNode>>{acos_arg.ptr}));
                        valid = true;
                    }
                    
                    if (valid) {
                        try {
                            SymExpr subbed = simplifyCore(subs(varPart, var, x_sub) * dx_sub);
                            SymExpr subbed_var = subs(subbed, "_t", SymExpr::makeVar(var));
                            SymExpr int_var = doInteg(trigsimp(subbed_var), depth + 1);
                            SymExpr res = subs(int_var, var, t_back);
                            return coeff * res;
                        } catch (...) {}
                    }
                }
            }

            // --- 2. 启发式分部积分 (Integration by Parts) ---
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                std::vector<SymExpr> factors;
                for (auto& arg : mul->args) factors.push_back(SymExpr(arg));

                auto getPriority = [&](const SymExpr& f) -> int {
                    if (f.ptr->getType() == SymType::FUNC) {
                        auto fn = std::static_pointer_cast<SymFunc>(f.ptr);
                        if (fn->name == "log" || fn->name == "ln" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") return 1;
                    }
                    if (f.ptr->getType() == SymType::POW) {
                        auto p = std::static_pointer_cast<SymPow>(f.ptr);
                        if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var) {
                            if (p->exp->getType() == SymType::NUM) {
                                auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                                if (isInt && n > 0) return 2;
                            }
                        }
                    }
                    if (f.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(f.ptr)->name == var) return 2;
                    return 3;
                };

                std::vector<size_t> indices(factors.size());
                std::iota(indices.begin(), indices.end(), 0);
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return getPriority(factors[a]) < getPriority(factors[b]);
                });

                for (size_t i : indices) {
                    SymExpr u = factors[i];
                    SymExpr dv(BigInt(1));
                    for (size_t j = 0; j < factors.size(); ++j) {
                        if (j != i) dv = dv * factors[j];
                    }

                    SymExpr du = simplifyCore(diff(u, var));
                    try {
                        SymExpr v = doInteg(dv, depth + 1);
                        SymExpr v_du = simplifyCore(v * du);
                        
                        // 循环分部积分检测 (1-step cycle)
                        SymExpr ratio1 = simplifyCore(v_du / varPart);
                        if (!containsVar(ratio1.ptr, var) && !ratio1.isZero()) {
                            SymExpr k = ratio1;
                            SymExpr one_plus_k = simplifyCore(SymExpr(BigInt(1)) + k);
                            if (!one_plus_k.isZero()) {
                                // 修复：分部积分循环时，必须确保 u*v 不会被错误化简
                                return coeff * simplify(expand((u * v) / one_plus_k, 500));
                            }
                        }
                        
                        // 循环分部积分检测 (2-step cycle)
                        if (v_du.ptr->getType() == SymType::MUL) {
                            auto mul2 = std::static_pointer_cast<SymMul>(v_du.ptr);
                            std::vector<SymExpr> factors2;
                            for (auto& arg : mul2->args) factors2.push_back(SymExpr(arg));
                            
                            std::vector<size_t> indices2(factors2.size());
                            std::iota(indices2.begin(), indices2.end(), 0);
                            std::sort(indices2.begin(), indices2.end(), [&](size_t a, size_t b) {
                                return getPriority(factors2[a]) < getPriority(factors2[b]);
                            });
                            
                            bool found_2step = false;
                            SymExpr result_2step;
                            for (size_t i2 : indices2) {
                                SymExpr u2 = factors2[i2];
                                SymExpr dv2(BigInt(1));
                                for (size_t j2 = 0; j2 < factors2.size(); ++j2) {
                                    if (j2 != i2) dv2 = dv2 * factors2[j2];
                                }
                                SymExpr du2 = simplifyCore(diff(u2, var));
                                try {
                                    SymExpr v2 = doInteg(dv2, depth + 2);
                                    SymExpr v2_du2 = simplifyCore(v2 * du2);
                                    
                                    SymExpr ratio2 = simplifyCore(v2_du2 / varPart);
                                    if (!containsVar(ratio2.ptr, var) && !ratio2.isZero()) {
                                        SymExpr k = ratio2;
                                        SymExpr one_minus_k = simplifyCore(SymExpr(BigInt(1)) - k);
                                        if (!one_minus_k.isZero()) {
                                            result_2step = coeff * simplifyCore((u * v - u2 * v2) / one_minus_k);
                                            found_2step = true;
                                            break;
                                        }
                                    }
                                } catch (...) {}
                            }
                            if (found_2step) return result_2step;
                        }

                        SymExpr int_v_du = doInteg(v_du, depth + 1);
                        return coeff * simplifyCore(u * v - int_v_du);
                    } catch (...) {}
                }
            }

            // --- 3. 有理分式积分引擎 (Rational Function Integration) ---
            SymExpr num(BigInt(1)), den(BigInt(1));
            bool isFraction = false;
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                for (auto& arg : mul->args) {
                    if (arg->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(arg);
                        if (powNode->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                            if (isInt && n < 0) {
                                den = den * (SymExpr(powNode->base) ^ SymExpr(BigInt(-n)));
                                isFraction = true;
                                continue;
                            }
                        }
                    }
                    num = num * SymExpr(arg);
                }
            } else if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt && n < 0) {
                        den = SymExpr(powNode->base) ^ SymExpr(BigInt(-n));
                        num = SymExpr(BigInt(1));
                        isFraction = true;
                    } else {
                        num = varPart;
                    }
                } else {
                    num = varPart;
                }
            } else {
                num = varPart;
            }

            if (isFraction && containsVar(den.ptr, var)) {
                SymExpr num_expanded = simplifyCore(expand(num, 500));
                SymExpr den_expanded = simplifyCore(expand(den, 500));
                
                int degN = getDegree(num_expanded, var);
                int degD = getDegree(den_expanded, var);
                
                // ★ 只有当分子和分母都是多项式时，才使用有理分式积分引擎
                if (degD >= 0 && (degN >= 0 || !containsVar(num_expanded.ptr, var))) {
                    SymExpr polyE(BigInt(0));
                    SymExpr R = num_expanded;
                    
                    if (degN >= degD && degD > 0) {
                        auto [q, r] = polyDiv(num_expanded, den_expanded, var);
                        polyE = q;
                        R = r;
                    }
                    
                    SymExpr res(BigInt(0));
                    if (!polyE.isZero()) {
                        res = doInteg(polyE, depth);
                    }
                    
                    if (!R.isZero()) {
                        SymExpr factD = factorReal(den);
                        std::vector<std::pair<SymExpr, int>> factors;
                        auto process = [&](const SymExpr& f) {
                            if (!containsVar(f.ptr, var)) return;
                            if (f.ptr->getType() == SymType::POW) {
                                auto powNode = std::static_pointer_cast<SymPow>(f.ptr);
                                if (powNode->exp->getType() == SymType::NUM) {
                                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                                    if (isInt && n > 0) {
                                        factors.push_back({SymExpr(powNode->base), static_cast<int>(n)});
                                        return;
                                    }
                                }
                            }
                            factors.push_back({f, 1});
                        };
                        if (factD.ptr->getType() == SymType::MUL) {
                            for (auto& arg : std::static_pointer_cast<SymMul>(factD.ptr)->args) process(SymExpr(arg));
                        } else {
                            process(factD);
                        }
                        
                        if (factors.empty()) {
                            throw std::runtime_error("Calculus Error: Failed to factorize denominator for partial fraction decomposition.");
                        } else {
                            SymExpr remaining_N = R;
                            SymExpr remaining_D(BigInt(1));
                            for (auto& f : factors) remaining_D = simplifyCore(expand(remaining_D * (f.first ^ SymExpr(BigInt(f.second))), 500));
                            
                            SymExpr D_expanded = den_expanded;
                            auto coeffs_D = extractCoeffs(D_expanded, var);
                            auto coeffs_rem = extractCoeffs(remaining_D, var);
                            if (coeffs_D.empty() || coeffs_rem.empty()) {
                                throw std::runtime_error("Calculus Error: Denominator is not a polynomial.");
                            }
                            auto lead_D = coeffs_D.back();
                            auto lead_rem = coeffs_rem.back();
                            SymExpr const_factor = simplifyCore(lead_D / lead_rem);
                            remaining_N = simplifyCore(expand(remaining_N / const_factor, 500));
                            
                            std::vector<std::pair<SymExpr, SymExpr>> partialFractions;
                            for (size_t i = 0; i < factors.size(); ++i) {
                                if (i == factors.size() - 1) {
                                    partialFractions.push_back({remaining_N, remaining_D});
                                    break;
                                }
                                SymExpr D1 = simplifyCore(expand(factors[i].first ^ SymExpr(BigInt(factors[i].second)), 500));
                                SymExpr D2(BigInt(1));
                                for (size_t j = i + 1; j < factors.size(); ++j) {
                                    D2 = simplifyCore(expand(D2 * (factors[j].first ^ SymExpr(BigInt(factors[j].second))), 500));
                                }
                                
                                try {
                                    auto [gcd, S, T] = polyEGCD(D1, D2, var);
                                    SymExpr N_T = simplifyCore(expand((remaining_N * T) / gcd, 500));
                                    SymExpr N_S = simplifyCore(expand((remaining_N * S) / gcd, 500));
                                    
                                    auto [q1, r1] = polyDiv(N_T, D1, var);
                                    partialFractions.push_back({r1, D1});
                                    
                                    remaining_N = simplifyCore(expand(N_S + q1 * D2, 500));
                                    remaining_D = D2;
                                } catch (const std::runtime_error& e) {
                                    throw std::runtime_error(std::string("Calculus Error: Partial fraction decomposition failed. ") + e.what());
                                }
                            }
                            
                            SymExpr polyPart(BigInt(0));
                            for (size_t i = 0; i < partialFractions.size(); ++i) {
                                SymExpr curr_N = partialFractions[i].first;
                                SymExpr base_D = factors[i].first;
                                int k = factors[i].second;
                                
                                for (int j = k; j >= 1; --j) {
                                    try {
                                        auto [q, r] = polyDiv(curr_N, base_D, var);
                                        res = res + integratePartialFraction(r, base_D, j, var);
                                        curr_N = q;
                                    } catch (const std::runtime_error& e) {
                                        throw std::runtime_error(std::string("Calculus Error: Integration of partial fraction failed. ") + e.what());
                                    }
                                }
                                polyPart = polyPart + curr_N;
                            }
                            if (!polyPart.isZero()) {
                                res = res + doInteg(polyPart, depth);
                            }
                        }
                    }
                    return coeff * res;
                }
            }

            // --- 4. 万能公式换元 (Weierstrass Substitution) ---
            bool hasTrig = false;
            std::function<bool(const std::shared_ptr<SymNode>&)> isRationalTrig = [&](const std::shared_ptr<SymNode>& node) -> bool {
                if (!node) return true;
                switch (node->getType()) {
                    case SymType::NUM: return true;
                    case SymType::VAR: return std::static_pointer_cast<SymVar>(node)->name != var;
                    case SymType::ADD:
                        for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args)
                            if (!isRationalTrig(arg)) return false;
                        return true;
                    case SymType::MUL:
                        for (auto& arg : std::static_pointer_cast<SymMul>(node)->args)
                            if (!isRationalTrig(arg)) return false;
                        return true;
                    case SymType::POW: {
                        auto powNode = std::static_pointer_cast<SymPow>(node);
                        if (!isRationalTrig(powNode->base)) return false;
                        if (containsVar(powNode->exp, var)) return false;
                        return true;
                    }
                    case SymType::FUNC: {
                        auto func = std::static_pointer_cast<SymFunc>(node);
                        if (func->args.size() == 1 && func->args[0]->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(func->args[0])->name == var) {
                            if (func->name == "sin" || func->name == "cos" || func->name == "tan") {
                                hasTrig = true;
                                return true;
                            }
                            return false;
                        }
                        for (auto& arg : func->args)
                            if (containsVar(arg, var)) return false;
                        return true;
                    }
                }
                return false;
            };

            if (isRationalTrig(varPart.ptr) && hasTrig) {
                std::string t_var = "_weierstrass_t";
                SymExpr t = SymExpr::makeVar(t_var);
                SymExpr sin_sub = (SymExpr(BigInt(2)) * t) / (SymExpr(BigInt(1)) + t * t);
                SymExpr cos_sub = (SymExpr(BigInt(1)) - t * t) / (SymExpr(BigInt(1)) + t * t);
                SymExpr tan_sub = (SymExpr(BigInt(2)) * t) / (SymExpr(BigInt(1)) - t * t);
                SymExpr dt_sub = SymExpr(BigInt(2)) / (SymExpr(BigInt(1)) + t * t);

                std::function<SymExpr(const std::shared_ptr<SymNode>&)> applyWeierstrass = [&](const std::shared_ptr<SymNode>& node) -> SymExpr {
                    if (!node) return SymExpr(BigInt(0));
                    switch (node->getType()) {
                        case SymType::NUM: return SymExpr(node);
                        case SymType::VAR: return SymExpr(node);
                        case SymType::ADD: {
                            SymExpr res(BigInt(0));
                            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) res = res + applyWeierstrass(arg);
                            return res;
                        }
                        case SymType::MUL: {
                            SymExpr res(BigInt(1));
                            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) res = res * applyWeierstrass(arg);
                            return res;
                        }
                        case SymType::POW: {
                            auto powNode = std::static_pointer_cast<SymPow>(node);
                            return applyWeierstrass(powNode->base) ^ applyWeierstrass(powNode->exp);
                        }
                        case SymType::FUNC: {
                            auto func = std::static_pointer_cast<SymFunc>(node);
                            if (func->args.size() == 1 && func->args[0]->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(func->args[0])->name == var) {
                                if (func->name == "sin") return sin_sub;
                                if (func->name == "cos") return cos_sub;
                                if (func->name == "tan") return tan_sub;
                            }
                            std::vector<std::shared_ptr<SymNode>> newArgs;
                            for (auto& arg : func->args) newArgs.push_back(applyWeierstrass(arg).ptr);
                            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
                        }
                    }
                    return SymExpr(node);
                };

                try {
                    SymExpr subbed = applyWeierstrass(varPart.ptr) * dt_sub;
                    auto [num_t, den_t] = getFraction(subbed);
                    
                    SymExpr gcd_t = polyGCD(num_t, den_t, t_var);
                    if (!gcd_t.isOne() && !gcd_t.isZero()) {
                        num_t = polyDiv(num_t, gcd_t, t_var).first;
                        den_t = polyDiv(den_t, gcd_t, t_var).first;
                    }
                    
                    SymExpr rational_t = simplifyCore(expand(num_t / den_t, 500));
                    SymExpr rational_var = subs(rational_t, t_var, SymExpr::makeVar(var));
                    SymExpr integrated_var = doInteg(rational_var, depth + 1);
                    SymExpr back_sub = SymExpr(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{(SymExpr::makeVar(var) / SymExpr(BigInt(2))).ptr}));
                    return coeff * simplifyCore(subs(integrated_var, var, back_sub));
                } catch (...) {}
            }

            throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
        };

        auto tryInteg = [&]() -> SymExpr {
            try {
                return simplify(doInteg(expr, 0));
            } catch (...) {
                SymExpr expanded;
                try { expanded = expand(expr, 1000); } catch (...) { expanded = expr; }
                if (expanded.toString() != expr.toString()) {
                    return simplify(doInteg(expanded, 0));
                }
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            }
        };

        return tryInteg();
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
        while (changed) {
            changed = false;
            for (const auto& rule : rules) {
                SymExpr next = applyRule(current, rule.first, rule.second);
                if (next.ptr != current.ptr) {
                    SymExpr simplifiedNext = simplifyCore(next);
                    if (simplifiedNext.toString() != current.toString()) {
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
