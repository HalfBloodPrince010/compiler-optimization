/**
 * @file Available Expression Dataflow Analysis
 */
#include <llvm-12/llvm/Support/Casting.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Pass.h>

#include <dfa/Framework.h>
#include <dfa/MeetOp.h>

#include "Expression.h"

#define DEBUG_AVAIL_EXPR

using namespace dfa;
using namespace llvm;

namespace {

using AvailExprFrameworkBase =
    Framework<Expression, bool, Direction::kForward, Intersect>;

class AvailExpr final : public AvailExprFrameworkBase, public FunctionPass {
private:
  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      /**
       * Add Expression to the domain
       * std::vector<TDomainElem> Domain
       * TDomainElem is Expression
       */
      Expression Expr = Expression(*BinaryOp);
// If Expr not already in the Domain, add it
#ifdef DEBUG_AVAIL_EXPR
      errs() << "Domain Inst:" << Inst << "\n";
      errs() << "\t\tExpression:"
             << "\n";
      errs() << "\t\t  Opcode:" << Expr.Opcode << "\n";
      errs() << "\t\t  LHS:" << *Expr.LHS << "\n";
      errs() << "\t\t  RHS:" << *Expr.RHS << "\n";
#endif
      if (std::find(Domain.begin(), Domain.end(), Expr) == Domain.end()) {
        Domain.emplace_back(Expr);
      }
    }
  }
  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IBV,
                            DomainVal_t &OBV) override {

    int Idx = 0;
    DomainVal_t TEMP_OBV = IBV;

    /*
     * Step 1: Generate
     *
     * Set the newly defined Instr in output.
     * Add only if its a BinaryInstr
     *
     * x = x + 1 -> Generate, then kill X
     */
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);
      auto it = std::find(Domain.begin(), Domain.end(), Expr);
      if (it != Domain.end()) {
        int index = it - Domain.begin();
        TEMP_OBV.at(index) = true;
      }
    }
    /*
     * Step 2: Kill
     *
     * For all Expressions in Domain, whose Expr.LHS or Expr.RHS
     * equals current Inst, then Expr is killed or no more valid,
     * because Inst is re-defined.
     */
    for (Expression Expr : Domain) {
      bool redefined = false;
      // LHS
      if (auto *InstLHS = dyn_cast<Instruction>(Expr.LHS)) {
        if (InstLHS == &Inst) {
          redefined = true;
        }
      }
      // RHS
      if (auto *InstRHS = dyn_cast<Instruction>(Expr.RHS)) {
        if (InstRHS == &Inst) {
          redefined = true;
        }
      }
      if (redefined) {
        TEMP_OBV.at(Idx) = false;
      }
      Idx++;
    }

    // Did Output BitVector change?
    bool isChanged = diff(OBV, TEMP_OBV);

    OBV = TEMP_OBV;
    return isChanged;
  }

public:
  static char ID;

  AvailExpr() : AvailExprFrameworkBase(), FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
  bool runOnFunction(Function &F) override {
    return AvailExprFrameworkBase::runOnFunction(F);
  }
};

char AvailExpr::ID = 0;
RegisterPass<AvailExpr> X("avail-expr", "Available Expression");

} // anonymous namespace
