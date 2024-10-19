#pragma once // NOLINT(llvm-header-guard)

/**
 * @file Will-be-Available Expression Dataflow Analysis
 */
#include "1-AntiExpr.h"
#include "dfa/Framework.h"
#include "dfa/MeetOp.h"
#include <cassert>
#include <llvm-12/llvm/IR/InstrTypes.h>
#include <llvm-12/llvm/IR/Instruction.h>
#define DEBUG_WB_EXPR

class WBAvailExprWrapperPass;

using WBAvailExprFrameworkBase =
    Framework<Expression, bool, Direction::kForward, Intersect>;

class WBAvailExprImpl : public WBAvailExprFrameworkBase {
private:
  std::unordered_map<const Instruction *, DomainVal_t> AntiExprInstDomainValMap;
  // Basic Block - Boundary Value Mapping
  std::unordered_map<const BasicBlock *, std::vector<bool>>
      BasicBlockBoundaryValMap;

private:
  WBAvailExprImpl() = default;

  void initialize(std::vector<Expression> AntiExprDomain,
                  std::unordered_map<const Instruction *, DomainVal_t>
                      AntiExprInstDomainValMap) {
    std::copy(AntiExprDomain.begin(), AntiExprDomain.end(),
              std::back_inserter(this->Domain));
    this->AntiExprInstDomainValMap = AntiExprInstDomainValMap;
  }

  void constructBasicBlockBoundaryValMap(const Function &F) {
    for (const auto &BB : F) {
      BasicBlockBoundaryValMap.emplace(&BB, getBoundaryVal(BB));
    }
  }

  virtual void initializeDomainFromInst(const Instruction &Inst) override {
    /*
     * Domain same as Anti-Expression, which is set in the initialize
     * method. Just adding debug information.
     */
#ifdef DEBUG_WB_EXPR
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);
      auto it = std::find(Domain.begin(), Domain.end(), Expr);

      if (it != Domain.end()) {
        errs() << "Domain Inst:" << Inst << "\n";
        errs() << "\t\tExpression:"
               << "\n";
        errs() << "\t\t  Opcode:" << Expr.Opcode << "\n";
        errs() << "\t\t  LHS:" << *Expr.LHS << "\n";
        errs() << "\t\t  RHS:" << *Expr.RHS << "\n";
      }
    }
#endif
  }

  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IV,
                            DomainVal_t &OV) override {
    /*
     * Step 1: Generate
     *
     * Set the newly defined Instr in output.
     * Add only if its a BinaryInstr
     */
    DomainVal_t TEMP_OV = IV;
    if (const BinaryOperator *const BinaryOp =
            dyn_cast<BinaryOperator>(&Inst)) {
      Expression Expr = Expression(*BinaryOp);
      auto it = std::find(Domain.begin(), Domain.end(), Expr);
      if (it != Domain.end()) {
        int index = it - Domain.begin();
        TEMP_OV.at(index) = true;
      }
    }

    /*
     * Step 2: Anticipated?
     *
     * If Expression is anticipated, then
     * it also available
     *
     */
    DomainVal_t AntiExprIN = AntiExprInstDomainValMap.at(&Inst);
    assert(
        AntiExprIN.size() == TEMP_OV.size() &&
        "Anticipated IN Inst Vector should equal Will Be Availble OUT Vector");
    for (long unsigned int i = 0; i < TEMP_OV.size(); ++i) {
      // Anticipated U Available Expression
      TEMP_OV[i] = TEMP_OV[i] || AntiExprIN[i];
    }

    /*
     * Step 3: Kill
     *
     * For all Expressions in Domain, whose Expr.LHS or Expr.RHS
     * equals current Inst, then Expr is killed or no more valid,
     * because Inst is re-defined.
     */
    int Idx = 0;
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
        TEMP_OV.at(Idx) = false;
      }
      Idx++;
    }

    bool isChanged = TEMP_OV != OV;
    OV = TEMP_OV;

    return isChanged;
  }

private:
  friend class WBAvailExprWrapperPass;
};

class WBAvailExprWrapperPass : public FunctionPass {
private:
  WBAvailExprImpl WBAvailExpr;

public:
  static char ID;

  WBAvailExprWrapperPass() : FunctionPass(ID) {}

  virtual void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AntiExprWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    errs() << "* Will-Be-Available Expression *"
           << "\n";

    // Get the results from AntiExprWrapperPass
    AntiExprWrapperPass &AntiExpr = getAnalysis<AntiExprWrapperPass>();

    // Access the domain and instruction-domain value map from
    // Anticipated Expression
    std::vector<Expression> AntiExprDomain = AntiExpr.getDomain();
    auto AntiExprInstDomainValMap = AntiExpr.getInstDomainValMap();
    WBAvailExpr.initialize(AntiExprDomain, AntiExprInstDomainValMap);

    bool isModified = WBAvailExpr.runOnFunction(F);

    WBAvailExpr.constructBasicBlockBoundaryValMap(F);

    return isModified;
  }

  /**
   * Obtain the @c InstDomainValMap and boundary values at each BB
   *               from @c WBAvailExprImpl .
   */
  std::unordered_map<const Instruction *, std::vector<bool>>
  getInstDomainValMap() const {
    return WBAvailExpr.InstDomainValMap;
  }
  std::unordered_map<const BasicBlock *, std::vector<bool>>
  getBoundaryVals() const {
    return WBAvailExpr.BasicBlockBoundaryValMap;
  }
};
