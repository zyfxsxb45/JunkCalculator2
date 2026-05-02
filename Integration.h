#ifndef JC2_INTEGRATION_H
#define JC2_INTEGRATION_H

#include "Symbolic.h"
#include <string>

namespace jc {
    SymExpr integrate(const SymExpr& expr, const std::string& var, int depth = 0); // 符号积分
    SymExpr defint(const SymExpr& expr, const std::string& var, const SymExpr& a, const SymExpr& b); // 定积分
    SymExpr rischIntegrate(const SymExpr& expr, const std::string& var, int depth = 0); // Risch 算法入口
}

#endif // JC2_INTEGRATION_H
