#include "seadsa/WellFormed.hh"

#include "llvm/IR/Constants.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "seadsa/Global.hh"
#include "seadsa/Graph.hh"
#include "seadsa/Info.hh"

#include <algorithm>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;

static cl::opt<bool> WfReport(
    "sea-dsa-wf-report",
    cl::desc("Report SeaDsa well-formedness and graph-quality metrics"),
    cl::init(false));

static cl::opt<bool> WfStrict(
    "sea-dsa-wf-strict",
    cl::desc("Reject modules that violate SeaDsa well-formedness policy"),
    cl::init(false));

static cl::opt<bool> WfAllowExplicitCollapse(
    "sea-dsa-wf-allow-explicit-collapse",
    cl::desc("Allow explicit sea_dsa_collapse calls in strict mode"),
    cl::init(false));

static cl::opt<unsigned> WfMaxCollapsedLiveNodes(
    "sea-dsa-wf-max-collapsed-live-nodes",
    cl::desc("Reject if more than N live nodes are collapsed"),
    cl::init(0));

static cl::opt<double> WfMaxCollapsedAccessRatio(
    "sea-dsa-wf-max-collapsed-access-ratio",
    cl::desc("Reject if more than P percent of memory accesses use collapsed "
             "nodes"),
    cl::init(0.0));

static cl::opt<unsigned> WfMaxAllocSitesPerNode(
    "sea-dsa-wf-max-alloc-sites-per-node",
    cl::desc("Reject if any live node merges more than N allocation sites"),
    cl::init(std::numeric_limits<unsigned>::max()));

static cl::opt<bool> WfAllowConstantAddresses(
    "sea-dsa-wf-allow-constant-addresses",
    cl::desc("Allow inttoptr of integer constant literals"),
    cl::init(false));

static cl::opt<std::string> WfCsv(
    "sea-dsa-wf-to-csv",
    cl::desc("Emit SeaDsa well-formedness metrics to a CSV file"),
    cl::init(""));

namespace seadsa {
extern bool g_IsTypeAware;

namespace {

struct NodeMetrics {
  unsigned Id = 0;
  unsigned Accesses = 0;
  unsigned AllocSites = 0;
  bool OffsetCollapsed = false;
  bool TypeCollapsed = false;
  bool IntToPtr = false;
  bool External = false;
  bool Unknown = false;
  bool Incomplete = false;
  bool AllowedConstantAddress = false;
};

struct Metrics {
  unsigned ExplicitCollapseCalls = 0;
  unsigned LiveNodes = 0;
  unsigned CollapsedLiveNodes = 0;
  unsigned OffsetCollapsedLiveNodes = 0;
  unsigned TypeCollapsedLiveNodes = 0;
  unsigned IntToPtrLiveNodes = 0;
  unsigned RejectedIntToPtrLiveNodes = 0;
  unsigned ExternalLiveNodes = 0;
  unsigned UnknownLiveNodes = 0;
  unsigned IncompleteLiveNodes = 0;
  unsigned MaxAllocSites = 0;
  unsigned TotalAccesses = 0;
  unsigned CollapsedAccesses = 0;
  std::vector<NodeMetrics> Nodes;

