/**
 * @file Interference Graph Register Allocator
 */
#include <llvm-12/llvm/CodeGen/LiveInterval.h>
#include <llvm-12/llvm/CodeGen/MachineBasicBlock.h>
#include <llvm-12/llvm/CodeGen/Register.h>
#include <llvm-12/llvm/CodeGen/SlotIndexes.h>
#include <llvm-12/llvm/MC/MCRegister.h>
#include <llvm-12/llvm/MC/MCRegisterInfo.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include <llvm/CodeGen/LiveIntervals.h>
#include <llvm/CodeGen/LiveRangeEdit.h>
#include <llvm/CodeGen/LiveRegMatrix.h>
#include <llvm/CodeGen/LiveStacks.h>
#include <llvm/CodeGen/MachineBlockFrequencyInfo.h>
#include <llvm/CodeGen/MachineDominators.h>
#include <llvm/CodeGen/MachineFunctionPass.h>
#include <llvm/CodeGen/MachineLoopInfo.h>
#include <llvm/CodeGen/RegAllocRegistry.h>
#include <llvm/CodeGen/RegisterClassInfo.h>
#include <llvm/CodeGen/Spiller.h>
#include <llvm/CodeGen/TargetRegisterInfo.h>
#include <llvm/CodeGen/VirtRegMap.h>
#include <llvm/InitializePasses.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include "llvm/ADT/BitVector.h"

#include <cmath>
#include <float.h>
#include <math.h>
#include <queue>
#include <stack>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

using namespace llvm;

namespace llvm {

void initializeRAIntfGraphPass(PassRegistry &Registry);

} // namespace llvm

namespace std {

template <> //
struct hash<Register> {
  size_t operator()(const Register &Reg) const {
    return DenseMapInfo<Register>::getHashValue(Reg);
  }
};

template <> //
struct greater<LiveInterval *> {
  bool operator()(LiveInterval *const &LHS, LiveInterval *const &RHS) {
    /*
    The expression comp(a,b), where comp is an object of this type and a and b
    are element keys, shall return true if a is considered to go before b.
    */
    return LHS->weight() > RHS->weight();
  }
};

} // namespace std

namespace {

class RAIntfGraph;

class AllocationHints {
private:
  SmallVector<MCPhysReg, 16> Hints;

public:
  AllocationHints(RAIntfGraph *const RA, const LiveInterval *const LI);
  SmallVectorImpl<MCPhysReg>::iterator begin() { return Hints.begin(); }
  SmallVectorImpl<MCPhysReg>::iterator end() { return Hints.end(); }
};

class RAIntfGraph final : public MachineFunctionPass,
                          private LiveRangeEdit::Delegate {
private:
  MachineFunction *MF;

  SlotIndexes *SI;
  VirtRegMap *VRM;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo *MRI;
  RegisterClassInfo RCI;
  LiveRegMatrix *LRM;
  MachineLoopInfo *MLI;
  LiveIntervals *LIS;

  /**
   * @brief Interference Graph
   */
  class IntfGraph {
  private:
    RAIntfGraph *RA;

    /// Interference Relations
    std::multimap<LiveInterval *, std::unordered_set<Register>,
                  std::greater<LiveInterval *>>
        IntfRels;

    /**
     * @brief  Try to materialize all the virtual registers (internal).
     *
     * @return (nullptr, VirtPhysRegMap) in the case when a successful
     *         materialization is made, (LI, *) in the case when unsuccessful
     *         (and LI is the live interval to spill)
     *
     * @sa tryMaterializeAll
     */
    using MaterializeResult_t =
        std::tuple<LiveInterval *,
                   std::unordered_map<LiveInterval *, MCPhysReg>>;
    MaterializeResult_t tryMaterializeAllInternal();

  public:
    explicit IntfGraph(RAIntfGraph *const RA) : RA(RA) {}
    /**
     * @brief Insert a virtual register @c Reg into the interference graph.
     */
    void insert(const Register &Reg);
    /**
     * @brief Erase a virtual register @c Reg from the interference graph.
     *
     * @sa RAIntfGraph::LRE_CanEraseVirtReg
     */
    void erase(const Register &Reg);
    /**
     * @brief Build the whole graph.
     */
    void build();
    /**
     * @brief Try to materialize all the virtual registers.
     */
    void tryMaterializeAll();
    /**
     * @brief Returns the Allocatable Physical Register/ Available Colors Count
     * ] for the LI
     */
    unsigned getAvailableColors(const LiveInterval *LI);
    void clear() { IntfRels.clear(); }
  } G;

  SmallPtrSet<MachineInstr *, 32> DeadRemats;
  std::unique_ptr<Spiller> SpillerInst;

  void postOptimization() {
    SpillerInst->postOptimization();
    for (MachineInstr *const DeadInst : DeadRemats) {
      LIS->RemoveMachineInstrFromMaps(*DeadInst);
      DeadInst->eraseFromParent();
    }
    DeadRemats.clear();
    G.clear();
  }

  friend class AllocationHints;
  friend class IntfGraph;

  /// The following two methods are inherited from @c LiveRangeEdit::Delegate
  /// and implicitly used by the spiller to edit the live ranges.
  bool LRE_CanEraseVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and erase it from the interference graph.
    return true;
  }
  void LRE_WillShrinkVirtReg(Register Reg) override {
    /**
     * @todo(cscd70) Please implement this method.
     */
    // If the virtual register has been materialized, undo its physical
    // assignment and re-insert it into the interference graph.
  }

public:
  static char ID;

