#ifndef JC2_FACTORIZATION_H
#define JC2_FACTORIZATION_H

#include "Symbolic.h"

namespace jc {
    // 因式分解
    SymExpr factor(const SymExpr& expr, int depth = 0);
    // 实数域完全因式分解
    SymExpr factorReal(const SymExpr& expr);
}

#endif // JC2_FACTORIZATION_H
