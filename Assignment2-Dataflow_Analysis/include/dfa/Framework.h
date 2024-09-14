#pragma once // NOLINT(llvm-header-guard)

#include <type_traits>
#include <unordered_map>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instruction.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace dfa {

template <typename TDomainElemRepr> //
class MeetOp;

/// Analysis Direction, used as Template Parameter
enum class Direction { kForward, kBackward };

template <Direction TDirection> //
struct FrameworkTypeSupport {};

/*
 * Provides support to iterate thorugh a subrange.
 */
template <> //
struct FrameworkTypeSupport<Direction::kForward> {
  typedef iterator_range<Function::const_iterator> BBTraversalConstRange;
  typedef iterator_range<BasicBlock::const_iterator> InstTraversalConstRange;
};

template <> //
struct FrameworkTypeSupport<Direction::kBackward> {
  typedef iterator_range<Function::BasicBlockListType::const_reverse_iterator>
      BBTraversalConstRange;
  typedef iterator_range<BasicBlock::InstListType::const_reverse_iterator>
      InstTraversalConstRange;
};

/**
 * @brief  Dataflow Analysis Framework
 *
 * @tparam TDomainElem      Domain Element
 * @tparam TDomainElemRepr  Domain Element Representation
 * @tparam TDirection       Direction of Analysis
 * @tparam TMeetOp          Meet Operator
 */
template <typename TDomainElem, typename TDomainElemRepr, Direction TDirection,
          typename TMeetOp>
class Framework {

  static_assert(std::is_base_of<MeetOp<TDomainElemRepr>, TMeetOp>::value,
                "TMeetOp has to inherit from MeetOp");

/**
 * @brief Selectively enables methods depending on the analysis direction.
 * @param dir  Direction of Analysis
 * @param ret_type  Return Type
 */
#define METHOD_ENABLE_IF_DIRECTION(dir, ret_type)                              \
  template <Direction _TDirection = TDirection>                                \
  typename std::enable_if_t<_TDirection == dir, ret_type>

protected:
  using DomainVal_t = std::vector<TDomainElemRepr>;

private:
  using MeetOperands_t = std::vector<DomainVal_t>;
  using BBTraversalConstRange =
      typename FrameworkTypeSupport<TDirection>::BBTraversalConstRange;
  using InstTraversalConstRange =
      typename FrameworkTypeSupport<TDirection>::InstTraversalConstRange;

protected:
  // Domain
  std::vector<TDomainElem> Domain;
  // Instruction-Domain Value Mapping
  std::unordered_map<const Instruction *, DomainVal_t> InstDomainValMap;
  /*****************************************************************************
   * Auxiliary Print Subroutines
   *****************************************************************************/
private:
  /**
   * @brief Print the domain with mask. E.g., If domian = {%1, %2, %3,},
   *        dumping it with mask = 001 will give {%3,}.
   */
  void printDomainWithMask(const DomainVal_t &Mask) const {
    errs() << "{";
    assert(Mask.size() == Domain.size() &&
           "The size of mask must be equal to the size of domain.");
    unsigned MaskIdx = 0;
    for (const auto &Elem : Domain) {
      if (!Mask[MaskIdx++]) {
        continue;
      }
      errs() << Elem << ", ";
    } // for (MaskIdx ∈ [0, Mask.size()))
    errs() << "}";
  }