  StringRef getPassName() const override {
    return "Interference Graph Register Allocator";
  }

  RAIntfGraph() : MachineFunctionPass(ID), G(this) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.setPreservesCFG();
#define REQUIRE_AND_PRESERVE_PASS(PassName)                                    \
  AU.addRequired<PassName>();                                                  \
  AU.addPreserved<PassName>()

    REQUIRE_AND_PRESERVE_PASS(SlotIndexes);
    REQUIRE_AND_PRESERVE_PASS(VirtRegMap);
    REQUIRE_AND_PRESERVE_PASS(LiveIntervals);
    REQUIRE_AND_PRESERVE_PASS(LiveRegMatrix);
    REQUIRE_AND_PRESERVE_PASS(LiveStacks);
    REQUIRE_AND_PRESERVE_PASS(AAResultsWrapperPass);
    REQUIRE_AND_PRESERVE_PASS(MachineDominatorTree);
    REQUIRE_AND_PRESERVE_PASS(MachineLoopInfo);
    REQUIRE_AND_PRESERVE_PASS(MachineBlockFrequencyInfo);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoPHIs);
  }
  MachineFunctionProperties getClearedProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;
}; // class RAIntfGraph

AllocationHints::AllocationHints(RAIntfGraph *const RA,
                                 const LiveInterval *const LI) {
  const TargetRegisterClass *const RC = RA->MRI->getRegClass(LI->reg());
  ArrayRef<MCPhysReg> Order = RA->RCI.getOrder(RC);
  bool IsTargetSpecificHardHint =
      RA->TRI->getRegAllocationHints(LI->reg(), Order, Hints, *RA->MF, RA->VRM);

  if (!IsTargetSpecificHardHint) {
    /*
    1. Hard hints are strong suggestions for register allocation that the
    allocator should try to follow if possible.
    2. Soft hints, on the other hand, are more flexible suggestions that the
    allocator can consider but may easily ignore.

    The getRegAllocationHints method returns a boolean indicating whether the
    hints provided are hard hints or not
    */
    for (const MCPhysReg &PhysReg : Order) {
      Hints.push_back(PhysReg);
    }
  }

  outs() << "Hint Registers for Class " << RA->TRI->getRegClassName(RC)
         << ": [";
  for (const MCPhysReg &PhysReg : Hints) {
    outs() << RA->TRI->getRegAsmName(PhysReg) << ", ";
  }
  outs() << "]\n";
}

bool RAIntfGraph::runOnMachineFunction(MachineFunction &MF) {
  outs() << "************************************************\n"
         << "* Machine Function\n"
         << "************************************************\n";
  SI = &getAnalysis<SlotIndexes>();
  for (const MachineBasicBlock &MBB : MF) {
    MBB.print(outs(), SI);
    outs() << "\n";
  }
  outs() << "\n\n";

  this->MF = &MF;

  VRM = &getAnalysis<VirtRegMap>();
  TRI = &VRM->getTargetRegInfo();
  MRI = &VRM->getRegInfo();
  MRI->freezeReservedRegs(MF);
  LIS = &getAnalysis<LiveIntervals>();
  LRM = &getAnalysis<LiveRegMatrix>();
  RCI.runOnMachineFunction(MF);
  MLI = &getAnalysis<MachineLoopInfo>();

  SpillerInst.reset(createInlineSpiller(*this, MF, *VRM));

  G.build();
  G.tryMaterializeAll();

  postOptimization();
  return true;
}