  double collapsedAccessRatio() const {
    if (TotalAccesses == 0) return 0.0;
    return (100.0 * static_cast<double>(CollapsedAccesses)) /
           static_cast<double>(TotalAccesses);
  }
};

static bool isSeaDsaCollapseCall(const CallBase &CB) {
  const Function *Callee = CB.getCalledFunction();
  return Callee && Callee->getName() == "sea_dsa_collapse";
}

static unsigned countExplicitCollapseCalls(Module &M) {
  unsigned Count = 0;
  for (Function &F : M) {
    if (F.isDeclaration()) continue;
    for (Instruction &I : instructions(F)) {
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (isSeaDsaCollapseCall(*CB)) ++Count;
    }
  }
  return Count;
}

static bool isConstantAddressIntToPtr(const Value &V) {
  if (const auto *I = dyn_cast<IntToPtrInst>(&V))
    return isa<ConstantInt>(I->getOperand(0));

  if (const auto *CE = dyn_cast<ConstantExpr>(&V))
    return CE->getOpcode() == Instruction::IntToPtr &&
           isa<ConstantInt>(CE->getOperand(0));

  return false;
}

static bool isAllowedConstantAddressNode(const Node &N) {
  if (!WfAllowConstantAddresses || !N.isIntToPtr()) return false;

  const auto &AllocSites = N.getAllocSites();
  if (AllocSites.empty()) return false;

  return std::all_of(AllocSites.begin(), AllocSites.end(),
                     [](const Value *V) {
                       return V && isConstantAddressIntToPtr(*V);
                     });
}

static Metrics collectMetrics(Module &M, const DataLayout &DL,
                              TargetLibraryInfoWrapperPass &TLIWrapper,
                              GlobalAnalysis &Dsa) {
  Metrics Res;
  Res.ExplicitCollapseCalls = countExplicitCollapseCalls(M);

  DsaInfo Info(DL, TLIWrapper, Dsa);
  Info.runOnModule(M);

  for (const NodeInfo &NI : Info.live_nodes()) {
    const Node *N = NI.getNode();
    if (!N) continue;

    NodeMetrics NM;
    NM.Id = NI.getId();
    NM.Accesses = NI.getAccesses();
    NM.OffsetCollapsed = N->isOffsetCollapsed();
    NM.TypeCollapsed = g_IsTypeAware && N->isTypeCollapsed();
    NM.IntToPtr = N->isIntToPtr();
    NM.External = N->isExternal();
    NM.Unknown = N->isUnknown();
    NM.Incomplete = N->isIncomplete();
    NM.AllowedConstantAddress = isAllowedConstantAddressNode(*N);

    // visitCastIntToPtr uses the cast itself as a synthetic allocation site.
    // There is no per-site synthetic bit today, so keep alloc-site sharing
    // metrics focused on non-inttoptr nodes.
    NM.AllocSites = NM.IntToPtr ? 0 : N->getAllocSites().size();

    const bool Collapsed = NM.OffsetCollapsed || NM.TypeCollapsed;

    ++Res.LiveNodes;
    Res.TotalAccesses += NM.Accesses;
    if (NM.OffsetCollapsed) ++Res.OffsetCollapsedLiveNodes;
    if (NM.TypeCollapsed) ++Res.TypeCollapsedLiveNodes;
    if (Collapsed) {
      ++Res.CollapsedLiveNodes;
      Res.CollapsedAccesses += NM.Accesses;
    }
    if (NM.IntToPtr) {
      ++Res.IntToPtrLiveNodes;
      if (!NM.AllowedConstantAddress) ++Res.RejectedIntToPtrLiveNodes;
    }
    if (NM.External) ++Res.ExternalLiveNodes;
    if (NM.Unknown) ++Res.UnknownLiveNodes;
    if (NM.Incomplete) ++Res.IncompleteLiveNodes;
    Res.MaxAllocSites = std::max(Res.MaxAllocSites, NM.AllocSites);
    Res.Nodes.push_back(NM);
  }

  return Res;
}

static bool violatesStrictPolicy(const Metrics &M) {
  if (!WfAllowExplicitCollapse && M.ExplicitCollapseCalls > 0) return true;
  if (M.CollapsedLiveNodes > WfMaxCollapsedLiveNodes) return true;
  if (M.collapsedAccessRatio() > WfMaxCollapsedAccessRatio) return true;
  if (M.MaxAllocSites > WfMaxAllocSitesPerNode) return true;
  if (M.RejectedIntToPtrLiveNodes > 0) return true;
  if (M.ExternalLiveNodes > 0) return true;
  if (M.UnknownLiveNodes > 0) return true;
  return false;
}

static void printSummary(const Metrics &M, raw_ostream &OS) {
  OS << "=== SeaDsa well-formedness report ===\n";
  OS << "** explicit sea_dsa_collapse calls " << M.ExplicitCollapseCalls
     << "\n";
  OS << "** live nodes " << M.LiveNodes << "\n";
  OS << "\t** collapsed live nodes " << M.CollapsedLiveNodes << "\n";
  OS << "\t\t** offset-collapsed live nodes "
     << M.OffsetCollapsedLiveNodes << "\n";
  if (g_IsTypeAware)
    OS << "\t\t** type-collapsed live nodes " << M.TypeCollapsedLiveNodes
       << "\n";
  OS << "** memory accesses " << M.TotalAccesses << "\n";
  OS << "\t** accesses through collapsed nodes " << M.CollapsedAccesses
     << " (" << M.collapsedAccessRatio() << "%)\n";
  OS << "** live inttoptr nodes " << M.IntToPtrLiveNodes << "\n";
  if (WfAllowConstantAddresses)
    OS << "\t** rejected non-constant-address inttoptr nodes "
       << M.RejectedIntToPtrLiveNodes << "\n";
  OS << "** live external nodes " << M.ExternalLiveNodes << "\n";
  OS << "** live unknown nodes " << M.UnknownLiveNodes << "\n";
  OS << "** live incomplete nodes " << M.IncompleteLiveNodes << "\n";
  OS << "** max allocation sites in a live non-inttoptr node "
     << M.MaxAllocSites << "\n";
}

static void printWorstNodes(const Metrics &M, raw_ostream &OS) {
  std::vector<NodeMetrics> Nodes = M.Nodes;
  std::sort(Nodes.begin(), Nodes.end(),
            [](const NodeMetrics &A, const NodeMetrics &B) {
              const bool AC = A.OffsetCollapsed || A.TypeCollapsed;
              const bool BC = B.OffsetCollapsed || B.TypeCollapsed;
              if (AC != BC) return AC > BC;
              if (A.Accesses != B.Accesses) return A.Accesses > B.Accesses;
              if (A.AllocSites != B.AllocSites)
                return A.AllocSites > B.AllocSites;
              return A.Id < B.Id;
            });

  unsigned Printed = 0;
  for (const NodeMetrics &N : Nodes) {
    const bool Interesting =
        N.OffsetCollapsed || N.TypeCollapsed ||
        (N.IntToPtr && !N.AllowedConstantAddress) || N.External || N.Unknown ||
        N.AllocSites == M.MaxAllocSites;
    if (!Interesting) continue;
    if (Printed++ >= 10) break;

    OS << "\t[Node Id " << N.Id << "] accesses=" << N.Accesses
       << " alloc_sites=" << N.AllocSites
       << " offset_collapsed=" << N.OffsetCollapsed
       << " type_collapsed=" << N.TypeCollapsed
       << " inttoptr=" << N.IntToPtr
       << " allowed_constant_address=" << N.AllowedConstantAddress
       << " external=" << N.External << " unknown=" << N.Unknown
       << " incomplete=" << N.Incomplete << "\n";
  }
}

static void writeCsv(const Metrics &M, StringRef Filename) {
  if (Filename.empty()) return;

  std::error_code EC;
  raw_fd_ostream File(Filename, EC, sys::fs::OF_Text);
  if (EC) {
    errs() << "WARNING: could not open " << Filename
           << " for SeaDsa well-formedness CSV output: " << EC.message()
           << "\n";
    return;
  }

  File << "metric,value\n";
  File << "explicit_collapse_calls," << M.ExplicitCollapseCalls << "\n";
  File << "live_nodes," << M.LiveNodes << "\n";
  File << "collapsed_live_nodes," << M.CollapsedLiveNodes << "\n";
  File << "offset_collapsed_live_nodes," << M.OffsetCollapsedLiveNodes << "\n";
  File << "type_collapsed_live_nodes," << M.TypeCollapsedLiveNodes << "\n";
  File << "total_accesses," << M.TotalAccesses << "\n";
  File << "collapsed_accesses," << M.CollapsedAccesses << "\n";
  File << "collapsed_access_ratio," << M.collapsedAccessRatio() << "\n";
  File << "inttoptr_live_nodes," << M.IntToPtrLiveNodes << "\n";
  File << "rejected_inttoptr_live_nodes," << M.RejectedIntToPtrLiveNodes
       << "\n";
  File << "external_live_nodes," << M.ExternalLiveNodes << "\n";
  File << "unknown_live_nodes," << M.UnknownLiveNodes << "\n";
  File << "incomplete_live_nodes," << M.IncompleteLiveNodes << "\n";
  File << "max_alloc_sites_per_live_non_inttoptr_node," << M.MaxAllocSites
       << "\n";
}

} // namespace

bool isWellFormedCheckRequested() {
  return WfReport || WfStrict || !WfCsv.getValue().empty();
}

void runWellFormedCheck(Module &M, const DataLayout &DL,
                        TargetLibraryInfoWrapperPass &TLIWrapper,
                        GlobalAnalysis &Dsa) {
  if (!isWellFormedCheckRequested()) return;

  if (Dsa.kind() != GlobalAnalysisKind::CONTEXT_INSENSITIVE) {
    const char *Msg =
        "SeaDsa well-formedness checking currently supports only "
        "context-insensitive analysis (-sea-dsa=ci)";
    if (WfStrict) report_fatal_error(Msg, false);
    errs() << "WARNING: " << Msg << "\n";
    return;
  }

  Metrics Mx = collectMetrics(M, DL, TLIWrapper, Dsa);
  const bool Failed = WfStrict && violatesStrictPolicy(Mx);

  if (WfReport || Failed) {
    printSummary(Mx, errs());
    if (!Mx.Nodes.empty()) {
      errs() << "--- Worst SeaDsa well-formedness nodes\n";
      printWorstNodes(Mx, errs());
    }
  }

  writeCsv(Mx, WfCsv.getValue());

  if (Failed)
    report_fatal_error("SeaDsa well-formedness check failed", false);
}

} // namespace seadsa