  /**
   * @brief Fetch the DomainMap for the Instr, and print out at the
   * value at the Instr point in the Forward pass.
   * This can help visualize how each point (Instr) affect the domainMap.
   *
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kForward, void)
  printInstDomainValMap(const Instruction &Inst) const {
    const BasicBlock *const InstParent = Inst.getParent();
    if (&Inst == &(InstParent->front())) {
      errs() << "\t";
      printDomainWithMask(getBoundaryVal(*InstParent));
      errs() << "\n";
    } // if (&Inst == &(*InstParent->begin()))
    outs() << Inst << "\n";
    errs() << "\t";
    printDomainWithMask(InstDomainValMap.at(&Inst));
    errs() << "\n";
  }

  /**
   * Fetch and Print DomainMap values for each Instr during the backward pass
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kBackward, void)
  printInstDomainValMap(const Instruction &Inst) const {
    const BasicBlock *const InstParent = Inst.getParent();
    if (&Inst == &(InstParent->back())) {
      errs() << "\t";
      printDomainWithMask(getBoundaryVal(*InstParent));
      errs() << "\n";
    } // if (&Inst == &(*InstParent->end()))
    outs() << Inst << "\n";
    errs() << "\t";
    printDomainWithMask(InstDomainValMap.at(&Inst));
    errs() << "\n";
  }

  /**
   * @brief Dump, ∀inst ∈ F, the associated domain value.
   */
  void printInstDomainValMap(const Function &F) const {
    // clang-format off
    errs() << "**************************************************" << "\n"
           << "* Instruction-Domain Value Mapping" << "\n"
           << "**************************************************" << "\n";
    // clang-format on
    for (const auto &Inst : instructions(F)) {
      printInstDomainValMap(Inst);
    }
  }
  /*****************************************************************************
   * BasicBlock Boundary
   *****************************************************************************/
protected:
  virtual DomainVal_t getBoundaryVal(const BasicBlock &BB) const {
    MeetOperands_t MeetOperands = getMeetOperands(BB);
    if (MeetOperands.begin() == MeetOperands.end()) {
      // If the list of meet operands is empty, then we are at the boundary,
      // hence obtain the BC.
      return bc();
    }
    return meet(MeetOperands);
  }

private:
  /**
   * @brief Meet Operands for the Forward Direction Pass.
   *
   * Return last instruction's DomainVal map for each of the predecessors.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kForward, MeetOperands_t)
  getMeetOperands(const BasicBlock &BB) const {
    MeetOperands_t operands;
    for (auto *pred : predecessors(&BB)) {
      operands.push_back(InstDomainValMap.at(pred->back()));
    }

    return operands;
  }

  /**
   * @brief Meet Operands for the Backward Direction Pass.
   *
   * Return the first instruction's DomainVal map for each successors.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kBackward, MeetOperands_t)
  getMeetOperands(const BasicBlock &BB) const {
    MeetOperands_t operands;
    for (auto *succ : successors(&BB)) {
      operands.push_back(InstDomainValMap.at(succ->front()));
    }

    return operands;
  }
  /**
   * @brief Boundary Condition
   */
  DomainVal_t bc() const { return DomainVal_t(Domain.size()); }
  /**
   * @brief Apply the meet operator to the operands.
   */
  DomainVal_t meet(const MeetOperands_t &MeetOperands) const {
    TMeetOp MeetOp;
    // Does first element should be top?? or since all elements are assigned top
    // during initialization, this should be okay?
    DomainVal_t merge = MeetOperands.at(0);
    for (int Idx = 1; Idx < MeetOperands.size(); Idx++) {
      merge = MeetOp(merge, MeetOperands.at(Idx));
    }

    return merge;
  }
  /*****************************************************************************
   * Transfer Function
   *****************************************************************************/
protected:
  static bool diff(DomainVal_t &LHS, DomainVal_t &RHS) {
    if (LHS.size() != RHS.size()) {
      assert(false && "Size of domain values has to be the same");
    }
    for (size_t Idx = 0; Idx < LHS.size(); ++Idx) {
      if (LHS[Idx] != RHS[Idx]) {
        return true;
      }
    }
    return false;
  }

private:
  /**
   * @brief  Apply the transfer function at instruction @c Inst to the input
   *         domain values to get the output.
   * @return true if @c OV has been changed, false otherwise
   *
   * @todo(cscd70) Please implement this method for every child class.
   */
  virtual bool transferFunc(const Instruction &Inst, const DomainVal_t &IV,
                            DomainVal_t &OV) = 0;
  /*****************************************************************************
   * CFG Traversal
   *****************************************************************************/
  /**
   * @brief Return the traversal order of the basic blocks for a forward pass.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kForward, BBTraversalConstRange)
  getBBTraversalOrder(const Function &F) const {
    /*
     * return iterator_range<T>(std::move(x), std::move(y));
     * which defines begin() and end() to return iterators to be
     * compatible with range based for-loops.
     */
    return make_range(F.begin(), F.end());
  }
  /**
   * @brief Return the traversal order of the instructions for a forward pass.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kForward, InstTraversalConstRange)
  getInstTraversalOrder(const BasicBlock &BB) const {
    return make_range(BB.begin(), BB.end());
  }

  /**
   * @brief Return the traversal order of the basic blocks for a backward pass.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kBackward, BBTraversalConstRange)
  getBBTraversalOrder(const Function &F) const {
    return make_range(F.getBasicBlockList().rbegin(),
                      F.getBasicBlockList().rend());
  }
  /**
   * @brief Return the traversal order of the instructions for a backward pass.
   */
  METHOD_ENABLE_IF_DIRECTION(Direction::kBackward, InstTraversalConstRange)
  getInstTraversalOrder(const BasicBlock &BB) const {
    return make_range(BB.rbegin(), BB.rend());
  }

  /**
   * @brief  Traverse through the CFG and update instruction-domain value
   *         mapping.
   * @return true if changes are made to the mapping, false otherwise
   *
   */
  bool traverseCFG(const Function &F) {
    bool isChanged = false;
    for (const llvm::BasicBlock *BB : getBBTraversalOrder(F)) {
      /*
       * Initial
       * Case 1: First or Last Instruction in a BB, and not entry
       * or exit block. Get meet(meetOperands())
       * Case 2: Entry or Exit block, get bc().
       */
      DomainVal_t IN = getBoundaryVal(BB);
      for (const llvm::Instruction *I : getInstTraversalOrder(BB)) {
        DomainVal_t OUT = InstDomainValMap.at(I);
        isChanged = isChanged | transferFunc(I, &IN, &OUT);
        errs() << "Instr:" << I->getName() << "\n";
        // If output changed, we need to update the InstDomainValMap for next iter.
        InstDomainValMap.at(I) = OUT;
        /*
         * Case 3: Middle of the block, hence predeccesor (Forward) or successor
         * (Backward) acts as input for next Inst.
         */
        IN = OUT;
      }
    }

    return isChanged;
  }
  /*****************************************************************************
   * Domain Initialization
   *****************************************************************************/
  /**
   * @todo(cscd70) Please implement this method for every child class.
   */
  virtual void initializeDomainFromInst(const Instruction &Inst) = 0;
  /**
   * @brief Initialize the domain from each instruction and/or argument.
   */
  void initializeDomain(const Function &F) {
    for (const auto &Inst : instructions(F)) {
      initializeDomainFromInst(Inst);
    }
  }

protected:
  virtual ~Framework() {}

  bool runOnFunction(const Function &F) {
    // initialize the domain
    initializeDomain(F);
    // apply the initial conditions
    TMeetOp MeetOp;
    for (const auto &Inst : instructions(F)) {
      InstDomainValMap.emplace(&Inst, MeetOp.top(Domain.size()));
    }
    // keep traversing until no changes have been made to the
    // instruction-domain value mapping
    while (traverseCFG(F)) {
    }
    printInstDomainValMap(F);
    return false;
  }
};

} // namespace dfa