void RAIntfGraph::IntfGraph::insert(const Register &Reg) {
  /**
   * @todo(cscd70) Please implement this method.
   */
  // 1. Collect all VIRTUAL registers that interfere with 'Reg'.
  // 2. Collect all PHYSICAL registers that interfere with 'Reg'.
  // 3. Update the weights of Reg (and its interfering neighbors), using the
  //    formula on "Lecture 6 Register Allocation Page 23".
  // 4. Insert 'Reg' into the graph.
  LiveInterval &LI = RA->LIS->getInterval(Reg);
  std::unordered_set<Register> Interferences;
  AllocationHints PhysRegAllocationOrder = AllocationHints(RA, &LI);

  // 1. Collect all VIRTUAL registers that interfere with 'Reg'.
  for (unsigned virtRegIdx = 0; virtRegIdx < RA->MRI->getNumVirtRegs();
       ++virtRegIdx) {
    Register OtherVirtualRegister = Register::index2VirtReg(virtRegIdx);

    if (Reg == OtherVirtualRegister) {
      continue;
    }
    LiveInterval &OtherVirtualRegisterLI =
        RA->LIS->getInterval(OtherVirtualRegister);
    if (LI.overlaps(OtherVirtualRegisterLI)) {
      Interferences.insert(OtherVirtualRegister);
    }
  }

  /*
  2. Collect all PHYSICAL registers that interfere with 'Reg'.

  Essential thing we need to check here is overlapping of register units or
  overlap with other masked registers.

  Q: Can this interference can also be checked during allocation???
  */
  for (MCRegister PhysReg : PhysRegAllocationOrder) {
    for (MCRegUnitIterator Units(PhysReg, RA->TRI); Units.isValid(); ++Units) {
      if (LI.overlaps(RA->LIS->getRegUnit(*Units))) {
        // VirtReg overlaps with PhysReg Unit
        Interferences.insert(PhysReg);
      }
    }
  }

  // 3. Update the Weight of Live Interval
  unsigned degree = Interferences.size();
  errs() << "Degree of the Live Interval:" << degree << "\n";

  unsigned maxLoopNestDepth = 0;
  for (LiveRange::Segment &S : LI.segments) {
    SlotIndex start = S.start;
    SlotIndex end = S.end;

    for (SlotIndex I = start; I < end; I.getBaseIndex()) {
      if (const MachineInstr *MI = RA->SI->getInstructionFromIndex(I)) {
        const MachineBasicBlock *BB = MI->getParent();
        if (BB) {
          unsigned depth = RA->MLI->getLoopDepth(BB);
          if (depth > maxLoopNestDepth) {
            maxLoopNestDepth = depth;
          }
        }
      }
    }
  }
  errs() << "Loop Nest Depth:" << maxLoopNestDepth << "\n";

  unsigned defCount = 0;
  for (auto it = RA->MRI->def_operands(Reg).begin();
       it != RA->MRI->def_operands(Reg).end(); ++it) {
    defCount++;
  }
  errs() << "#Defs:" << defCount << "\n";

  unsigned useCount = 0;
  for (auto it = RA->MRI->use_operands(Reg).begin();
       it != RA->MRI->use_operands(Reg).end(); ++it) {
    useCount++;
  }
  errs() << "#Uses:" << useCount << "\n";

  float weight = (defCount + useCount) * pow(10.0, maxLoopNestDepth) / degree;
  errs() << "Weight of the Live Interval:" << LI << "is " << weight << "\n";

  // Set the weight to the Live Interval
  LI.setWeight(weight);

  // 4. Insert the Register to the Interference Graph.
  IntfRels.insert({&LI, Interferences});
}

void RAIntfGraph::IntfGraph::erase(const Register &Reg) {
  // 1. ∀n ∈ neighbors(Reg), erase 'Reg' from n's interfering set and update its
  //    weights accordingly.
  LiveInterval &LI = RA->LIS->getInterval(Reg);
  for (auto &entry : IntfRels) {
    std::unordered_set<Register> &interferingRegisterSet = entry.second;
    interferingRegisterSet.erase(Reg);
  }

  // 2. Erase 'Reg' from the interference graph.
  IntfRels.erase(&LI);
}

