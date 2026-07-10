#include "seadsa/FieldType.hh"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/CommandLine.h"

#include "seadsa/Graph.hh"
#include "seadsa/TypeUtils.hh"

namespace seadsa {

static llvm::cl::opt<bool> EnableOmnipotentChar(
    "sea-dsa-omnipotent-char",
    llvm::cl::desc("Enable SeaDsa omnipotent char (default is true)"),
    // NOTE: Setting this to false results in unsound results
    // because LLVM insists on storing pointers as i8*
    // even when they have different types in the source
    // language
    llvm::cl::init(true));

namespace seadsa {
bool g_IsTypeAware;
}

namespace {
using SeenTypes = llvm::SmallDenseSet<llvm::Type *, 8>;

llvm::Type *GetInnermostTypeImpl(llvm::Type *const Ty, SeenTypes &seen) {
  assert(Ty);

  static llvm::DenseMap<llvm::Type *, llvm::Type *> s_cachedInnermostTypes;
  {
    auto it = s_cachedInnermostTypes.find(Ty);
    if (it != s_cachedInnermostTypes.end()) return it->getSecond();
  }

  llvm::Type *currentTy = Ty;
  while (!seen.count(currentTy)) {
    seen.insert(currentTy);

    if (currentTy->isPointerTy()) {
      auto *PTy = llvm::cast<llvm::PointerType>(currentTy);
      // Opaque pointers carry no element type: dead-end. (Pointee types
      // recovered by inference are canonicalized separately, see the
      // FieldType two-argument constructor.)
      if (PTy->isOpaque()) break;
      auto *ElemTy = PTy->getNonOpaquePointerElementType();
      currentTy = GetInnermostTypeImpl(ElemTy, seen)
                      ->getPointerTo(PTy->getAddressSpace());
      break;
    }

    auto It = AggregateIterator::mkBegin(currentTy, /* DL = */ nullptr);
    auto *FirstTy = It->Ty;
    if (!FirstTy) break;

    if (FirstTy->isPointerTy() &&
        !llvm::cast<llvm::PointerType>(FirstTy)->isOpaque() &&
        FirstTy->getNonOpaquePointerElementType() == currentTy)
      break;

    if (FirstTy == currentTy) break;

    currentTy = FirstTy;
  }

  s_cachedInnermostTypes.insert({Ty, currentTy});
  return currentTy;
}
} // namespace

/// This is intended to be used within a single llvm::Context. When there's more
/// than one context, the caching might misbehave.
/// LLVM 15: typed pointers still canonicalize through their element type as
/// in dev14; OPAQUE pointers carry no pointee and are treated as primitive
/// types, which may be returned.
llvm::Type *GetFirstPrimitiveTy(llvm::Type *const Ty) {
  assert(Ty);

  SeenTypes seen;
  return GetInnermostTypeImpl(Ty, seen);
}

static bool IsOmnipotentChar(llvm::Type *const Ty) {
  assert(Ty);
  if (auto *ITy = llvm::dyn_cast<const llvm::IntegerType>(Ty))
    return ITy->getBitWidth() == 8;
  else if (auto *ATy = llvm::dyn_cast<const llvm::ArrayType>(Ty)) {
    // -- array of omni chars is an omni char
    // -- used by RustC (or llvm optimizer), where [0 x i8]* is used instead of
    // i8*
    return ATy->getNumElements() == 0 &&
           IsOmnipotentChar(ATy->getElementType());
  }

  return false;
}

static bool IsOmnipotentPtr(llvm::Type *const Ty) {
  auto *PTy = llvm::dyn_cast<llvm::PointerType>(Ty);
  if (!PTy || PTy->isOpaque()) return false;
  return IsOmnipotentChar(PTy->getNonOpaquePointerElementType());
}

FieldType::FieldType(llvm::Type *Ty) {
  assert(Ty);

  // -- debug logging
  static bool s_WarnTypeAware = true;
  if (s_WarnTypeAware && g_IsTypeAware) {
    llvm::errs() << "Sea-Dsa type aware!\n";
    s_WarnTypeAware = false;
  }

  m_ty = IsNotTypeAware() ? nullptr : GetFirstPrimitiveTy(Ty);
  if (m_ty && EnableOmnipotentChar) m_is_omni = IsOmnipotentPtr(m_ty);

  // -- debug logging
  if (m_is_omni) {
    static bool s_shown = false;
    if (!s_shown) {
      llvm::errs() << "Omnipotent char: " << *this << "\n";
      s_shown = true;
    }
  }
}

FieldType::FieldType(llvm::Type *Ty, llvm::Type *PointeeTy) : FieldType(Ty) {
  assert(PointeeTy);
  // Canonicalize the pointee the same way field types themselves are
  // canonicalized so that e.g. ptr<%struct.S> and ptr<first-field-of-S>
  // denote the same field (dev14 got this for free from GetFirstPrimitiveTy
  // recursing through pointer element types).
  //
  // Function pointees are collapsed to a single representative: the same
  // function is routinely used at call sites whose signature differs from
  // its definition (varargs casts, K&R C), and field identity only needs
  // "pointer to function", not the exact signature.
  llvm::Type *CanonTy =
      llvm::isa<llvm::FunctionType>(PointeeTy)
          ? llvm::FunctionType::get(
                llvm::Type::getVoidTy(PointeeTy->getContext()),
                /*isVarArg=*/false)
          : GetFirstPrimitiveTy(PointeeTy);
  m_pointee_ty = IsNotTypeAware() ? nullptr : CanonTy;
  if (m_pointee_ty && EnableOmnipotentChar)
    m_is_omni = IsOmnipotentChar(m_pointee_ty);
}

bool FieldType::IsNotTypeAware() { return !g_IsTypeAware; }

} // namespace seadsa
