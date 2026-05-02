#include "Integration.h"
#include "SymRules.h"
#include "Factorization.h"
#include "Groebner.h"
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <optional>
#include <unordered_set>

namespace jc {

    thread_local std::unordered_set<std::string> g_taintedSignatures;

    static void markTainted(const SymExpr& expr) {
        if (!expr.ptr) return;
        g_taintedSignatures.insert(expr.ptr->getSignature());
        switch (expr.ptr->getType()) {
            case SymType::ADD:
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) markTainted(SymExpr(arg));
                break;
            case SymType::MUL:
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) markTainted(SymExpr(arg));
                break;
            case SymType::POW:
                markTainted(SymExpr(std::static_pointer_cast<SymPow>(expr.ptr)->base));
                markTainted(SymExpr(std::static_pointer_cast<SymPow>(expr.ptr)->exp));
                break;
            case SymType::FUNC:
                for (auto& arg : std::static_pointer_cast<SymFunc>(expr.ptr)->args) markTainted(SymExpr(arg));
                break;
            default: break;
        }
    }

    static bool isTainted(const SymExpr& expr) {
        if (!expr.ptr) return false;
        if (g_taintedSignatures.count(expr.ptr->getSignature())) return true;
        switch (expr.ptr->getType()) {
            case SymType::ADD:
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) if (isTainted(SymExpr(arg))) return true;
                break;
            case SymType::MUL:
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) if (isTainted(SymExpr(arg))) return true;
                break;
            case SymType::POW:
                if (isTainted(SymExpr(std::static_pointer_cast<SymPow>(expr.ptr)->base))) return true;
                if (isTainted(SymExpr(std::static_pointer_cast<SymPow>(expr.ptr)->exp))) return true;
                break;
            case SymType::FUNC:
                for (auto& arg : std::static_pointer_cast<SymFunc>(expr.ptr)->args) if (isTainted(SymExpr(arg))) return true;
                break;
            default: break;
        }
        return false;
    }

    // =================================================================
    // Rothstein-Trager 算法 (Rothstein-Trager Algorithm)
    // 用于积分 A/D，其中 D 无平方，deg(A) < deg(D)
    // =================================================================
    static SymExpr rothsteinTrager(SymExpr A, SymExpr D, const std::string& var) {
        // 智能拦截：基于 Galois 群扩展度的 Rothstein-Trager 智能拦截
        auto sqFree = polySquareFree(D, var);
        for (const auto& p : sqFree) {
            if (getDegree(p.first, var) > 4) {
                SymExpr factored = factor(p.first);
                auto checkDegree = [&](const SymExpr& f) {
                    if (getDegree(f, var) > 4) {
                        throw std::runtime_error("Non-integrable rational function: irreducible denominator factor degree exceeds solvability.");
                    }
                };
                if (factored.ptr->getType() == SymType::MUL) {
                    for (auto& arg : std::static_pointer_cast<SymMul>(factored.ptr)->args) checkDegree(SymExpr(arg));
                } else if (factored.ptr->getType() == SymType::POW) {
                    checkDegree(SymExpr(std::static_pointer_cast<SymPow>(factored.ptr)->base));
                } else {
                    checkDegree(factored);
                }
            }
        }

        SymExpr z = SymExpr::makeVar("_z");
        SymExpr dD = diff(D, var);
        SymExpr P = simplifyCore(expand(A - z * dD, SymConfig::maxExpandTerms));
        
        // 确保 polyResultant 的第一个参数次数 >= 第二个参数
        SymExpr R_z;
        if (getDegree(D, var) >= getDegree(P, var)) {
            R_z = polyResultant(D, P, var);
        } else {
            R_z = polyResultant(P, D, var);
        }
        R_z = getFraction(R_z).first; // 提取分子，消除伪除法引入的 z 分母
        
        std::vector<SymExpr> roots = solveEq(R_z, "_z");
        if (roots.empty()) {
            if (R_z.isZero() || getDegree(R_z, "_z") > 0) {
                throw std::runtime_error("Calculus Error: Rothstein-Trager failed to find roots of the resultant.");
            }
            return SymExpr(BigInt(0));
        }
        
        // 使用 Gröbner 基在 Q[x, _z] 上计算通用的 GCD (避免代数数运算失效)
        std::vector<MultiPoly> generators;
        generators.push_back(MultiPoly(P));
        generators.push_back(MultiPoly(D));
        generators.push_back(MultiPoly(R_z));
        auto gb = computeGroebnerBasis(generators);
        
        SymExpr universal_gcd(BigInt(1));
        for (const auto& poly : gb) {
            SymExpr p_expr = poly.toSymExpr();
            if (containsVar(p_expr.ptr, var)) {
                if (universal_gcd.isOne() || getDegree(p_expr, var) < getDegree(universal_gcd, var)) {
                    universal_gcd = p_expr;
                }
            }
        }

        SymExpr result(BigInt(0));
        std::map<std::string, std::vector<SymExpr>> rootOfGroups;
        
        for (const auto& root : roots) {
            if (root.ptr->getType() == SymType::FUNC) {
                auto func = std::static_pointer_cast<SymFunc>(root.ptr);
                if (func->name == "RootOf" && func->args.size() == 3) {
                    std::string polySig = func->args[0]->getSignature();
                    rootOfGroups[polySig].push_back(root);
                    continue;
                }
            }
            
            SymExpr P_i = simplifyCore(expand(subs(P, "_z", root), SymConfig::maxExpandTerms));
            SymExpr v_i;
            if (getDegree(P_i, var) == 1) {
                v_i = P_i; // 降次短路
            } else {
                SymExpr gb_vi = simplifyCore(expand(subs(universal_gcd, "_z", root), SymConfig::maxExpandTerms));
                if (!gb_vi.isOne() && !gb_vi.isZero() && containsVar(gb_vi.ptr, var)) {
                    v_i = gb_vi;
                } else {
                    v_i = polyGCD(P_i, D, var);
                }
            }
            
            auto coeffs = extractCoeffs(v_i, var);
            if (!coeffs.empty()) {
                SymExpr lead = coeffs.back();
                if (!lead.isZero() && !lead.isOne()) {
                    v_i = simplifyCore(expand(v_i / lead, SymConfig::maxExpandTerms));
                }
            }
            
            SymExpr log_vi(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v_i.ptr}));
            result = result + root * log_vi;
        }
        
        // 处理 RootOf 组，将其折叠为优雅的 RootSum
        for (const auto& [sig, group] : rootOfGroups) {
            if (group.empty()) continue;
            auto func = std::static_pointer_cast<SymFunc>(group[0].ptr);
            SymExpr poly(func->args[0]);
            SymExpr z_var(func->args[1]);
            
            // 构造 RootSum 内部表达式: _z * log(universal_gcd(x, _z))
            SymExpr v_z = universal_gcd;
            auto coeffs = extractCoeffs(v_z, var);
            if (!coeffs.empty()) {
                SymExpr lead = coeffs.back();
                if (!lead.isZero() && !lead.isOne()) {
                    v_z = simplifyCore(expand(v_z / lead, SymConfig::maxExpandTerms));
                }
            }
            
            SymExpr log_vz(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v_z.ptr}));
            SymExpr expr_z = z_var * log_vz;
            
            SymExpr rootSum(std::make_shared<SymFunc>("RootSum", std::vector<std::shared_ptr<SymNode>>{
                expr_z.ptr, z_var.ptr, poly.ptr
            }));
            
            result = result + rootSum;
        }
        
        SymExpr finalRes = simplifyCore(result);
        markTainted(finalRes);
        return finalRes;
    }

    // =================================================================
    // 🚀 Risch 算法核心组件 (Risch Algorithm Core Components)
    // =================================================================
    enum class RischExtType { LOG, EXP, ALG };

    struct RischExtension {
        RischExtType type = RischExtType::LOG;
        SymExpr arg;       // 扩张的参数 u (如 log(u) 或 exp(u) 中的 u)
        SymExpr deriv;     // 导数 t'
        std::string name;  // 变量名 (如 _t1)
        SymExpr t_var;     // 变量节点
        SymExpr minPoly;   // 极小多项式 (仅代数扩张使用，如 t^2 - P(x))
    };

    struct RischDiffField {
        std::string x_var;
        std::vector<RischExtension> tower;

        // 将表达式重写为微分域变量的代数式
        SymExpr rewrite(const SymExpr& expr) const {
            std::function<SymExpr(const SymExpr&)> preprocessPow = [&](const SymExpr& e) -> SymExpr {
                if (!e.ptr) return e;
                switch (e.ptr->getType()) {
                    case SymType::ADD: {
                        SymExpr r(BigInt(0));
                        for (auto& arg : std::static_pointer_cast<SymAdd>(e.ptr)->args) r = r + preprocessPow(SymExpr(arg));
                        return r;
                    }
                    case SymType::MUL: {
                        SymExpr r(BigInt(1));
                        for (auto& arg : std::static_pointer_cast<SymMul>(e.ptr)->args) r = r * preprocessPow(SymExpr(arg));
                        return r;
                    }
                    case SymType::POW: {
                        auto p = std::static_pointer_cast<SymPow>(e.ptr);
                        SymExpr base = preprocessPow(SymExpr(p->base));
                        SymExpr exp = preprocessPow(SymExpr(p->exp));
                        if (containsVar(exp.ptr, x_var)) {
                            SymExpr log_base(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{base.ptr}));
                            return SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(exp * log_base).ptr}));
                        }
                        if (exp.ptr->getType() == SymType::NUM) {
                            auto numVal = std::static_pointer_cast<SymNum>(exp.ptr)->value;
                            if (std::holds_alternative<Fraction>(numVal)) {
                                Fraction frac = std::get<Fraction>(numVal);
                                if (frac.getDen() > BigInt(1)) {
                                    SymExpr ext_arg = base ^ SymExpr(Fraction(BigInt(1), frac.getDen()));
                                    for (auto it = tower.rbegin(); it != tower.rend(); ++it) {
                                        if (it->type == RischExtType::ALG && it->arg == ext_arg) {
                                            return it->t_var ^ SymExpr(frac.getNum());
                                        }
                                    }
                                }
                            }
                        }
                        return base ^ exp;
                    }
                    case SymType::FUNC: {
                        auto f = std::static_pointer_cast<SymFunc>(e.ptr);
                        std::vector<std::shared_ptr<SymNode>> nArgs;
                        for (auto& arg : f->args) nArgs.push_back(preprocessPow(SymExpr(arg)).ptr);
                        return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                    }
                    default: return e;
                }
            };
            SymExpr res = preprocessPow(expr);
            
            // 从塔顶向下替换，确保嵌套扩张被正确处理 (如 exp(exp(x)))
            for (auto it = tower.rbegin(); it != tower.rend(); ++it) {
                if (it->type == RischExtType::LOG) {
                    SymExpr targetLog(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetLog, it->t_var);
                } else if (it->type == RischExtType::EXP) {
                    SymExpr targetExp(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetExp, it->t_var);
                } else if (it->type == RischExtType::ALG) {
                    res = applyRule(res, it->arg, it->t_var);
                }
            }
            return res;
        }
    };

    static void buildRischFieldRec(const SymExpr& expr, const std::string& x, RischDiffField& field, int& counter) {
        if (!expr.ptr) return;
        
        // 后序遍历：确保内层扩张先被加入塔中
        switch (expr.ptr->getType()) {
            case SymType::ADD:
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                break;
            case SymType::MUL:
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                break;
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                if (containsVar(p->exp, x)) {
                    SymExpr log_base(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{p->base}));
                    SymExpr exp_node(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(SymExpr(p->exp) * log_base).ptr}));
                    buildRischFieldRec(exp_node, x, field, counter);
                } else {
                    buildRischFieldRec(SymExpr(p->base), x, field, counter);
                    buildRischFieldRec(SymExpr(p->exp), x, field, counter);
                    
                    if (p->exp->getType() == SymType::NUM && containsVar(p->base, x)) {
                        auto numVal = std::static_pointer_cast<SymNum>(p->exp)->value;
                        if (std::holds_alternative<Fraction>(numVal)) {
                            Fraction frac = std::get<Fraction>(numVal);
                            if (frac.getDen() > BigInt(1)) {
                                SymExpr base(p->base);
                                SymExpr ext_arg = base ^ SymExpr(Fraction(BigInt(1), frac.getDen()));
                                
                                bool exists = false;
                                for (const auto& ext : field.tower) {
                                    if (ext.type == RischExtType::ALG && ext.arg == ext_arg) {
                                        exists = true;
                                        break;
                                    }
                                }
                                
                                if (!exists) {
                                    RischExtension ext;
                                    ext.type = RischExtType::ALG;
                                    ext.arg = ext_arg;
                                    ext.name = "_t" + std::to_string(++counter);
                                    ext.t_var = SymExpr::makeVar(ext.name);
                                    
                                    ext.minPoly = (ext.t_var ^ SymExpr(frac.getDen())) - base;
                                    
                                    SymExpr dP_dx = diff(ext.minPoly, x);
                                    SymExpr dP_dt = diff(ext.minPoly, ext.name);
                                    ext.deriv = simplifyCore(-dP_dx / dP_dt);
                                    
                                    field.tower.push_back(ext);
                                }
                            }
                        }
                    }
                }
                break;
            }
            case SymType::FUNC: {
                auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
                for (auto& arg : f->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                
                if (f->name == "log" || f->name == "exp") {
                    SymExpr arg(f->args[0]);
                    if (!containsVar(arg.ptr, x)) break; // 常数不构成扩张
                    
                    // 检查是否已在塔中
                    bool exists = false;
                    for (const auto& ext : field.tower) {
                        if ((ext.type == RischExtType::LOG && f->name == "log") ||
                            (ext.type == RischExtType::EXP && f->name == "exp")) {
                            if (ext.arg == arg) {
                                exists = true;
                                break;
                            }
                        }
                    }
                    
                    if (!exists) {
                        RischExtension ext;
                        ext.type = (f->name == "exp") ? RischExtType::EXP : RischExtType::LOG;
                        ext.arg = arg;
                        ext.name = "_t" + std::to_string(++counter);
                        ext.t_var = SymExpr::makeVar(ext.name);
                        
                        // 计算导数 t'
                        SymExpr du = diff(arg, x);
                        if (ext.type == RischExtType::LOG) {
                            ext.deriv = simplifyCore(du / arg);
                        } else {
                            ext.deriv = simplifyCore(du * ext.t_var);
                        }
                        
                        field.tower.push_back(ext);
                    }
                } else if (f->name == "RootOf" && f->args.size() == 3) {
                    SymExpr minPoly(f->args[0]);
                    SymExpr dummy(f->args[1]);
                    
                    if (!containsVar(minPoly.ptr, x)) break;
                    
                    bool exists = false;
                    for (const auto& ext : field.tower) {
                        if (ext.type == RischExtType::ALG && ext.arg == expr) {
                            exists = true;
                            break;
                        }
                    }
                    
                    if (!exists) {
                        RischExtension ext;
                        ext.type = RischExtType::ALG;
                        ext.arg = SymExpr(expr);
                        ext.name = "_t" + std::to_string(++counter);
                        ext.t_var = SymExpr::makeVar(ext.name);
                        
                        ext.minPoly = subs(minPoly, std::static_pointer_cast<SymVar>(dummy.ptr)->name, ext.t_var);
                        
                        SymExpr dP_dx = diff(ext.minPoly, x);
                        SymExpr dP_dt = diff(ext.minPoly, ext.name);
                        ext.deriv = simplifyCore(-dP_dx / dP_dt);
                        
                        field.tower.push_back(ext);
                    }
                }
                break;
            }
            default: break;
        }
    }

    static RischDiffField buildRischDifferentialField(const SymExpr& expr, const std::string& x) {
        RischDiffField field;
        field.x_var = x;
        int counter = 0;
        buildRischFieldRec(expr, x, field, counter);
        
        // 将导数重写为域变量的代数式
        for (auto& ext : field.tower) {
            ext.deriv = field.rewrite(ext.deriv);
        }
        return field;
    }

    // =================================================================
    // 🚀 Risch 算法 Step 2: Hermite 约化 (Hermite Reduction)
    // =================================================================
    static std::tuple<SymExpr, SymExpr, SymExpr, SymExpr> hermiteReduce(SymExpr A, SymExpr D, const std::string& var) {
        A = simplifyCore(expand(A, SymConfig::maxExpandTerms));
        D = simplifyCore(expand(D, SymConfig::maxExpandTerms));
        
        auto [Q, R] = polyDiv(A, D, var);
        SymExpr rationalPart(0);
        SymExpr polyPart = Q; 
        
        A = R;
        if (A.isZero()) return {rationalPart, polyPart, SymExpr(BigInt(0)), SymExpr(BigInt(1))};

        auto sqFree = polySquareFree(D, var);
        int m = 0;
        for (const auto& p : sqFree) m = std::max(m, p.second);

        SymExpr currentA = A;
        SymExpr currentD = D;

        for (int i = m; i >= 2; --i) {
            SymExpr V(1);
            for (auto& p : sqFree) {
                if (p.second == i) V = p.first;
            }
            if (getDegree(V, var) <= 0) continue;

            SymExpr Vi = simplifyCore(expand(V ^ SymExpr(i), SymConfig::maxExpandTerms));
            SymExpr U = polyDiv(currentD, Vi, var).first;

            SymExpr dV = diff(V, var);
            SymExpr U_dV = simplifyCore(expand(U * dV, SymConfig::maxExpandTerms));

            auto [gcd, S, T] = polyEGCD(U_dV, V, var);
            SymExpr factor = simplifyCore(expand(currentA / gcd, SymConfig::maxExpandTerms));
            SymExpr B = simplifyCore(expand(S * factor, SymConfig::maxExpandTerms));
            SymExpr C = simplifyCore(expand(T * factor, SymConfig::maxExpandTerms));

            auto [q, r] = polyDiv(B, V, var);
            B = r;
            C = simplifyCore(expand(C + q * U_dV, SymConfig::maxExpandTerms));

            SymExpr V_im1 = simplifyCore(expand(V ^ SymExpr(i - 1), SymConfig::maxExpandTerms));
            SymExpr denom = simplifyCore(expand(SymExpr(i - 1) * V_im1, SymConfig::maxExpandTerms));
            rationalPart = rationalPart + simplifyCore(expand(-B / denom, SymConfig::maxExpandTerms));

            SymExpr dB = diff(B, var);
            SymExpr term2 = simplifyCore(expand((dB * U) / SymExpr(i - 1), SymConfig::maxExpandTerms));
            currentA = simplifyCore(expand(C + term2, SymConfig::maxExpandTerms));
            
            currentD = simplifyCore(expand(U * V_im1, SymConfig::maxExpandTerms));
            
            for (auto& p : sqFree) {
                if (p.second == i) p.second = i - 1;
            }
        }
        
        return {rationalPart, polyPart, currentA, currentD};
    }

    // =================================================================
    // 三角函数与复指数双向转换 (Trig <-> Exp)
    // =================================================================
    static SymExpr trigToExp(const SymExpr& expr) {
        if (!expr.ptr) return expr;
        switch (expr.ptr->getType()) {
            case SymType::ADD: {
                SymExpr res(BigInt(0));
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) res = res + trigToExp(SymExpr(arg));
                return res;
            }
            case SymType::MUL: {
                SymExpr res(BigInt(1));
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) res = res * trigToExp(SymExpr(arg));
                return res;
            }
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                return trigToExp(SymExpr(p->base)) ^ trigToExp(SymExpr(p->exp));
            }
            case SymType::FUNC: {
                auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
                std::vector<std::shared_ptr<SymNode>> nArgs;
                for (auto& arg : f->args) nArgs.push_back(trigToExp(SymExpr(arg)).ptr);
                
                if (nArgs.size() == 1) {
                    SymExpr arg(nArgs[0]);
                    SymExpr I = SymExpr::makeVar("i");
                    if (f->name == "sin") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return (exp_ix - exp_mix) / (SymExpr(BigInt(2)) * I);
                    }
                    if (f->name == "cos") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return (exp_ix + exp_mix) / SymExpr(BigInt(2));
                    }
                    if (f->name == "tan") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return -I * (exp_ix - exp_mix) / (exp_ix + exp_mix);
                    }
                    if (f->name == "cot") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return I * (exp_ix + exp_mix) / (exp_ix - exp_mix);
                    }
                    if (f->name == "sec") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return SymExpr(BigInt(2)) / (exp_ix + exp_mix);
                    }
                    if (f->name == "csc") {
                        SymExpr exp_ix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(I * arg).ptr}));
                        SymExpr exp_mix(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-I * arg).ptr}));
                        return (SymExpr(BigInt(2)) * I) / (exp_ix - exp_mix);
                    }
                    if (f->name == "sinh") {
                        SymExpr exp_x(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                        SymExpr exp_mx(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-arg).ptr}));
                        return (exp_x - exp_mx) / SymExpr(BigInt(2));
                    }
                    if (f->name == "cosh") {
                        SymExpr exp_x(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                        SymExpr exp_mx(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-arg).ptr}));
                        return (exp_x + exp_mx) / SymExpr(BigInt(2));
                    }
                    if (f->name == "tanh") {
                        SymExpr exp_x(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                        SymExpr exp_mx(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(-arg).ptr}));
                        return (exp_x - exp_mx) / (exp_x + exp_mx);
                    }
                }
                return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
            }
            default: return expr;
        }
    }

    static SymExpr expToTrig(const SymExpr& expr) {
        if (!expr.ptr) return expr;
        switch (expr.ptr->getType()) {
            case SymType::ADD: {
                SymExpr res(BigInt(0));
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) res = res + expToTrig(SymExpr(arg));
                return res;
            }
            case SymType::MUL: {
                SymExpr res(BigInt(1));
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) res = res * expToTrig(SymExpr(arg));
                return res;
            }
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                return expToTrig(SymExpr(p->base)) ^ expToTrig(SymExpr(p->exp));
            }
            case SymType::FUNC: {
                auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
                std::vector<std::shared_ptr<SymNode>> nArgs;
                for (auto& arg : f->args) nArgs.push_back(expToTrig(SymExpr(arg)).ptr);
                
                if (f->name == "exp" && nArgs.size() == 1) {
                    SymExpr arg(nArgs[0]);
                    auto coeffs = extractCoeffs(arg, "i");
                    if (coeffs.size() == 2) {
                        SymExpr A = coeffs[0];
                        SymExpr B = coeffs[1];
                        SymExpr cos_B(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                        SymExpr sin_B(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{B.ptr}));
                        SymExpr I = SymExpr::makeVar("i");
                        SymExpr trig_part = cos_B + I * sin_B;
                        if (A.isZero()) return trig_part;
                        SymExpr exp_A(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{A.ptr}));
                        return exp_A * trig_part;
                    }
                }
                return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
            }
            default: return expr;
        }
    }

    // =================================================================
    // 🚀 Risch 算法核心 (Risch Algorithm Core)
    // =================================================================
    static SymExpr rischIntegrateCore(const SymExpr& expr, const std::string& var, int depth) {
        // Step 0 - 刘维尔域规范化 (Liouvillian Field Normalization)
        SymExpr normalizedExpr = rischNormalize(expr);

        // Step 1 - 构建微分域与刘维尔扩张
        RischDiffField field = buildRischDifferentialField(normalizedExpr, var);
        SymExpr rewritten = field.rewrite(normalizedExpr);
        
        // Step 1.5 - 单一扩张的变量代换降维打击 (Change of Variables for Single Extension)
        if (field.tower.size() == 1) {
            auto ext = field.tower[0];
            
            // 形式 1: 积分恰好是 F(t) * t' dx = F(t) dt
            SymExpr F_t = simplifyCore(rewritten / ext.deriv);
            if (!containsVar(F_t.ptr, var)) {
                try {
                    SymExpr int_t = integrate(F_t, ext.name, depth + 1);
                    SymExpr orig_ext;
                    if (ext.type == RischExtType::LOG) {
                        orig_ext = SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                    } else if (ext.type == RischExtType::EXP) {
                        orig_ext = SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                    } else if (ext.type == RischExtType::ALG) {
                        orig_ext = ext.arg;
                    }
                    return simplifyCore(subs(int_t, ext.name, orig_ext));
                } catch (...) {}
            }
            
            // 形式 2: 积分是 F(e^(ax+b)) dx，且 F 中不含 x
            if (ext.type == RischExtType::EXP && !containsVar(rewritten.ptr, var)) {
                SymExpr u_deriv = simplifyCore(diff(ext.arg, var));
                if (!containsVar(u_deriv.ptr, var) && !u_deriv.isZero()) {
                    try {
                        SymExpr integrand_t = simplifyCore(rewritten / (u_deriv * ext.t_var));
                        SymExpr int_t = integrate(integrand_t, ext.name, depth + 1);
                        SymExpr orig_ext(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                        return simplifyCore(subs(int_t, ext.name, orig_ext));
                    } catch (...) {}
                }
            }
        }

        // Step 2 - Hermite 约化 (目前仅针对有理函数域 Q(x))
        if (field.tower.empty()) {
            auto [num, den] = getFraction(rewritten);
            if (getDegree(den, var) > 0) {
                auto [ratPart, polyPart, cA, cD] = hermiteReduce(num, den, var);
            
                SymExpr intPart = polyPart;
                if (!cA.isZero()) intPart = intPart + simplifyCore(expand(cA / cD, SymConfig::maxExpandTerms));
                
                if (intPart.isZero()) return ratPart;
                
                try {
                    if (intPart == normalizedExpr) throw std::runtime_error("Loop");
                    if (getAstNodeCount(intPart) >= getAstNodeCount(normalizedExpr)) throw std::runtime_error("Complexity increased");
                    SymExpr intRes = integrate(intPart, var, depth + 1);
                    return ratPart + intRes;
                } catch (const std::exception& e) {
                    std::string debugInfo = "Hermite Reduction completed, but remaining integral is non-elementary.\n";
                    debugInfo += "Original: " + normalizedExpr.toString() + "\n";
                    debugInfo += "Rational Part: " + ratPart.toString() + "\n";
                    debugInfo += "Remaining Integral: \\int (" + intPart.toString() + ") d" + var + "\n";
                    debugInfo += "Reason: " + std::string(e.what()) + "\n";
                    throw std::runtime_error("Calculus Error: " + debugInfo);
                } catch (...) {
                    std::string debugInfo = "Hermite Reduction completed, but remaining integral is non-elementary.\n";
                    debugInfo += "Original: " + normalizedExpr.toString() + "\n";
                    debugInfo += "Rational Part: " + ratPart.toString() + "\n";
                    debugInfo += "Remaining Integral: \\int (" + intPart.toString() + ") d" + var + "\n";
                    throw std::runtime_error("Calculus Error: " + debugInfo);
                }
            }
        }
        
        // Step 4 & 5 - 扩张多项式积分 (Extension Polynomial Integration)
        if (!field.tower.empty()) {
            auto topExt = field.tower.back();
            
            auto backSubstitute = [&](SymExpr res) {
                for (auto it = field.tower.rbegin(); it != field.tower.rend(); ++it) {
                    SymExpr orig;
                    if (it->type == RischExtType::LOG) {
                        orig = SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    } else if (it->type == RischExtType::EXP) {
                        orig = SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    } else if (it->type == RischExtType::ALG) {
                        orig = it->arg;
                    }
                    res = subs(res, it->name, orig);
                }
                return res;
            };

            if (topExt.type == RischExtType::LOG) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    std::vector<SymExpr> B(n + 2, SymExpr(BigInt(0)));
                    SymExpr t_deriv = topExt.deriv;
                    SymExpr t_sym = SymExpr::makeVar(topExt.name);

                    try {
                        SymExpr intAn_x = backSubstitute(coeffs[n]);
                        if (getAstNodeCount(intAn_x) > getAstNodeCount(expr) * 3 + 50) {
                            throw std::runtime_error("Risch Step 4: Leading coefficient expansion too large, aborting.");
                        }
                        SymExpr intAn = integrate(intAn_x, var, depth + 1);
                        SymExpr intAn_t = field.rewrite(trigToExp(intAn));
                        auto coeffs_intAn = extractCoeffs(intAn_t, topExt.name);
                        
                        SymExpr c(BigInt(0));
                        SymExpr B_n(BigInt(0));
                        if (coeffs_intAn.empty()) {
                            if (!intAn_t.isZero()) throw std::runtime_error("Non-elementary integral.");
                        } else if (coeffs_intAn.size() > 1) {
                            c = simplifyCore(coeffs_intAn[1] / SymExpr(BigInt(n + 1)));
                            B_n = coeffs_intAn[0];
                            if (coeffs_intAn.size() > 2) {
                                throw std::runtime_error("Non-elementary integral.");
                            }
                            if (containsVar(c.ptr, var)) {
                                throw std::runtime_error("Non-elementary integral: residue is not constant.");
                            }
                        } else {
                            B_n = coeffs_intAn[0];
                        }
                        
                        B[n + 1] = c;
                        B[n] = B_n;
                        
                        for (int i = n - 1; i >= 0; --i) {
                            SymExpr integrand = simplifyCore(coeffs[i] - SymExpr(BigInt(i + 1)) * B[i + 1] * t_deriv);
                            SymExpr integrand_x = backSubstitute(integrand);
                            if (getAstNodeCount(integrand_x) > getAstNodeCount(expr) * 3 + 50) {
                                throw std::runtime_error("Risch Step 4: Lower coefficient expansion too large, aborting.");
                            }
                            SymExpr int_i = integrate(integrand_x, var, depth + 1);
                            SymExpr int_i_t = field.rewrite(trigToExp(int_i));
                            
                            auto coeffs_int_i = extractCoeffs(int_i_t, topExt.name);
                            if (coeffs_int_i.empty()) {
                                if (!int_i_t.isZero()) throw std::runtime_error("Non-elementary integral.");
                                B[i] = SymExpr(BigInt(0));
                            } else if (coeffs_int_i.size() > 2) {
                                throw std::runtime_error("Non-elementary integral.");
                            } else if (coeffs_int_i.size() == 2) {
                                SymExpr c_i = coeffs_int_i[1];
                                if (containsVar(c_i.ptr, var)) {
                                    throw std::runtime_error("Non-elementary integral: residue is not constant.");
                                }
                                B[i + 1] = simplifyCore(B[i + 1] + c_i / SymExpr(BigInt(i + 1)));
                                B[i] = coeffs_int_i[0];
                            } else {
                                B[i] = coeffs_int_i[0];
                            }
                        }
                        
                        SymExpr result(BigInt(0));
                        for (int i = 0; i <= n + 1; ++i) {
                            if (!B[i].isZero()) {
                                result = result + B[i] * (t_sym ^ SymExpr(BigInt(i)));
                            }
                        }
                        SymExpr finalRes = simplifyCore(backSubstitute(result));
                        markTainted(finalRes);
                        return finalRes;
                    } catch (...) {
                        // 失败则继续往下走，抛出调试信息
                    }
                }
            } else if (topExt.type == RischExtType::EXP) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    SymExpr u_deriv = simplifyCore(topExt.deriv / SymExpr::makeVar(topExt.name));
                    SymExpr t_sym = SymExpr::makeVar(topExt.name);
                    
                    bool success = true;
                    SymExpr result(BigInt(0));
                    
                    for (int i = 0; i <= n; ++i) {
                        if (coeffs[i].isZero()) continue;
                        if (i == 0) {
                            try {
                                SymExpr integrand_x = backSubstitute(coeffs[0]);
                                if (getAstNodeCount(integrand_x) > getAstNodeCount(expr) * 3 + 50) {
                                    throw std::runtime_error("Risch Step 5: Constant term expansion too large, aborting.");
                                }
                                SymExpr int_0 = integrate(integrand_x, var, depth + 1);
                                result = result + field.rewrite(trigToExp(int_0));
                            } catch (...) {
                                success = false;
                                break;
                            }
                        } else {
                            // 求解 Risch 微分方程 B_i' + i u' B_i = A_i (SPDE 多项式度数界限法)
                            SymExpr A_i = backSubstitute(coeffs[i]);
                            SymExpr V_i = backSubstitute(simplifyCore(SymExpr(BigInt(i)) * u_deriv));
                            
                            SymExpr B_i(BigInt(0));
                            SymExpr currentA = A_i;
                            bool rde_success = false;
                            
                            auto cV = extractCoeffs(V_i, var);
                            while (!cV.empty() && cV.back().isZero()) cV.pop_back();
                            
                            if (cV.empty()) {
                                // V_i 不是多项式，退化为常数除法尝试
                                SymExpr B_term = simplifyCore(currentA / V_i);
                                if (!containsVar(B_term.ptr, var)) {
                                    B_i = B_term;
                                    rde_success = true;
                                }
                            } else {
                                int degV = static_cast<int>(cV.size()) - 1;
                                SymExpr leadV = cV.back();
                                
                                // 计算多项式度数界限 b = deg(A) - deg(V)
                                int b_bound = -1;
                                auto cA_init = extractCoeffs(currentA, var);
                                while (!cA_init.empty() && cA_init.back().isZero()) cA_init.pop_back();
                                
                                if (!cA_init.empty()) {
                                    b_bound = static_cast<int>(cA_init.size()) - 1 - degV;
                                }
                                
                                if (b_bound >= 0) {
                                    for (int d = b_bound; d >= 0; --d) {
                                        if (currentA.isZero()) break;
                                        
                                        auto cA = extractCoeffs(currentA, var);
                                        while (!cA.empty() && cA.back().isZero()) cA.pop_back();
                                        if (cA.empty()) break;
                                        
                                        int degA = static_cast<int>(cA.size()) - 1;
                                        
                                        if (degA != d + degV) {
                                            if (degA > d + degV) break; // 异常升次，无多项式解
                                            continue; // 当前次项系数为0，跳过
                                        }
                                        
                                        SymExpr leadA = cA.back();
                                        SymExpr c_d = simplifyCore(leadA / leadV);
                                        SymExpr B_term = simplifyCore(c_d * (SymExpr::makeVar(var) ^ SymExpr(BigInt(d))));
                                        
                                        B_i = B_i + B_term;
                                        SymExpr B_term_deriv = simplifyCore(diff(B_term, var));
                                        currentA = simplifyCore(expand(currentA - B_term_deriv - V_i * B_term, SymConfig::maxExpandTerms));
                                    }
                                }
                                
                                if (currentA.isZero()) {
                                    rde_success = true;
                                } else if (degV == 0) {
                                    auto cA_final = extractCoeffs(currentA, var);
                                    while (!cA_final.empty() && cA_final.back().isZero()) cA_final.pop_back();
                                    if (cA_final.empty()) {
                                        // 兜底常数项处理
                                        SymExpr B_term = simplifyCore(currentA / V_i);
                                        if (!containsVar(B_term.ptr, var)) {
                                            B_i = B_i + B_term;
                                            rde_success = true;
                                        }
                                    }
                                }
                            }
                            
                            if (rde_success) {
                                SymExpr B_i_t = field.rewrite(B_i);
                                result = result + B_i_t * (t_sym ^ SymExpr(BigInt(i)));
                            } else {
                                success = false;
                                break;
                            }
                        }
                    }
                    
                    if (success) {
                        SymExpr finalRes = simplifyCore(backSubstitute(result));
                        markTainted(finalRes);
                        return finalRes;
                    }
                }
            } else if (topExt.type == RischExtType::ALG) {
                // Trager 算法 (1984) - 代数曲线伪化简 (Pseudo-reduction)
                SymExpr minPoly = topExt.minPoly;
                SymExpr t_sym = SymExpr::makeVar(topExt.name);
                
                auto [A, D] = getFraction(rewritten);
                
                // 1. 分母有理化 (Rationalize Denominator)
                auto [gcd_D, U, V] = polyEGCD(D, minPoly, topExt.name);
                SymExpr res_D = simplifyCore(expand(U * D + V * minPoly, SymConfig::maxExpandTerms));
                
                if (!res_D.isZero() && !containsVar(res_D.ptr, topExt.name)) {
                    SymExpr newA = simplifyCore(expand(A * U, SymConfig::maxExpandTerms));
                    SymExpr newD = res_D;
                    
                    // 2. 伪化简 (Pseudo-reduction) 降次
                    auto [Q, R] = polyDiv(newA, minPoly, topExt.name);
                    SymExpr A_red = R;
                    SymExpr D_red = newD;
                    
                    // 3. 构造 Trager 结式 (Trager's Resultant) 提取对数留数
                    SymExpr z = SymExpr::makeVar("_z");
                    SymExpr dP_dx = diff(minPoly, var);
                    SymExpr dP_dt = diff(minPoly, topExt.name);
                    
                    // Trager 核心方程: z * D_red * dP/dx - A_red * dP/dt
                    SymExpr trager_poly = simplifyCore(expand(z * D_red * dP_dx - A_red * dP_dt, SymConfig::maxExpandTerms));
                    trager_poly = getFraction(trager_poly).first; // 提前清除分母，防止结式计算时系数膨胀
                    SymExpr R_z = polyResultant(minPoly, trager_poly, topExt.name);
                    
                    // 提取分子，消除伪除法引入的分母
                    R_z = getFraction(R_z).first;
                    
                    std::set<std::string> vars_in_Rz;
                    collectAllVars(R_z.ptr, vars_in_Rz);
                    vars_in_Rz.erase("_z");
                    
                    SymExpr gcd_z(BigInt(0));
                    if (vars_in_Rz.empty()) {
                        gcd_z = R_z;
                    } else {
                        for (int pt = 1; pt <= 5; ++pt) {
                            SymExpr eval_Rz = R_z;
                            for (const auto& v : vars_in_Rz) {
                                eval_Rz = simplifyCore(subs(eval_Rz, v, SymExpr(BigInt(pt))));
                            }
                            eval_Rz = getFraction(eval_Rz).first;
                            if (eval_Rz.isZero()) continue;
                            if (gcd_z.isZero()) gcd_z = eval_Rz;
                            else gcd_z = polyGCD(gcd_z, eval_Rz, "_z");
                            if (gcd_z.isOne()) break;
                        }
                    }
                    
                    std::vector<SymExpr> roots;
                    if (!gcd_z.isZero() && getDegree(gcd_z, "_z") > 0) {
                        roots = solveEq(gcd_z, "_z");
                    }
                    
                    if (!roots.empty()) {
                        bool allConstant = true;
                        for (const auto& root : roots) {
                            if (containsVar(root.ptr, var)) {
                                allConstant = false;
                                break;
                            }
                        }
                        if (allConstant) {
                            SymExpr result(BigInt(0));
                            for (const auto& root : roots) {
                                SymExpr v_i = simplifyCore(expand(subs(trager_poly, "_z", root), SymConfig::maxExpandTerms));
                                
                                // 降次短路与首一化
                                auto coeffs_vi = extractCoeffs(v_i, topExt.name);
                                if (!coeffs_vi.empty()) {
                                    SymExpr lead = coeffs_vi.back();
                                    if (!lead.isZero() && !lead.isOne()) {
                                        v_i = simplifyCore(expand(v_i / lead, SymConfig::maxExpandTerms));
                                    }
                                }
                                
                                SymExpr log_vi(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v_i.ptr}));
                                result = result + root * log_vi;
                            }
                            SymExpr finalRes = simplifyCore(backSubstitute(result));
                            markTainted(finalRes);
                            return finalRes;
                        }
                    }
                }
            }
        }

        // 调试输出：展示构建的微分域
        std::string debugInfo = "Risch Field over Q(" + var + "):\n";
        bool isElliptic = false;
        bool isHyperElliptic = false;
        
        for (const auto& ext : field.tower) {
            debugInfo += "  " + ext.name + " = ";
            if (ext.type == RischExtType::LOG) {
                debugInfo += "log(" + ext.arg.toString() + ")";
            } else if (ext.type == RischExtType::EXP) {
                debugInfo += "exp(" + ext.arg.toString() + ")";
            } else if (ext.type == RischExtType::ALG) {
                debugInfo += "RootOf(" + ext.minPoly.toString() + " = 0)";
                int degX = getDegree(ext.minPoly, var);
                int degT = getDegree(ext.minPoly, ext.name);
                if (degT == 2 && degX >= 3) {
                    // 提取 P(x) 并检查是否无平方 (Square-free check)
                    SymExpr P_neg = subs(ext.minPoly, ext.name, SymExpr(BigInt(0)));
                    auto sqFree = polySquareFree(P_neg, var);
                    bool hasMultipleRoots = false;
                    for (const auto& factor : sqFree) {
                        if (factor.second > 1 && getDegree(factor.first, var) > 0) {
                            hasMultipleRoots = true;
                            break;
                        }
                    }
                    if (!hasMultipleRoots) {
                        if (degX == 3 || degX == 4) isElliptic = true;
                        else if (degX >= 5) isHyperElliptic = true;
                    }
                }
            }
            debugInfo += ",  " + ext.name + "' = " + ext.deriv.toString() + "\n";
        }
        debugInfo += "Rewritten integrand: " + rewritten.toString();
        
        if (isElliptic) {
            throw std::runtime_error("Calculus Error: Integral is non-elementary (Elliptic curve of genus g=1 detected).\n" + debugInfo);
        } else if (isHyperElliptic) {
            throw std::runtime_error("Calculus Error: Integral is non-elementary (Hyperelliptic curve of genus g>1 detected).\n" + debugInfo);
        }
        
        throw std::runtime_error("Calculus Error: Integral is non-elementary or requires advanced Risch steps.\n" + debugInfo);
    }

    // =================================================================
    // 🚀 Risch 算法入口 (Risch Algorithm Entry Point)
    // =================================================================
    SymExpr rischIntegrate(const SymExpr& expr, const std::string& var, int depth) {
        if (depth > SymConfig::maxDepth) {
            throw std::runtime_error("Integration depth limit exceeded in Risch algorithm.");
        }

        if (getAstNodeCount(expr) > 300) {
            throw std::runtime_error("Integration AST size limit exceeded in Risch algorithm.");
        }

        // Step -1 - 三角函数转复指数 (Trig to Exp)
        SymExpr expExpr = trigToExp(expr);

        SymExpr res = rischIntegrateCore(expExpr, var, depth);

        // Step 6 - 复指数转回三角函数 (Exp to Trig)
        SymExpr trigRes = expToTrig(res);
        return simplify(trigRes);
    }

    // =================================================================
    // 🚀 符号积分 (Symbolic Integration) - 基础多项式与初等函数
    // =================================================================
    SymExpr integrate(const SymExpr& expr, const std::string& var, int start_depth) {
        if (start_depth == 0) {
            g_taintedSignatures.clear();
        }

        if (!expr.ptr) return expr;

        SymExpr x = SymExpr::makeVar(var);

        // 从规则库获取静态积分规则
        const auto& rules = getStaticIntegRules();

        std::unordered_map<std::string, std::optional<SymExpr>> integCache;
        std::function<std::optional<SymExpr>(const SymExpr&, int)> doInteg;

        int baseVarDepth = getVarDepth(expr, var);
        int baseTransWeight = getTranscendentalWeight(expr, var);

        auto doIntegImpl = [&](const SymExpr& e, int current_depth) -> std::optional<SymExpr> {
            if (current_depth > SymConfig::maxDepth) {
                return std::nullopt;
            }

            // 积分变量下沉检测 (Variable Submergence Check)
            int currentVarDepth = getVarDepth(e, var);
            if (currentVarDepth > baseVarDepth + 5) {
                return std::nullopt;
            }

            // 超越函数嵌套惩罚 (Transcendental Extension Penalty)
            int currentTransWeight = getTranscendentalWeight(e, var);
            if (currentTransWeight > baseTransWeight + 20) {
                return std::nullopt;
            }

            // 绝对体积限制 (Absolute AST Size Limit)
            if (getAstNodeCount(e) > 300) {
                return std::nullopt;
            }

            // 积分路径污染机制的黑盒锁 (Integration Path Taint Lock)
            if (isTainted(e)) {
                return std::nullopt;
            }

            if (!containsVar(e.ptr, var)) {
                return e * x; // ∫ c dx = c * x
            }

            if (e.ptr->getType() == SymType::ADD) {
                auto add = std::static_pointer_cast<SymAdd>(e.ptr);
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) {
                    auto partRes = doInteg(SymExpr(arg), current_depth);
                    if (!partRes) return std::nullopt;
                    res = res + *partRes;
                }
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
                    // 积分规则中的 _x 必须精确匹配当前的积分变量 var
                    auto it_x = captures.find("_x");
                    if (it_x == captures.end() || it_x->second.ptr->getType() != SymType::VAR || 
                        std::static_pointer_cast<SymVar>(it_x->second.ptr)->name != var) {
                        continue;
                    }
                    
                    // 积分规则中的 _n, _a 必须是相对于积分变量的常数
                    bool validMatch = true;
                    for (const auto& cap : captures) {
                        if ((cap.first == "_n" || cap.first == "_a") && containsVar(cap.second.ptr, var)) {
                            validMatch = false;
                            break;
                        }
                    }
                    if (validMatch) {
                        integratedPart = substituteCaptures(rule.second, captures);
                        matched = true;
                        break;
                    }
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
                    SymExpr u_base(powNode->base);
                    SymExpr du_base = simplifyCore(diff(u_base, var));
                    if (!du_base.isZero() && !containsVar(du_base.ptr, var)) {
                        if (u_base.ptr->getType() != SymType::VAR) {
                            out_u = u_base;
                            out_a = du_base;
                            return true;
                        }
                    }
                    SymExpr u_exp(powNode->exp);
                    SymExpr du_exp = simplifyCore(diff(u_exp, var));
                    if (!du_exp.isZero() && !containsVar(du_exp.ptr, var)) {
                        if (u_exp.ptr->getType() != SymType::VAR) {
                            out_u = u_exp;
                            out_a = du_exp;
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
                    if (expr_node == sub_u) return u_sym;
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
                    SymExpr f_var = subs(f_u, u_var, SymExpr::makeVar(var));
                    if (auto int_var = doInteg(simplify(expand(f_var / sub_a, SymConfig::maxExpandTerms)), current_depth + 1)) {
                        SymExpr res = subs(*int_var, var, sub_u);
                        return coeff * res;
                    }
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
                        if (expr_node == u) return u_sym;
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
                                rem_u = simplify(expand(replaceXn(rem_u), SymConfig::maxExpandTerms));
                            }
                        }
                    }

                    if (!containsVar(rem_u.ptr, var)) {
                        SymExpr f_var = subs(rem_u, u_var, SymExpr::makeVar(var));
                        if (auto int_var = doInteg(simplify(expand(f_var, SymConfig::maxExpandTerms)), current_depth + 1)) {
                            SymExpr res = subs(*int_var, var, u);
                            return coeff * res;
                        }
                    }
                }
            }

            // --- 1.6 根式整体换元 (Radical Substitution) ---
            SymExpr radicalBase;
            int radicalN = 0;
            bool foundRadical = false;
            std::function<void(const std::shared_ptr<SymNode>&)> findRadical = [&](const std::shared_ptr<SymNode>& node) {
                if (!node || foundRadical) return;
                if (node->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(node);
                    if (powNode->exp->getType() == SymType::NUM) {
                        auto numVal = std::static_pointer_cast<SymNum>(powNode->exp)->value;
                        if (std::holds_alternative<Fraction>(numVal)) {
                            Fraction frac = std::get<Fraction>(numVal);
                            if (frac.getDen() > BigInt(1) && containsVar(powNode->base, var)) {
                                radicalBase = SymExpr(powNode->base);
                                radicalN = static_cast<int>(frac.getDen().toDouble());
                                foundRadical = true;
                                return;
                            }
                        }
                    }
                }
                if (node->getType() == SymType::ADD) {
                    for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) findRadical(arg);
                } else if (node->getType() == SymType::MUL) {
                    for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) findRadical(arg);
                } else if (node->getType() == SymType::FUNC) {
                    for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) findRadical(arg);
                }
            };
            findRadical(varPart.ptr);

            if (foundRadical && radicalN >= 2) {
                auto invertFunction = [&](SymExpr f, SymExpr target, std::string v) -> std::optional<SymExpr> {
                    int iters = 0;
                    while (f.ptr->getType() != SymType::VAR && iters++ < 10) {
                        if (f.ptr->getType() == SymType::ADD) {
                            auto add = std::static_pointer_cast<SymAdd>(f.ptr);
                            SymExpr withV(BigInt(0)), withoutV(BigInt(0));
                            int countV = 0;
                            for (auto& arg : add->args) {
                                if (containsVar(arg, v)) { withV = SymExpr(arg); countV++; }
                                else withoutV = withoutV + SymExpr(arg);
                            }
                            if (countV != 1) return std::nullopt;
                            target = target - withoutV;
                            f = withV;
                        } else if (f.ptr->getType() == SymType::MUL) {
                            auto mul = std::static_pointer_cast<SymMul>(f.ptr);
                            SymExpr withV(BigInt(1)), withoutV(BigInt(1));
                            int countV = 0;
                            for (auto& arg : mul->args) {
                                if (containsVar(arg, v)) { withV = SymExpr(arg); countV++; }
                                else withoutV = withoutV * SymExpr(arg);
                            }
                            if (countV != 1) return std::nullopt;
                            target = target / withoutV;
                            f = withV;
                        } else if (f.ptr->getType() == SymType::POW) {
                            auto powNode = std::static_pointer_cast<SymPow>(f.ptr);
                            bool baseHasV = containsVar(powNode->base, v);
                            bool expHasV = containsVar(powNode->exp, v);
                            if (baseHasV && !expHasV) {
                                target = target ^ (SymExpr(BigInt(1)) / SymExpr(powNode->exp));
                                f = SymExpr(powNode->base);
                            } else if (!baseHasV && expHasV) {
                                SymExpr log_target(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                                SymExpr log_base(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{powNode->base}));
                                target = log_target / log_base;
                                f = SymExpr(powNode->exp);
                            } else {
                                return std::nullopt;
                            }
                        } else if (f.ptr->getType() == SymType::FUNC) {
                            auto func = std::static_pointer_cast<SymFunc>(f.ptr);
                            if (func->args.size() != 1) return std::nullopt;
                            SymExpr arg(func->args[0]);
                            if (func->name == "sin") target = SymExpr(std::make_shared<SymFunc>("asin", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "cos") target = SymExpr(std::make_shared<SymFunc>("acos", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "tan") target = SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "asin") target = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "acos") target = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "atan") target = SymExpr(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "exp") target = SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "log") target = SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "sinh") target = SymExpr(std::make_shared<SymFunc>("asinh", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "cosh") target = SymExpr(std::make_shared<SymFunc>("acosh", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else if (func->name == "tanh") target = SymExpr(std::make_shared<SymFunc>("atanh", std::vector<std::shared_ptr<SymNode>>{target.ptr}));
                            else return std::nullopt;
                            f = arg;
                        } else {
                            return std::nullopt;
                        }
                    }
                    if (f.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(f.ptr)->name == v) {
                        return target;
                    }
                    return std::nullopt;
                };

                std::string u_var = "_u_rad";
                SymExpr u_sym = SymExpr::makeVar(u_var);
                SymExpr target = u_sym ^ SymExpr(BigInt(radicalN));
                
                if (auto x_sol = invertFunction(radicalBase, target, var)) {
                    SymExpr dx_du = simplify(diff(*x_sol, u_var));
                    if (!dx_du.isZero()) {
                        SymExpr new_integrand = simplify(subs(varPart, var, *x_sol) * dx_du);
                        SymExpr integrand_var = subs(new_integrand, u_var, SymExpr::makeVar(var));
                        if (auto int_var = doInteg(integrand_var, current_depth + 1)) {
                            SymExpr back_sub = radicalBase ^ SymExpr(Fraction(BigInt(1), BigInt(radicalN)));
                            return coeff * simplify(subs(*int_var, var, back_sub));
                        }
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
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), SymConfig::maxExpandTerms);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                if (auto res = doInteg(expanded, current_depth + 1)) return coeff * (*res);
                            } else {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr cosSq = SymExpr(BigInt(1)) - (_C ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (cosSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, SymConfig::maxExpandTerms);
                                SymExpr cosx = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_C", cosx);
                                if (auto res = doInteg(expanded, current_depth + 1)) return coeff * (*res);
                            }
                        } else if (func->name == "cos") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) + _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), SymConfig::maxExpandTerms);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                if (auto res = doInteg(expanded, current_depth + 1)) return coeff * (*res);
                            } else {
                                SymExpr _S = SymExpr::makeVar("_S");
                                SymExpr sinSq = SymExpr(BigInt(1)) - (_S ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (sinSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, SymConfig::maxExpandTerms);
                                SymExpr sinx = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_S", sinx);
                                if (auto res = doInteg(expanded, current_depth + 1)) return coeff * (*res);
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
                        SymExpr subbed = simplifyCore(subs(varPart, var, x_sub) * dx_sub);
                        SymExpr subbed_var = subs(subbed, "_t", SymExpr::makeVar(var));
                        if (auto int_var = doInteg(trigsimp(subbed_var), current_depth + 1)) {
                            SymExpr res = subs(*int_var, var, t_back);
                            return coeff * res;
                        }
                    }
                }
            }

            // --- 1.94 双二次分式积分 (Biquadratic Fraction) ---
            if (varPart.ptr->getType() == SymType::MUL || varPart.ptr->getType() == SymType::POW) {
                SymExpr bqBase;
                int bqM = 0;
                bool isBq = false;
                
                if (varPart.ptr->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                    if (powNode->exp->getType() == SymType::NUM) {
                        auto [isIntExp, expVal] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                        if (isIntExp && expVal == -1) {
                            bqBase = SymExpr(powNode->base);
                            bqM = 0;
                            isBq = true;
                        }
                    }
                } else if (varPart.ptr->getType() == SymType::MUL) {
                    auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                    if (mul->args.size() == 2) {
                        SymExpr arg1(mul->args[0]);
                        SymExpr arg2(mul->args[1]);
                        auto checkBq = [&](const SymExpr& p1, const SymExpr& p2) {
                            if (p2.ptr->getType() == SymType::POW) {
                                auto powNode = std::static_pointer_cast<SymPow>(p2.ptr);
                                if (powNode->exp->getType() == SymType::NUM) {
                                    auto [isIntExp, expVal] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                                    if (isIntExp && expVal == -1) {
                                        if (p1.ptr->getType() == SymType::POW) {
                                            auto p1Pow = std::static_pointer_cast<SymPow>(p1.ptr);
                                            if (p1Pow->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p1Pow->base)->name == var) {
                                                if (p1Pow->exp->getType() == SymType::NUM) {
                                                    auto [isMInt, mVal] = extractExactInt(std::static_pointer_cast<SymNum>(p1Pow->exp)->value);
                                                    if (isMInt && (mVal == 2)) {
                                                        bqBase = SymExpr(powNode->base);
                                                        bqM = 2;
                                                        isBq = true;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        };
                        checkBq(arg1, arg2);
                        if (!isBq) checkBq(arg2, arg1);
                    }
                }

                if (isBq) {
                    auto coeffs = extractCoeffs(bqBase, var);
                    if (coeffs.size() == 5 && !coeffs[4].isZero() && coeffs[3].isZero() && coeffs[2].isZero() && coeffs[1].isZero() && !coeffs[0].isZero()) {
                        SymExpr A = coeffs[4];
                        SymExpr B = coeffs[0];
                        
                        auto isPos = [](const SymExpr& e) {
                            if (e.ptr->getType() == SymType::NUM) return !isCasNegative(std::static_pointer_cast<SymNum>(e.ptr)->value) && !e.isZero();
                            return false;
                        };
                        
                        if (isPos(A) && isPos(B)) {
                            SymExpr C = simplifyCore((B / A) ^ SymExpr(Fraction(1, 2)));
                            SymExpr sqrt2C = simplifyCore((SymExpr(BigInt(2)) * C) ^ SymExpr(Fraction(1, 2)));
                            SymExpr sqrtC_half = simplifyCore((C / SymExpr(BigInt(2))) ^ SymExpr(Fraction(1, 2)));
                            
                            SymExpr x_sym = SymExpr::makeVar(var);
                            SymExpr x2 = x_sym * x_sym;
                            
                            SymExpr log_term1 = simplifyCore(x2 - sqrt2C * x_sym + C);
                            SymExpr log_term2 = simplifyCore(x2 + sqrt2C * x_sym + C);
                            
                            SymExpr atan_arg1 = simplifyCore((x_sym - sqrtC_half) / sqrtC_half);
                            SymExpr atan_arg2 = simplifyCore((x_sym + sqrtC_half) / sqrtC_half);
                            
                            SymExpr atan1(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atan_arg1.ptr}));
                            SymExpr atan2(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atan_arg2.ptr}));
                            SymExpr log1(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{log_term1.ptr}));
                            SymExpr log2(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{log_term2.ptr}));
                            
                            if (bqM == 2) {
                                SymExpr part1 = simplifyCore(SymExpr(BigInt(1)) / (SymExpr(BigInt(4)) * A * sqrt2C));
                                SymExpr part2 = simplifyCore(SymExpr(BigInt(1)) / (SymExpr(BigInt(2)) * A * sqrt2C));
                                SymExpr res = part1 * (log1 - log2) + part2 * (atan1 + atan2);
                                return coeff * res;
                            } else if (bqM == 0) {
                                SymExpr part1 = simplifyCore(SymExpr(BigInt(1)) / (SymExpr(BigInt(4)) * A * C * sqrt2C));
                                SymExpr part2 = simplifyCore(SymExpr(BigInt(1)) / (SymExpr(BigInt(2)) * A * C * sqrt2C));
                                SymExpr res = part1 * (log2 - log1) + part2 * (atan1 + atan2);
                                return coeff * res;
                            }
                        }
                    }
                }
            }

            // --- 1.95 高次二项式分式积分 (Binomial Fraction Integration) ---
            if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isIntExp, expVal] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isIntExp && expVal == -1) {
                        auto coeffs = extractCoeffs(SymExpr(powNode->base), var);
                        if (coeffs.size() > 2) {
                            bool isBinomial = true;
                            int n = static_cast<int>(coeffs.size()) - 1;
                            for (int i = 1; i < n; ++i) {
                                if (!coeffs[i].isZero()) { isBinomial = false; break; }
                            }
                            if (isBinomial && !coeffs[n].isZero() && !coeffs[0].isZero()) {
                                SymExpr A = coeffs[n];
                                SymExpr B = coeffs[0];
                                
                                SymExpr C = simplifyCore(A / B);
                                bool isPos = false;
                                bool isNeg = false;
                                if (C.ptr->getType() == SymType::NUM) {
                                    isNeg = isCasNegative(std::static_pointer_cast<SymNum>(C.ptr)->value);
                                    isPos = !isNeg && !C.isZero();
                                } else if (C.ptr->getType() == SymType::MUL) {
                                    auto mul = std::static_pointer_cast<SymMul>(C.ptr);
                                    if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                        isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                                        isPos = !isNeg;
                                    } else {
                                        isPos = true;
                                    }
                                } else {
                                    isPos = true;
                                }
                                
                                SymExpr absC = isNeg ? simplifyCore(-C) : C;
                                SymExpr u_coeff = simplifyCore(absC ^ SymExpr(Fraction(1, n)));
                                SymExpr u = simplifyCore(u_coeff * SymExpr::makeVar(var));
                                SymExpr du_dx = u_coeff;
                                
                                SymExpr n_sym = SymExpr(BigInt(n));
                                SymExpr res(BigInt(0));
                                SymExpr PI = SymExpr::makeVar("PI");
                                
                                auto makeCos = [&](SymExpr theta) {
                                    return SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{theta.ptr}));
                                };
                                auto makeSin = [&](SymExpr theta) {
                                    return SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{theta.ptr}));
                                };
                                auto makeLog = [&](SymExpr arg) {
                                    return SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                };
                                auto makeAtan = [&](SymExpr arg) {
                                    return SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                };
                                
                                if (isPos) {
                                    if (n % 2 != 0) {
                                        res = res + (SymExpr(BigInt(1)) / n_sym) * makeLog(u + SymExpr(BigInt(1)));
                                        for (int k = 1; k <= (n - 1) / 2; ++k) {
                                            SymExpr theta = simplifyCore((SymExpr(BigInt(2 * k - 1)) * PI) / n_sym);
                                            SymExpr cos_th = makeCos(theta);
                                            SymExpr sin_th = makeSin(theta);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_th + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_th) / sin_th);
                                            res = res - (cos_th / n_sym) * makeLog(log_arg) + (SymExpr(BigInt(2)) * sin_th / n_sym) * makeAtan(atan_arg);
                                        }
                                    } else {
                                        for (int k = 1; k <= n / 2; ++k) {
                                            SymExpr theta = simplifyCore((SymExpr(BigInt(2 * k - 1)) * PI) / n_sym);
                                            SymExpr cos_th = makeCos(theta);
                                            SymExpr sin_th = makeSin(theta);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_th + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_th) / sin_th);
                                            res = res - (cos_th / n_sym) * makeLog(log_arg) + (SymExpr(BigInt(2)) * sin_th / n_sym) * makeAtan(atan_arg);
                                        }
                                    }
                                } else {
                                    SymExpr res_minus(BigInt(0));
                                    if (n % 2 != 0) {
                                        res_minus = res_minus + (SymExpr(BigInt(1)) / n_sym) * makeLog(u - SymExpr(BigInt(1)));
                                        for (int k = 1; k <= (n - 1) / 2; ++k) {
                                            SymExpr phi = simplifyCore((SymExpr(BigInt(2 * k)) * PI) / n_sym);
                                            SymExpr cos_ph = makeCos(phi);
                                            SymExpr sin_ph = makeSin(phi);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_ph + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_ph) / sin_ph);
                                            res_minus = res_minus + (cos_ph / n_sym) * makeLog(log_arg) - (SymExpr(BigInt(2)) * sin_ph / n_sym) * makeAtan(atan_arg);
                                        }
                                    } else {
                                        res_minus = res_minus + (SymExpr(BigInt(1)) / n_sym) * makeLog(u - SymExpr(BigInt(1))) - (SymExpr(BigInt(1)) / n_sym) * makeLog(u + SymExpr(BigInt(1)));
                                        for (int k = 1; k <= n / 2 - 1; ++k) {
                                            SymExpr phi = simplifyCore((SymExpr(BigInt(2 * k)) * PI) / n_sym);
                                            SymExpr cos_ph = makeCos(phi);
                                            SymExpr sin_ph = makeSin(phi);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_ph + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_ph) / sin_ph);
                                            res_minus = res_minus + (cos_ph / n_sym) * makeLog(log_arg) - (SymExpr(BigInt(2)) * sin_ph / n_sym) * makeAtan(atan_arg);
                                        }
                                    }
                                    res = -res_minus;
                                }
                                
                                return coeff * simplifyCore(res / (B * du_dx));
                            }
                        }
                    }
                }
            }

            // --- 1.98 二次配方法启发式 (Inverse Quadratic Integration) ---
            auto [num, den] = getFraction(varPart);
            bool isFraction = !den.isOne();

            if (isFraction && !containsVar(num.ptr, var)) {
                auto coeffs = extractCoeffs(den, var);
                if (coeffs.size() == 3 && !coeffs[2].isZero()) {
                    SymExpr A = coeffs[2];
                    SymExpr B = coeffs[1];
                    SymExpr C = coeffs[0];
                    
                    SymExpr Delta = simplifyCore((B ^ SymExpr(BigInt(2))) - SymExpr(BigInt(4)) * A * C);
                    
                    bool isDeltaNeg = false;
                    if (Delta.ptr->getType() == SymType::NUM) {
                        isDeltaNeg = isCasNegative(std::static_pointer_cast<SymNum>(Delta.ptr)->value);
                    } else if (Delta.ptr->getType() == SymType::MUL) {
                        auto mul = std::static_pointer_cast<SymMul>(Delta.ptr);
                        if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                            isDeltaNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                        }
                    }
                    
                    if (isDeltaNeg) {
                        SymExpr negDelta = simplifyCore(-Delta);
                        SymExpr sqrtNegDelta = simplifyCore(negDelta ^ SymExpr(Fraction(1, 2)));
                        
                        SymExpr atan_arg = simplifyCore((SymExpr(BigInt(2)) * A * SymExpr::makeVar(var) + B) / sqrtNegDelta);
                        SymExpr atan_term = SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atan_arg.ptr}));
                        
                        SymExpr res = simplifyCore((SymExpr(BigInt(2)) * num / sqrtNegDelta) * atan_term);
                        return coeff * res;
                    }
                }
            }

            // --- 2. 有理分式积分引擎 (Rational Function Integration) ---
            if (isFraction && containsVar(den.ptr, var)) {
                // 参数化常数提取与隔离黑盒 (Symbolic Constant Blackboxing)
                std::map<std::string, SymExpr> constDict;
                int constCounter = 0;
                std::function<SymExpr(const SymExpr&)> blackboxConstants = [&](const SymExpr& node) -> SymExpr {
                    if (!node.ptr) return node;
                    if (!containsVar(node.ptr, var)) {
                        if (getAstNodeCount(node) > 5) {
                            std::string cName = "_C_box" + std::to_string(++constCounter);
                            constDict[cName] = node;
                            return SymExpr::makeVar(cName);
                        }
                        return node;
                    }
                    switch (node.ptr->getType()) {
                        case SymType::ADD: {
                            SymExpr res(BigInt(0));
                            for (auto& arg : std::static_pointer_cast<SymAdd>(node.ptr)->args) res = res + blackboxConstants(SymExpr(arg));
                            return res;
                        }
                        case SymType::MUL: {
                            SymExpr res(BigInt(1));
                            for (auto& arg : std::static_pointer_cast<SymMul>(node.ptr)->args) res = res * blackboxConstants(SymExpr(arg));
                            return res;
                        }
                        case SymType::POW: {
                            auto p = std::static_pointer_cast<SymPow>(node.ptr);
                            return blackboxConstants(SymExpr(p->base)) ^ blackboxConstants(SymExpr(p->exp));
                        }
                        case SymType::FUNC: {
                            auto f = std::static_pointer_cast<SymFunc>(node.ptr);
                            std::vector<std::shared_ptr<SymNode>> nArgs;
                            for (auto& arg : f->args) nArgs.push_back(blackboxConstants(SymExpr(arg)).ptr);
                            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                        }
                        default: return node;
                    }
                };

                SymExpr boxed_num = blackboxConstants(num);
                SymExpr boxed_den = blackboxConstants(den);

                SymExpr num_expanded = simplifyCore(expand(boxed_num, SymConfig::maxExpandTerms));
                SymExpr den_expanded = simplifyCore(expand(boxed_den, SymConfig::maxExpandTerms));
                
                int degN = getDegree(num_expanded, var);
                int degD = getDegree(den_expanded, var);
                
                // ★ 只有当分子和分母都是多项式时，才使用有理分式积分引擎
                if (degD >= 0 && (degN >= 0 || !containsVar(num_expanded.ptr, var))) {
                    try {
                        auto [ratPart, polyPart, cA, cD] = hermiteReduce(num_expanded, den_expanded, var);
                        SymExpr res = ratPart;
                        
                        if (!polyPart.isZero()) {
                            SymExpr unboxed_poly = polyPart;
                            for (const auto& kv : constDict) unboxed_poly = subs(unboxed_poly, kv.first, kv.second);
                            if (auto polyInt = doInteg(unboxed_poly, current_depth)) {
                                res = res + *polyInt;
                            } else {
                                throw std::runtime_error("polyPart integration failed");
                            }
                        }
                        
                        if (!cA.isZero()) {
                            res = res + rothsteinTrager(cA, cD, var);
                        }
                        
                        for (const auto& kv : constDict) {
                            res = subs(res, kv.first, kv.second);
                        }
                        
                        return coeff * res;
                    } catch (...) {
                        // 有理分式积分失败，静默吞没，继续尝试后续方法（如 Risch 算法）
                    }
                }
            }

            // --- 3. 万能公式换元 (Weierstrass Substitution) ---
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
                        if (powNode->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                            if (!isInt) return false;
                        } else {
                            return false;
                        }
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
                    
                    SymExpr rational_t = simplifyCore(expand(num_t / den_t, SymConfig::maxExpandTerms));
                    SymExpr rational_var = subs(rational_t, t_var, SymExpr::makeVar(var));
                    if (auto opt_integrated_var = doInteg(rational_var, current_depth + 1)) {
                        SymExpr back_sub = SymExpr(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{(SymExpr::makeVar(var) / SymExpr(BigInt(2))).ptr}));
                        return coeff * simplifyCore(subs(*opt_integrated_var, var, back_sub));
                    }
                } catch (...) {}
            }

            // --- 4. 启发式分部积分 (Integration by Parts) ---
            bool tryParts = false;
            std::vector<SymExpr> factors;
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                for (auto& arg : mul->args) factors.push_back(SymExpr(arg));
                tryParts = true;
            } else if (varPart.ptr->getType() == SymType::FUNC || varPart.ptr->getType() == SymType::POW) {
                bool isGoodForIBP = false;
                if (varPart.ptr->getType() == SymType::FUNC) {
                    auto fn = std::static_pointer_cast<SymFunc>(varPart.ptr);
                    if (fn->name == "log" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") isGoodForIBP = true;
                } else if (varPart.ptr->getType() == SymType::POW) {
                    auto p = std::static_pointer_cast<SymPow>(varPart.ptr);
                    if (p->base->getType() == SymType::FUNC) {
                        auto fn = std::static_pointer_cast<SymFunc>(p->base);
                        if (fn->name == "log" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") isGoodForIBP = true;
                    }
                }
                if (isGoodForIBP) {
                    factors.push_back(varPart);
                    factors.push_back(SymExpr(BigInt(1)));
                    tryParts = true;
                }
            }

            bool hasTranscendental = false;
            std::function<void(const std::shared_ptr<SymNode>&)> checkTrans = [&](const std::shared_ptr<SymNode>& node) {
                if (!node || hasTranscendental) return;
                if (node->getType() == SymType::FUNC) {
                    auto func = std::static_pointer_cast<SymFunc>(node);
                    if (func->name != "sqrt" && func->name != "cbrt" && func->name != "root" && func->name != "RootOf" && func->name != "RootSum") {
                        hasTranscendental = true;
                        return;
                    }
                }
                if (node->getType() == SymType::ADD) {
                    for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) checkTrans(arg);
                } else if (node->getType() == SymType::MUL) {
                    for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) checkTrans(arg);
                } else if (node->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(node);
                    checkTrans(powNode->base);
                    checkTrans(powNode->exp);
                } else if (node->getType() == SymType::FUNC) {
                    for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) checkTrans(arg);
                }
            };
            checkTrans(varPart.ptr);
            
            if (tryParts && !hasTranscendental) {
                tryParts = false;
            }

            if (tryParts) {
                auto getPriority = [&](const SymExpr& f) -> int {
                    if (f.ptr->getType() == SymType::FUNC) {
                        auto fn = std::static_pointer_cast<SymFunc>(f.ptr);
                        if (fn->name == "log" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") return 1;
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

                auto getIbpCandidates = [&](const std::vector<SymExpr>& facs, const std::vector<size_t>& idxs) {
                    std::vector<std::pair<SymExpr, SymExpr>> cands;
                    auto addCand = [&](SymExpr u, SymExpr dv) {
                        for (const auto& c : cands) if (c.first == u && c.second == dv) return;
                        cands.push_back({u, dv});
                    };
                    // 1. 选取优先级最高（最适合求导）的 1~2 个因子作为 u
                    for (size_t k = 0; k < std::min<size_t>(2, idxs.size()); ++k) {
                        size_t i = idxs[k];
                        SymExpr u = facs[i];
                        SymExpr dv(BigInt(1));
                        for (size_t j = 0; j < facs.size(); ++j) if (j != i) dv = dv * facs[j];
                        addCand(u, dv);
                    }
                    // 2. 选取优先级最低（最适合积分）的 1~2 个因子作为 dv
                    for (size_t k = 0; k < std::min<size_t>(2, idxs.size()); ++k) {
                        size_t i = idxs[idxs.size() - 1 - k];
                        SymExpr dv = facs[i];
                        SymExpr u(BigInt(1));
                        for (size_t j = 0; j < facs.size(); ++j) if (j != i) u = u * facs[j];
                        addCand(u, dv);
                    }
                    return cands;
                };

                auto tryPartsWith = [&](SymExpr u, SymExpr dv) -> std::optional<SymExpr> {
                    SymExpr du = simplifyCore(diff(u, var));
                    if (du.isZero()) {
                        return std::nullopt; // 剪枝：常数求导为0，分部积分无意义
                    }

                    // 导数膨胀率探测 (Derivative Swell Metric)
                    if (getAstNodeCount(du) >= 2.5 * getAstNodeCount(u)) {
                        return std::nullopt;
                    }

                    auto opt_v = doInteg(dv, current_depth + 1);
                    if (!opt_v) return std::nullopt;
                    SymExpr v = *opt_v;

                    // 剪枝：如果积分出来的 v 极其复杂（体积膨胀过大），及时止损
                    if (getAstNodeCount(v) > getAstNodeCount(dv) * 4 + 10) {
                        return std::nullopt;
                    }

                    SymExpr v_du = simplifyCore(v * du);
                    
                    // 循环分部积分检测 (1-step cycle)
                    SymExpr ratio1 = simplifyCore(v_du / varPart);
                    if (!containsVar(ratio1.ptr, var) && !ratio1.isZero()) {
                        SymExpr k = ratio1;
                        SymExpr one_plus_k = simplifyCore(SymExpr(BigInt(1)) + k);
                        if (!one_plus_k.isZero()) {
                            return coeff * simplify(expand((u * v) / one_plus_k, SymConfig::maxExpandTerms));
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
                        
                        auto candidates2 = getIbpCandidates(factors2, indices2);
                        int ibp2_attempts = 0;
                        for (const auto& cand2 : candidates2) {
                            if (current_depth > SymConfig::maxDepth / 2 && ibp2_attempts > 0) {
                                break;
                            }
                            ibp2_attempts++;
                            SymExpr u2_inner = cand2.first;
                            SymExpr dv2_inner = cand2.second;
                            SymExpr du2_inner = simplifyCore(diff(u2_inner, var));
                            if (du2_inner.isZero()) continue;

                            // 导数膨胀率探测 (Derivative Swell Metric)
                            if (getAstNodeCount(du2_inner) >= 2.5 * getAstNodeCount(u2_inner)) continue;

                            auto opt_v2_inner = doInteg(dv2_inner, current_depth + 2);
                            if (opt_v2_inner) {
                                SymExpr v2_inner = *opt_v2_inner;
                                SymExpr v2_du2 = simplifyCore(v2_inner * du2_inner);
                                
                                SymExpr ratio2 = simplifyCore(v2_du2 / varPart);
                                if (!containsVar(ratio2.ptr, var) && !ratio2.isZero()) {
                                    SymExpr k = ratio2;
                                    SymExpr one_minus_k = simplifyCore(SymExpr(BigInt(1)) - k);
                                    if (!one_minus_k.isZero()) {
                                        return coeff * simplifyCore((u * v - u2_inner * v2_inner) / one_minus_k);
                                    }
                                }
                            }
                        }
                    }

                    auto opt_int_v_du = doInteg(v_du, current_depth + 1);
                    if (!opt_int_v_du) return std::nullopt;
                    return coeff * simplifyCore(u * v - *opt_int_v_du);
                };

                auto candidates = getIbpCandidates(factors, indices);
                int ibp_attempts = 0;
                for (const auto& cand : candidates) {
                    if (current_depth > SymConfig::maxDepth / 2 && ibp_attempts > 0) {
                        break;
                    }
                    ibp_attempts++;
                    if (auto res = tryPartsWith(cand.first, cand.second)) return *res;
                }
            }

            // 启发式方法全部失效，移交 Risch 算法处理
            try {
                return coeff * rischIntegrate(varPart, var, current_depth + 1);
            } catch (...) {
                // 如果是在递归深层，静默返回 nullopt 让上层继续尝试其他分支
                // 如果是在最外层 (current_depth == start_depth)，则抛出异常以保留详细的 Risch 失败信息
                if (current_depth == start_depth) throw;
                return std::nullopt;
            }
        };

        doInteg = [&](const SymExpr& e, int current_depth) -> std::optional<SymExpr> {
            if (!e.ptr) return std::nullopt;
            std::string sig = e.ptr->getSignature();
            auto it = integCache.find(sig);
            if (it != integCache.end()) return it->second;
            
            auto res = doIntegImpl(e, current_depth);
            integCache[sig] = res;
            return res;
        };

        auto tryInteg = [&]() -> SymExpr {
            try {
                if (auto res = doInteg(expr, start_depth)) {
                    return simplify(*res);
                }
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            } catch (const std::runtime_error& e) {
                std::string msg = e.what();
                SymExpr expanded;
                try { expanded = expand(expr, SymConfig::maxExpandTerms * 2); } catch (...) { expanded = expr; }
                
                // 严格防死锁：只有当展开后的表达式确实发生了变化，才允许重置 depth 再次尝试
                if (expanded != expr) {
                    try {
                        if (auto res2 = doInteg(expanded, start_depth)) {
                            return simplify(*res2);
                        }
                    } catch (const std::runtime_error& e2) {
                        std::string msg2 = e2.what();
                        // 如果展开后再次失败，保留新的错误信息
                        if (msg2.find("Risch Field") != std::string::npos || 
                            msg2.find("Hermite Reduction") != std::string::npos ||
                            msg2.find("Integration depth limit exceeded") != std::string::npos ||
                            msg2.find("Calculus Error:") == 0) {
                            msg = msg2;
                        }
                    } catch (...) {}
                }
                
                // 如果是 Risch 算法抛出的进度信息，直接向外传递
                if (msg.find("Risch Field") != std::string::npos || 
                    msg.find("Hermite Reduction") != std::string::npos || 
                    msg.find("Integration depth limit exceeded") != std::string::npos) {
                    throw std::runtime_error(msg);
                }
                
                if (msg.find("Calculus Error:") == 0) {
                    throw std::runtime_error(msg);
                }
                
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power. (" + msg + ")");
            } catch (...) {
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            }
        };

        return tryInteg();
    }

    // =================================================================
    // 🚀 定积分 (Definite Integral) - 带有多值函数与支割线安全检查
    // =================================================================
    static bool hasMultiValuedFuncs(const SymExpr& expr) {
        if (!expr.ptr) return false;
        bool found = false;
        std::function<void(const std::shared_ptr<SymNode>&)> check = [&](const std::shared_ptr<SymNode>& node) {
            if (!node || found) return;
            if (node->getType() == SymType::FUNC) {
                auto func = std::static_pointer_cast<SymFunc>(node);
                if (func->name == "RootOf" || func->name == "RootSum") {
                    found = true;
                    return;
                }
            } else if (node->getType() == SymType::VAR) {
                auto varName = std::static_pointer_cast<SymVar>(node)->name;
                if (varName == "i" || varName == "I") {
                    found = true;
                    return;
                }
            }
            
            if (node->getType() == SymType::ADD) {
                for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) check(arg);
            } else if (node->getType() == SymType::MUL) {
                for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) check(arg);
            } else if (node->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(node);
                check(powNode->base);
                check(powNode->exp);
            } else if (node->getType() == SymType::FUNC) {
                for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) check(arg);
            }
        };
        check(expr.ptr);
        return found;
    }

    SymExpr defint(const SymExpr& expr, const std::string& var, const SymExpr& a, const SymExpr& b) {
        SymExpr antideriv = integrate(expr, var);
        if (hasMultiValuedFuncs(antideriv)) {
            throw std::runtime_error("Calculus Error: Definite integral cannot be evaluated safely due to multi-valued functions (e.g., RootOf, RootSum) or complex branch cuts in the antiderivative.");
        }
        return simplify(subs(antideriv, var, b) - subs(antideriv, var, a));
    }

} // namespace jc
