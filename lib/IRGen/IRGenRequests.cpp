//===--- IRGenRequests.cpp - Requests for LLVM IR Generation --------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2020 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "swift/AST/IRGenRequests.h"
#include "swift/AST/ASTContext.h"
#include "swift/AST/FileUnit.h"
#include "swift/AST/Module.h"
#include "swift/AST/SourceFile.h"
#include "swift/SIL/SILModule.h"
#include "swift/Subsystems.h"
#include "swift/TBDGen/TBDGen.h"
#include "llvm/IR/Module.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"

using namespace swift;

namespace swift {
// Implement the IRGen type zone (zone 20).
#define SWIFT_TYPEID_ZONE IRGen
#define SWIFT_TYPEID_HEADER "swift/AST/IRGenTypeIDZone.def"
#include "swift/Basic/ImplementTypeIDZone.h"
#undef SWIFT_TYPEID_ZONE
#undef SWIFT_TYPEID_HEADER
} // end namespace swift

llvm::orc::ThreadSafeModule GeneratedModule::intoThreadSafeContext() && {
  return {std::move(Module), std::move(Context)};
}

void swift::simple_display(llvm::raw_ostream &out,
                           const IRGenDescriptor &desc) {
  auto *MD = desc.Ctx.dyn_cast<ModuleDecl *>();
  if (MD) {
    out << "IR Generation for module " << MD->getName();
  } else {
    auto *file = desc.Ctx.get<FileUnit *>();
    out << "IR Generation for file ";
    simple_display(out, file);
  }
}

SourceLoc swift::extractNearestSourceLoc(const IRGenDescriptor &desc) {
  return SourceLoc();
}

TinyPtrVector<FileUnit *> IRGenDescriptor::getFiles() const {
  // For a whole module, we emit IR for all files.
  if (auto *mod = Ctx.dyn_cast<ModuleDecl *>())
    return TinyPtrVector<FileUnit *>(mod->getFiles());

  // For a primary file, we emit IR for both it and potentially its
  // SynthesizedFileUnit.
  auto *primary = Ctx.get<FileUnit *>();
  TinyPtrVector<FileUnit *> files;
  files.push_back(primary);

  if (auto *SF = dyn_cast<SourceFile>(primary)) {
    if (auto *synthesizedFile = SF->getSynthesizedFile())
      files.push_back(synthesizedFile);
  }
  return files;
}

ModuleDecl *IRGenDescriptor::getParentModule() const {
  if (auto *file = Ctx.dyn_cast<FileUnit *>())
    return file->getParentModule();
  return Ctx.get<ModuleDecl *>();
}

std::vector<std::string> IRGenDescriptor::getLinkerDirectives() const {
  auto opts = TBDOpts;
  opts.LinkerDirectivesOnly = true;
  if (auto *file = Ctx.dyn_cast<FileUnit *>()) {
    return getPublicSymbols(TBDGenDescriptor::forFile(file, opts));
  } else {
    auto *M = Ctx.get<ModuleDecl *>();
    return getPublicSymbols(TBDGenDescriptor::forModule(M, opts));
  }
}

evaluator::DependencySource IRGenRequest::readDependencySource(
    const evaluator::DependencyRecorder &e) const {
  auto &desc = std::get<0>(getStorage());

  // We don't track dependencies in whole-module mode.
  if (auto *mod = desc.Ctx.dyn_cast<ModuleDecl *>()) {
    return {nullptr, e.getActiveSourceScope()};
  }

  auto *primary = desc.Ctx.get<FileUnit *>();
  return {dyn_cast<SourceFile>(primary), evaluator::DependencyScope::Cascading};
}

// Define request evaluation functions for each of the IRGen requests.
static AbstractRequestFunction *irGenRequestFunctions[] = {
#define SWIFT_REQUEST(Zone, Name, Sig, Caching, LocOptions)                    \
  reinterpret_cast<AbstractRequestFunction *>(&Name::evaluateRequest),
#include "swift/AST/IRGenTypeIDZone.def"
#undef SWIFT_REQUEST
};

void swift::registerIRGenRequestFunctions(Evaluator &evaluator) {
  evaluator.registerRequestFunctions(Zone::IRGen, irGenRequestFunctions);
}
