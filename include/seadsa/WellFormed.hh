#ifndef __SEADSA_WELL_FORMED_HH_
#define __SEADSA_WELL_FORMED_HH_

namespace llvm {
class DataLayout;
class Module;
class TargetLibraryInfoWrapperPass;
} // namespace llvm

namespace seadsa {
class GlobalAnalysis;

bool isWellFormedCheckRequested();

void runWellFormedCheck(llvm::Module &M, const llvm::DataLayout &DL,
                        llvm::TargetLibraryInfoWrapperPass &TLIWrapper,
                        GlobalAnalysis &Dsa);
} // namespace seadsa

#endif
