#pragma once // NOLINT(llvm-header-guard)

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

/**
 * @brief A wrapper for binary expressions.
 */
struct Expression {
  const unsigned Opcode;
  const Value *const LHS = nullptr, *const RHS = nullptr;
  Expression(const BinaryOperator &BinaryOp)
      : Opcode(BinaryOp.getOpcode()), LHS(BinaryOp.getOperand(0)),
        RHS(BinaryOp.getOperand(1)) {}

  bool operator==(const Expression &Expr) const {
    if (Expr.Opcode == Opcode && Expr.LHS == LHS && Expr.RHS == RHS) {
      return true;
    }

    /*
     * Commutation Instruction like add (8), fadd(9), mul(12) and fmul(13),
     * where lhs + rhs == rhs + lhs
     * https://llvm.org/doxygen/group__LLVMCCoreTypes.html
     */
    if (Expr.Opcode == Opcode) {
      switch (Opcode) {
      case Instruction::Add:
      case Instruction::FAdd:
      case Instruction::Mul:
      case Instruction::FMul:
      case Instruction::And:
      case Instruction::Or:
      case Instruction::Xor: {
        // Commutative
        if (Expr.LHS == RHS && Expr.RHS == LHS) {
          return true;
        }
        break;
      }
      default:
        break;
      }
      /*
      if (Opcode == 8 || Opcode == 9 || Opcode == 12 || Opcode == 13) {
        if (Expr.LHS == RHS && Expr.RHS == LHS) {
          return true;
        }
      }
      */
    }

    return false;
  }
};

inline raw_ostream &operator<<(raw_ostream &Outs, const Expression &Expr) {
  Outs << "[" << Instruction::getOpcodeName(Expr.Opcode) << " ";
  Expr.LHS->printAsOperand(Outs, false);
  Outs << ", ";
  Expr.RHS->printAsOperand(Outs, false);
  Outs << "]";
  return Outs;
}
