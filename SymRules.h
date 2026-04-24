#ifndef JC2_SYMRULES_H
#define JC2_SYMRULES_H

#include "Symbolic.h"
#include <vector>
#include <utility>
#include <string>

namespace jc {

    // =================================================================
    // 积分规则字典表 (动态生成，绑定特定的积分变量 var)
    // =================================================================
    inline std::vector<std::pair<SymExpr, SymExpr>> getIntegRules(const std::string& var) {
        SymExpr x = SymExpr::makeVar(var);
        SymExpr _n = SymExpr::makeVar("_n");

        auto func = [](const std::string& name, const SymExpr& arg) {
            return SymExpr(std::make_shared<SymFunc>(name, std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
        };

        return {
            // 幂函数与对数
            { x ^ SymExpr(-1), func("log", x) },
            { x ^ _n, (SymExpr(1) / (_n + SymExpr(1))) * (x ^ (_n + SymExpr(1))) },
            { x, (SymExpr(1) / SymExpr(2)) * (x ^ SymExpr(2)) },
            
            // 指数函数
            { func("exp", x), func("exp", x) },
            
            // 基础三角函数
            { func("sin", x), -func("cos", x) },
            { func("cos", x), func("sin", x) },
            { SymExpr(1) / (func("cos", x) ^ SymExpr(2)), func("tan", x) },
            { SymExpr(1) / (func("sin", x) ^ SymExpr(2)), -SymExpr(1) / func("tan", x) },
            
            // 三角函数平方
            { func("tan", x) ^ SymExpr(2), func("tan", x) - x },
            
            // 反三角函数导数逆运算
            { SymExpr(1) / (SymExpr(1) + (x ^ SymExpr(2))), func("atan", x) },
            { SymExpr(1) / ((SymExpr(1) - (x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))), func("asin", x) },
            
            // 双曲函数
            { func("sinh", x), func("cosh", x) },
            { func("cosh", x), func("sinh", x) },
            
            // 特殊函数 (误差函数与菲涅尔积分)
            { func("exp", -(x ^ SymExpr(2))), (SymExpr(Fraction(1, 2)) * (SymExpr::makeVar("PI") ^ SymExpr(Fraction(1, 2)))) * func("erf", x) },
            { func("sin", x ^ SymExpr(2)), ((SymExpr::makeVar("PI") / SymExpr(2)) ^ SymExpr(Fraction(1, 2))) * func("fresnel_s", ((SymExpr(2) / SymExpr::makeVar("PI")) ^ SymExpr(Fraction(1, 2))) * x) },
            { func("cos", x ^ SymExpr(2)), ((SymExpr::makeVar("PI") / SymExpr(2)) ^ SymExpr(Fraction(1, 2))) * func("fresnel_c", ((SymExpr(2) / SymExpr::makeVar("PI")) ^ SymExpr(Fraction(1, 2))) * x) },
            
            // 积分正弦、余弦与指数积分
            { func("sin", x) / x, func("Si", x) },
            { func("cos", x) / x, func("Ci", x) },
            { func("exp", x) / x, func("Ei", x) }
        };
    }

    // =================================================================
    // 三角化简规则字典表 (静态缓存，使用通配符 _x, _c)
    // =================================================================
    inline const std::vector<std::pair<SymExpr, SymExpr>>& getTrigRules() {
        static std::vector<std::pair<SymExpr, SymExpr>> rules;
        if (rules.empty()) {
            SymExpr _x = SymExpr::makeVar("_x");
            SymExpr _c = SymExpr::makeVar("_c");

            auto func = [](const std::string& name, const SymExpr& arg) {
                return SymExpr(std::make_shared<SymFunc>(name, std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
            };

            SymExpr sin_x = func("sin", _x);
            SymExpr cos_x = func("cos", _x);
            SymExpr tan_x = func("tan", _x);

            rules = {
                // 平方和恒等式
                { (sin_x ^ SymExpr(2)) + (cos_x ^ SymExpr(2)), SymExpr(1) },
                { _c * (sin_x ^ SymExpr(2)) + _c * (cos_x ^ SymExpr(2)), _c },
                
                // 平方差转换
                { SymExpr(1) - (sin_x ^ SymExpr(2)), cos_x ^ SymExpr(2) },
                { SymExpr(1) - (cos_x ^ SymExpr(2)), sin_x ^ SymExpr(2) },
                
                // 商数关系
                { sin_x / cos_x, tan_x },
                { cos_x / sin_x, SymExpr(1) / tan_x },
                { (sin_x ^ SymExpr(2)) / (cos_x ^ SymExpr(2)), tan_x ^ SymExpr(2) },
                { (cos_x ^ SymExpr(2)) / (sin_x ^ SymExpr(2)), SymExpr(1) / (tan_x ^ SymExpr(2)) },
                
                // 乘积与消去关系
                { tan_x * cos_x, sin_x },
                { sin_x / tan_x, cos_x },
                { tan_x / sin_x, SymExpr(1) / cos_x },
                
                // 割线与正切的平方关系 (1 + tan^2 = sec^2)
                { SymExpr(1) + (tan_x ^ SymExpr(2)), SymExpr(1) / (cos_x ^ SymExpr(2)) },
                { _c + _c * (tan_x ^ SymExpr(2)), _c / (cos_x ^ SymExpr(2)) },
                { (SymExpr(1) / (cos_x ^ SymExpr(2))) - (tan_x ^ SymExpr(2)), SymExpr(1) },
                { (SymExpr(1) / (cos_x ^ SymExpr(2))) - SymExpr(1), tan_x ^ SymExpr(2) }
            };
        }
        return rules;
    }

} // namespace jc

#endif // JC2_SYMRULES_H