void RAIntfGraph::IntfGraph::build() {
  // Virtual Registers
  for (unsigned virtRegIdx = 0; virtRegIdx < RA->MRI->getNumVirtRegs();
       ++virtRegIdx) {
    Register Reg = Register::index2VirtReg(virtRegIdx);
    if (RA->MRI->reg_nodbg_empty(Reg)) {
      continue;
    }

    insert(Reg);
  }
}

unsigned RAIntfGraph::IntfGraph::getAvailableColors(const LiveInterval *LI) {
  // Get Register Class
  const TargetRegisterClass *RC = RA->MRI->getRegClass(LI->reg());

  // List of allocatable physical registers to set to 1 in BitVector
  BitVector availableRegs = RA->TRI->getAllocatableSet(*RA->MF, RC);

  // Keep track of register and its alias we have seen so far which was counted
  // as a color
  std::set<unsigned> aliasedRegisters;
  unsigned avaialbleColors = 0;

  unsigned reg = availableRegs.find_first();
  for (unsigned idx = 0; idx < availableRegs.count(); ++idx) {
    bool overlap = false;
    /*
    The loop checks if the current register (reg) overlaps with any
    register already in aliasedRegs

    Registers like AL, AH, and EAX overlap because they represent parts
    of the same physical register.

    Hence, they should be counted only once. We can't assign different
    virtReg to AL, and say EAX. They are not 2 different colors
    */
    for (unsigned aliasedRegister : aliasedRegisters) {
      if (RA->TRI->regsOverlap(reg, aliasedRegister)) {
        overlap = true;
        break;
      }
    }

    if (!overlap) {
      // reg doesn't overlap with any register or with its aliases
      // Hence increment the color
      avaialbleColors++;

      /*
      And add the reg and its aliases to aliasedRegisters.

      If EAX was available, we consider that as 1 color, Subsequently,
      add AL, AH and other units to aliasedRegisters.
      In Next Iteration, if we see, AH/AL, overlap is set to true, and we don't
      increment the available colors.

      EAX, AL, AH is counted as 1 color
      */
      for (MCRegAliasIterator Units(reg, RA->TRI, true); Units.isValid();
           ++Units) {
        aliasedRegisters.insert(*Units);
      }
    }

    reg = availableRegs.find_next(reg);
  }

  return avaialbleColors;
}

RAIntfGraph::IntfGraph::MaterializeResult_t
RAIntfGraph::IntfGraph::tryMaterializeAllInternal() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;

  // ∀r ∈ IntfRels.keys, try to materialize it. If successful, cache it in
  // PhysRegAssignment, else mark it as to be spilled.

  // Nodes clone of IntfRels
  std::multimap<LiveInterval *, std::unordered_set<Register>,
                std::greater<LiveInterval *>>
      Nodes = IntfRels;

  std::stack<
      std::pair<llvm::LiveInterval *const, std::unordered_set<llvm::Register>>>
      RAStack;

  for (auto &entry : Nodes) {
    LiveInterval *LI = entry.first;
    std::unordered_set<Register> &neighbors = entry.second;
    // Neighbors / degree
    unsigned degree = neighbors.size();
    // Available Physical Registers (Colors)
    unsigned availableColors = getAvailableColors(LI);

    if (degree < availableColors) {
      // Add to stack
      RAStack.push(entry);
      // Remove from the Neighbors and Interference Graph
      erase(LI->reg());
    }
  }

  /*
  Chaitin's Alogrithm: If we can't add to Stack (neighbors > colors),
  then spill.

  More conservative approach would be to defer spilling, by
  adding the nodes whose degree > available colors, then spill
  while making the assignment.

  Here, I am taking the trivial approach of spilling if it can't be
  added to stack by skipping the register selection phase.
  */

  if (!Nodes.empty()) {
    // Not empty indicates, few nodes were not added to stack
    double MinSpillWeight = DBL_MAX;
    LiveInterval *SpillCandidate = nullptr;

    // Pick one with least spill weight
    for (auto &spillable : Nodes) {
      LiveInterval *SpillableLI = spillable.first;
      if (SpillableLI->weight() < MinSpillWeight) {
        MinSpillWeight = SpillableLI->weight();
        SpillCandidate = SpillableLI;
      }
    }

    if (SpillCandidate) {
      return std::make_tuple(SpillCandidate, PhysRegAssignment);
      } else {
        llvm_unreachable("No candidate was selected for spilling");
      }
  }

  // If No spillable nodes, then do Register assignment
  while (!RAStack.empty()) {
    std::set<MCPhysReg> usedRegs;
    std::pair<llvm::LiveInterval *const, std::unordered_set<llvm::Register>>
        entry = RAStack.top();
    bool allocationSuccessful = false;

    LiveInterval *ColorableLI = entry.first;
    std::unordered_set<llvm::Register> Neighbors = entry.second;

    /*
    If Neighbor is already popped from Stack, and has been assigned a color,
    then that Physical Register is Unavailable, and can add
    to usedRegs.
    */
    for (auto Neighbor : Neighbors) {
      LiveInterval &LI = RA->LIS->getInterval(Neighbor);

      auto it = PhysRegAssignment.find(&LI);
      if (it != PhysRegAssignment.end()) {
        MCPhysReg alreadyAssignedRegister = it->second;
        // Neighbor has been assigned a Physical register. We can't use that
        // color
        usedRegs.insert(alreadyAssignedRegister);

        // Add its aliases too.
        for (MCRegAliasIterator Units(alreadyAssignedRegister, RA->TRI, true);
             Units.isValid(); ++Units) {
          usedRegs.insert(*Units);
        }
      }
    }

    // Allocate Physical Register from using Allocation Hints
    AllocationHints PhysRegAllocationOrder = AllocationHints(RA, ColorableLI);
    for (MCRegister PhysReg : PhysRegAllocationOrder) {
      if (usedRegs.find(PhysReg) == usedRegs.end()) {
        // Assign Physical Register.
        PhysRegAssignment.insert({ColorableLI, PhysReg});
        allocationSuccessful = true;
      }
    }

    // If couldn't assign from Hints, assign to any available and allocatable
    // register
    const TargetRegisterClass *RC = RA->MRI->getRegClass(ColorableLI->reg());
    BitVector availableRegs = RA->TRI->getAllocatableSet(*RA->MF, RC);

    for (int regIndex = availableRegs.find_first(); regIndex != -1;
         regIndex = availableRegs.find_next(regIndex)) {
      if (availableRegs.test(regIndex)) {
        MCPhysReg reg = static_cast<MCPhysReg>(regIndex);
        bool overlap = false;

        // Check if it overlaps with any register assigned to neighbor or its
        // aliases
        for (MCPhysReg usedReg : usedRegs) {
          if (RA->TRI->regsOverlap(reg, usedReg)) {
            overlap = true;
            break;
          }
        }

        if (!overlap) {
          // Available
          PhysRegAssignment.insert({ColorableLI, reg});
          allocationSuccessful = true;
        }
      }
    }

    if (!allocationSuccessful) {
      // Allocation failed for this LiveInterval, Spill
      return std::make_tuple(ColorableLI, PhysRegAssignment);
    }

    RAStack.pop();
  } // while Stack not empty

  return std::make_tuple(nullptr, PhysRegAssignment);
}

void RAIntfGraph::IntfGraph::tryMaterializeAll() {
  std::unordered_map<LiveInterval *, MCPhysReg> PhysRegAssignment;
  LiveInterval *SpillCandidate;

  // Keep looping until a valid assignment is made. In the case of spilling,
  // modify the interference graph accordingly.

  while (true) {
    std::tie(SpillCandidate, PhysRegAssignment) = tryMaterializeAllInternal();
    if (SpillCandidate == nullptr) {
      // Success, all virtRegs have been materialized.
      break;
    }

    // Spill the Register returned

    // Re-build the Interference graph

    // Try to materialize the virtual registers again
  }

  // Assign the virtual register to the physical register.
  for (auto &PhysRegAssignPair : PhysRegAssignment) {
    RA->LRM->assign(*PhysRegAssignPair.first, PhysRegAssignPair.second);
  }
}

char RAIntfGraph::ID = 0;

static RegisterRegAlloc X("intfgraph", "Interference Graph Register Allocator",
                          []() -> FunctionPass * { return new RAIntfGraph(); });

} // anonymous namespace

INITIALIZE_PASS_BEGIN(RAIntfGraph, "regallointfgraph",
                      "Interference Graph Register Allocator", false, false)
INITIALIZE_PASS_DEPENDENCY(SlotIndexes)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_DEPENDENCY(LiveIntervals)
INITIALIZE_PASS_DEPENDENCY(LiveRegMatrix)
INITIALIZE_PASS_DEPENDENCY(LiveStacks);
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass);
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTree);
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfo);
INITIALIZE_PASS_DEPENDENCY(MachineBlockFrequencyInfo);
INITIALIZE_PASS_END(RAIntfGraph, "regallointfgraph",
                    "Interference Graph Register Allocator", false, false)
