#include "collect.h"
#include "utilities_clang.h"
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendPluginRegistry.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Lex/PreprocessorOptions.h>

using namespace clang;
using namespace std;
namespace fs = boost::filesystem;

namespace carbon {

static const bool debugMode = false;

static collector c;
static fs::path root_src_dir;
static fs::path root_bin_dir;

// we keep a list of macro uses to apply at the close since the preprocessor
// will expand macros before the parser will notify of the AST's therein
static list<pair<clang_source_range_t, clang_source_range_t>> if_def_uses;

//
// stores most-recent #define for a given macro
//
static unordered_map<string, clang_source_range_t> _macro_defs;
static unordered_map<string, SourceRange> macro_defs;

static clang_source_file_t clang_source_file(FileID);
static clang_source_range_t clang_source_range(const SourceRange &);
static SourceManager *gl_SM;
#if 0
static llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const clang_source_file_t &f);
#endif
static llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const clang_source_range_t &cl_src_rng);

static bool is_counterpart(const clang_source_range_t &cl_src_rng,
                           const clang_source_range_t &other_cl_src_rng);

static clang_source_range_t
normalize_source_range(const clang_source_range_t &);

static void needsDecl(const clang_source_range_t &user, const Decl *D);
static void needsType(const clang_source_range_t& user_src_rng, const Type* T);
static bool isInBuiltin(SourceLocation Loc) {
  string buffNm = gl_SM->getBufferName(gl_SM->getSpellingLoc(Loc));
  return buffNm == "<built-in>" || buffNm == "<scratch space>";
}

class CarbonCollectVisitor : public RecursiveASTVisitor<CarbonCollectVisitor> {
  SourceManager &SM;

public:
  CarbonCollectVisitor(CompilerInstance &CI) : SM(CI.getSourceManager()) {}

  bool isInBuiltin(SourceLocation Loc) {
    string buffNm = SM.getBufferName(SM.getSpellingLoc(Loc));
    return buffNm == "<built-in>" || buffNm == "<scratch space>";
  }

  bool VisitMemberExpr(MemberExpr *e) {
    if (debugMode)
      llvm::errs() << "MemberExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()),
              dyn_cast<FieldDecl>(e->getMemberDecl())->getParent());

    return true;
  }

  bool VisitOffsetOfExpr(OffsetOfExpr *e) {
    if (debugMode)
      llvm::errs() << "OffsetOfExpr\n";

    needsType(clang_source_range(e->getSourceRange()),
              e->getTypeSourceInfo()->getType().getTypePtrOrNull());

    return true;
  }

  bool VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *e) {
    if (e->getKind() != UETT_SizeOf) /* sizeof(type) */
      return true;

    if (debugMode)
      llvm::errs() << "UnaryExprOrTypeTraitExpr\n";

    needsType(clang_source_range(e->getSourceRange()),
              e->getTypeOfArgument().getTypePtrOrNull());

    return true;
  }

  bool VisitCStyleCastExpr(CStyleCastExpr *e) {
    if (debugMode)
      llvm::errs() << "CStyleCastExpr\n";

    needsType(clang_source_range(e->getSourceRange()),
              e->getTypeAsWritten().getTypePtrOrNull());

    return true;
  }

  bool VisitCallExpr(CallExpr *e) {
    if (debugMode)
      llvm::errs() << "CallExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()), e->getCalleeDecl());

    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *e) {
    if (debugMode)
      llvm::errs() << "VisitDeclRefExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()), e->getDecl());

    return true;
  }

  bool VisitFieldDecl(FieldDecl *D) {
    if (debugMode)
      llvm::errs() << "FieldDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getTypeSourceInfo()->getType().getTypePtrOrNull());

    return true;
  }

  bool VisitVarDecl(VarDecl *D) {
    if (debugMode)
      llvm::errs() << "VarDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getTypeSourceInfo()->getType().getTypePtrOrNull());

    return true;
  }

  bool VisitParmVarDecl(ParmVarDecl *D) {
    if (debugMode)
      llvm::errs() << "ParmVarDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getOriginalType().getTypePtrOrNull());

    return true;
  }
};

static const char *FileChangeReasonStrings[] = {
    "EnterFile", "ExitFile", "SystemHeaderPragma", "RenameFile"};

#if 0
static const char *CharacteristicKindStrings[] = {
    "C_User", "C_System", "C_ExternCSystem", "C_User_ModuleMap",
    "C_System_ModuleMap"};
#endif

class CarbonCollectPP : public PPCallbacks {
  CompilerInstance &CI;
  SourceManager &SM;
  unordered_set<string> macrosSeen;

public:
  CarbonCollectPP(CompilerInstance &CI) : CI(CI), SM(CI.getSourceManager()) {}

  // \brief Return true if \c Loc is a location in a built-in macro.
  bool isInBuiltin(SourceLocation Loc) {
    string buffNm = SM.getBufferName(SM.getSpellingLoc(Loc));
    return buffNm == "<built-in>" || buffNm == "<scratch space>";
  }


  bool isSourceRangeSensible(const SourceRange& SR) {
    pair<FileID, unsigned> beg = SM.getDecomposedExpansionLoc(SR.getBegin());
    pair<FileID, unsigned> end = SM.getDecomposedExpansionLoc(SR.getEnd());

    return beg.first == end.first && SM.getFileEntryForID(beg.first);
  }

  void FileChanged(SourceLocation Loc, FileChangeReason Reason,
                   SrcMgr::CharacteristicKind FileType, FileID PrevFID) {

    if (debugMode) {
      FileID FID = SM.getDecomposedExpansionLoc(Loc).first;
      bool isSys;

      {
        bool Invalid = false;
        const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
        isSys = !Invalid && SEntry.isFile() &&
                SrcMgr::isSystem(SEntry.getFile().getFileCharacteristic());
      }

      const FileEntry *FE = SM.getFileEntryForID(FID);
      if (FE) {
        llvm::errs() << "FileChanged: ";
        if (isSys)
          llvm::errs() << FE->getName();
        else
          llvm::errs() << fs::relative(FE->getName().str(), root_src_dir).string();
        llvm::errs() << " (" << FileChangeReasonStrings[Reason] << ")\n";
      }
    }

#if 0
    if (debugMode) {
      const FileEntry *PrevFE = SM.getFileEntryForID(PrevFID);

      llvm::errs() << "FileChanged:\n"
                   << "  Loc : " << Loc.printToString(SM) << '\n'
                   << "  Reason : " << FileChangeReasonStrings[Reason] << '\n'
                   << "  FileType : " << CharacteristicKindStrings[FileType] << '\n'
                   << "  PrevFID : "
                   << ((PrevFE && PrevFE->isValid()) ?
                       PrevFE->tryGetRealPathName() :
                       "<invalid>") << '\n';
    }
#endif
  }

  void InclusionDirective(SourceLocation HashLoc,
                          const Token &IncludeTok,
                          StringRef FileName,
                          bool IsAngled,
                          CharSourceRange FilenameRange,
                          const FileEntry *File,
                          StringRef SearchPath,
                          StringRef RelativePath,
                          const Module *Imported,
                          SrcMgr::CharacteristicKind FileType) {
    if (!IncludeTok.is(tok::identifier))
      return;

    if (IncludeTok.getIdentifierInfo()->getPPKeywordID() != tok::pp_include)
      return;

    if (debugMode) {
      llvm::errs() << "InclusionDirective: \"" << FileName << "\" (";

      FileID FID = SM.getDecomposedExpansionLoc(HashLoc).first;
      bool isSys;

      {
        bool Invalid = false;
        const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(FID, &Invalid);
        isSys = !Invalid && SEntry.isFile() &&
                SrcMgr::isSystem(SEntry.getFile().getFileCharacteristic());
      }

      const FileEntry *FE = SM.getFileEntryForID(FID);
      if (FE) {
        if (isSys)
          llvm::errs() << FE->getName();
        else
          llvm::errs() << fs::relative(FE->getName().str(), root_src_dir).string();

        llvm::errs() << ")\n";
      }
    }
  }

  //
  // Hook called whenever a macro definition is seen.
  //
  void MacroDefined(const Token &MacroNameTok, const MacroDirective *MD) {
    const MacroInfo *MI = MD->getMacroInfo();
    if (!MI)
      return;

    IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
    if (!II)
      return;

    SourceRange SR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

    if (!isSourceRangeSensible(SR))
      return;

    StringRef Nm = II->getName();

    bool redefined = macro_defs.find(Nm.str()) != macro_defs.end();

    if (debugMode) {
      if (redefined)
        llvm::errs() << "MacroRedefined ";
      else
        llvm::errs() << "MacroDefined ";
      llvm::errs() << '\"' << Nm << '\"';
    }

    //
    // heavy-weight machinery is required to acquire the source range for a
    // macro definition
    //
    clang_source_range_t src_rng(clang_source_range(SR));
    src_rng.end = src_rng.beg +
                  static_cast<clang_source_location_t>(
                      Lexer::getSourceText(CharSourceRange::getTokenRange(SR),
                                           SM, CI.getLangOpts(), nullptr)
                          .size());
    src_rng.beg -= get_backwards_offset_to_new_line(src_rng);
    c.code(src_rng);

    if (debugMode)
      llvm::errs() << ' ' << src_rng << '\n';

    //
    // if the macro here is being redefined, we need to make sure that all code
    // which uses the previous definition is, in turn, used by the new
    // redefinition to preserve the textual ordering when we perform a
    // topological sort of the dependency graph
    //
    if (redefined && !is_counterpart(src_rng, _macro_defs[Nm.str()])) {
      auto it = macro_defs.find(Nm.str());
      auto _it = _macro_defs.find(Nm.str());

      if (debugMode)
        llvm::errs() << "  PrevDefLoc: "
                     << (*it).second.getBegin().printToString(SM) << '\n'
                     << "  PrevDefEndLoc: "
                     << (*it).second.getEnd().printToString(SM) << '\n';

      c.follow_users_of((*_it).second, src_rng);
    }

    macro_defs[Nm.str()] = SR;
    _macro_defs[Nm.str()] = src_rng;
  }

  //
  // Hook called whenever a macro invocation is found.
  //
  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange userSR, const MacroArgs *Args) {
    const MacroInfo *MI = MD.getMacroInfo();
    if (!MI)
      return;

    IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
    if (!II)
      return;

    if (!isSourceRangeSensible(userSR))
      return;

    SourceRange useeSR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());
    if (!isSourceRangeSensible(useeSR))
      return;

    StringRef Nm = II->getName();
#if 0
    if (debugMode)
      llvm::errs() << "MacroExpands: " << Nm << '\n'
                   << "  user: " << userSR.getBegin().printToString(SM) << '\n'
                   << "  usee: " << useeSR.getBegin().printToString(SM) << '\n';
#endif

    clang_source_range_t user(clang_source_range(userSR));
    clang_source_range_t usee(clang_source_range(useeSR));

#if 1
    if (debugMode)
      llvm::errs() << "MacroExpands \"" << Nm << "\" " << user << " ---> "
                   << usee << '\n';
#endif

    c.use(user, usee);
  }

  //
  // Hook called whenever the 'defined' operator is seen.
  //
  void Defined(const Token &MacroNameTok, const MacroDefinition &MD,
               SourceRange Range) {
    const MacroInfo *MI = MD.getMacroInfo();

    if (!MI || MI->isBuiltinMacro() || isInBuiltin(MI->getDefinitionLoc()) ||
        isInBuiltin(Range.getBegin()))
      return;

    SourceRange userSR(Range);
    SourceRange useeSR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

    clang_source_range_t user(clang_source_range(userSR));
    user.beg -= get_backwards_offset_to_new_line(user);

    clang_source_range_t usee(clang_source_range(useeSR));

    if_def_uses.push_back(make_pair(user, normalize_source_range(usee)));
  }

  //
  // Hook called whenever an #ifdef is seen.
  //
  void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
             const MacroDefinition &MD) {
    const MacroInfo *MI = MD.getMacroInfo();
    if (!MI || MI->isBuiltinMacro() || isInBuiltin(MI->getDefinitionLoc()) ||
        isInBuiltin(Loc))
      return;

    SourceRange userSR(Loc, Loc);
    SourceRange useeSR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

    clang_source_range_t user(clang_source_range(userSR));
    user.beg -= get_backwards_offset_to_new_line(user);

    clang_source_range_t usee(clang_source_range(useeSR));

    if_def_uses.push_back(make_pair(user, normalize_source_range(usee)));
  }

  //
  // Hook called whenever an #ifndef is seen.
  //
  void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
              const MacroDefinition &MD) {
    const MacroInfo *MI = MD.getMacroInfo();
    if (!MI || MI->isBuiltinMacro() || isInBuiltin(MI->getDefinitionLoc()) ||
        isInBuiltin(Loc))
      return;

    SourceRange userSR(Loc, Loc);
    SourceRange useeSR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

    clang_source_range_t user(clang_source_range(userSR));
    user.beg -= get_backwards_offset_to_new_line(user);

    clang_source_range_t usee(clang_source_range(useeSR));

    if_def_uses.push_back(make_pair(user, normalize_source_range(usee)));
  }
};

class CarbonCollectConsumer : public ASTConsumer {
  SourceManager &SM;
  CarbonCollectVisitor Visitor;

public:
  CarbonCollectConsumer(CompilerInstance &CI)
      : SM(CI.getSourceManager()), Visitor(CI) {
    CI.getPreprocessor().addPPCallbacks(llvm::make_unique<CarbonCollectPP>(CI));
  }

  clang_source_range_t sourceRangeOfTopLevelDecl(Decl *D) {
    if (!(isa<FunctionDecl>(D) &&
          cast<FunctionDecl>(D)->doesThisDeclarationHaveABody())) {
      SourceLocation semiEnd =
          findSemiAfterLocation(D->getLocEnd(), D->getASTContext(), true);
      if (semiEnd.isValid()) {
        return clang_source_range(SourceRange(D->getLocStart(), semiEnd));
      }
    }

    return clang_source_range(D->getSourceRange());
  }

  bool HandleTopLevelDecl(DeclGroupRef DR) {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      Decl *D = *b;

      clang_source_range_t src_rng = sourceRangeOfTopLevelDecl(D);

      //
      // mark this declaration as a piece of code
      //
      c.code(src_rng);

      if (debugMode) {
        llvm::errs() << "TopLevel " << D->getDeclKindName() << ' ';

        if (isa<NamedDecl>(D)) {
          NamedDecl *ND = cast<NamedDecl>(D);
          if (!ND->getName().empty())
            llvm::errs() << '\"' << ND->getName() << '\"';
        }

        llvm::errs() << '\n';
      }

      //
      // specially treat certain declarations
      //
      if (isa<FunctionDecl>(D)) {
        FunctionDecl *FD = cast<FunctionDecl>(D);
        //
        // note symbol
        //

        if (cast<FunctionDecl>(D)->getStorageClass() == SC_Static)
          c.static_code(src_rng, FD->getName(), FD->hasBody());
        else
          c.global_code(src_rng, FD->getName(), FD->hasBody());

        // 
        // examine return type
        //
        needsType(src_rng, FD->getReturnType().getTypePtrOrNull());
      } else if (isa<VarDecl>(D)) {
        //
        // global symbol across object files
        //

        VarDecl *VD = cast<VarDecl>(D);
        c.global_code(src_rng, VD->getName(), !VD->hasExternalStorage());
      } else if (isa<TypedefDecl>(D)) {
        //
        // examine type being typedef'd
        //

        TypedefDecl *TD = cast<TypedefDecl>(D);
        needsType(src_rng, TD->getUnderlyingType().getTypePtrOrNull());
      }

      //
      // traverse declaration's insides
      //
      Visitor.TraverseDecl(*b);
    }

    return true;
  }

  void HandleTranslationUnit(ASTContext &Context) {
    //
    // handle preprocessor ifdef, ifndef, if defined at the end, because we only
    // want to consider those uses which fall within top-level declarations and
    // we'll only be able to know that at the end
    //
    for (auto &user_usee_pair : if_def_uses) {
      clang_source_range_t &user = user_usee_pair.first;
      clang_source_range_t &usee = user_usee_pair.second;

      if (debugMode)
        llvm::errs() << "MacroIfDef " << user << ' ' << usee << '\n';

      c.use_if_user_exists(user, usee);
    }

    c.write_carbon_output();
  }
};

class CarbonCollectAction : public PluginASTAction {
public:
  CarbonCollectAction() {}

  unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                            llvm::StringRef file) override {
    // XXX
    gl_SM = &CI.getSourceManager();

#if 0
    llvm::outs() << "CI.getFrontendOpts().LLVMArgs:" << '\n';
    for (const auto& p : CI.getInvocation().getPreprocessorOpts().Macros) {
      string macronm;
      bool isundef;
      tie(macronm, isundef) = move(p);
      llvm::outs() << "  -" << (isundef ? 'U' : 'D') << macronm << '\n';
    }
#endif
    c.set_invocation_macros(CI.getInvocation().getPreprocessorOpts().Macros);

#if 0
    llvm::errs() << "header search options:\n";
    llvm::errs() << "  Sysroot: " << CI.getHeaderSearchOpts().Sysroot << '\n';
    llvm::errs() << "  SystemHeaderPrefixes:\n";
    for (const HeaderSearchOptions::SystemHeaderPrefix &p :
         CI.getHeaderSearchOpts().SystemHeaderPrefixes)
      llvm::errs() << "    " << p.Prefix << '\n';
    llvm::errs() << "  UserEntries:\n";
    static const char *IncludeDirGroupStrings[] = {
        "Quoted",        "Angled",  "IndexHeaderMap", "System",
        "ExternCSystem", "CSystem", "CXXSystem",      "ObjCSystem",
        "ObjCXXSystem",  "After"};
    for (const HeaderSearchOptions::Entry &e :
         CI.getHeaderSearchOpts().UserEntries)
      llvm::errs() << "    " << e.Path << ' ' << IncludeDirGroupStrings[e.Group]
                   << '\n';
#endif

    std::set<std::string> hdr_dirs;

    for (const HeaderSearchOptions::Entry &h :
         CI.getHeaderSearchOpts().UserEntries)
      if (fs::is_directory(h.Path))
        hdr_dirs.insert(fs::canonical(h.Path).string());

    c.set_invocation_header_directories(hdr_dirs);

#if 0
    llvm::errs() << "SystemHeaderPrefixes:\n";
    for (const HeaderSearchOptions::SystemHeaderPrefix &h :
         CI.getHeaderSearchOpts().SystemHeaderPrefixes) {
      llvm::errs() << h.Prefix << ' '
                   << (fs::is_directory(h.Prefix) ? 'Y' : 'N') << '\n';
    }
#endif

#if 0
    llvm::errs() << "header dirs:\n";
    for (const string& d : hdr_dirs)
      llvm::errs() << "  " << d << '\n';
#endif

    return llvm::make_unique<CarbonCollectConsumer>(CI);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const vector<string> &args) override {
    SourceManager &SM = CI.getSourceManager();
    const StringRef& src = SM.getFileEntryForID(SM.getMainFileID())->getName();

    //
    // we only process C code
    //
    {
      const LangOptions &LO = CI.getLangOpts();
      bool is_c = !LO.CPlusPlus && !LO.CPlusPlus11 && !LO.CPlusPlus14 &&
                  !LO.CPlusPlus17 && !LO.CPlusPlus2a && !LO.ObjC1 && !LO.ObjC2;
      if (!is_c) {
        llvm::outs() << "collect: skipping " << src << '\n';
        return false;
      }
    }

    //
    // parse arguments
    //
    if (args.size() != 2 ||
        !fs::is_directory(args[0]) ||
        !fs::is_directory(args[1])) {
      llvm::errs() << "Usage: "
                      "-load carbon-collect.so "
                      "-add-plugin carbon-collect "
                      "-plugin-arg-carbon-collect root_source_directory "
                      "-plugin-arg-carbon-collect root_build_directory\n";
      return false;
    }

    root_src_dir = fs::canonical(args[0]);
    root_bin_dir = fs::canonical(args[1]);

    c.set_args(fs::canonical(fs::path(src.str())), root_src_dir, root_bin_dir);

    gl_SM = &CI.getSourceManager(); /* XXX */
    return true;
  }
};

struct MultipliersForFileIDs {
  int Curr;
  map<FileID, int> FIDMap;

  MultipliersForFileIDs() : Curr(0) {}
};

/// Note: SM assigns unique FileID's for each unique \#include chain.
static map<llvm::sys::fs::UniqueID, MultipliersForFileIDs> FileMultMap;

bool is_counterpart(const clang_source_range_t &lhs,
                    const clang_source_range_t &rhs) {
  if ((lhs.end - lhs.beg) != (rhs.end - rhs.beg))
    return false;

  SourceManager &SM = *gl_SM;

  if (SM.getFileEntryForID(lhs.f)->getUniqueID() !=
      SM.getFileEntryForID(rhs.f)->getUniqueID())
    return false;

  StringRef MB = SM.getBufferData(lhs.f);
  return lhs.beg % static_cast<int>(MB.size()) ==
         rhs.beg % static_cast<int>(MB.size());
}

clang_source_range_t
normalize_source_range(const clang_source_range_t &cl_src_rng) {
  SourceManager &SM = *gl_SM;

  int beg =
      cl_src_rng.beg % static_cast<int>(SM.getBufferData(cl_src_rng.f).size());
  return {cl_src_rng.f, beg, beg + (cl_src_rng.end - cl_src_rng.beg)};
}

clang_source_range_t clang_source_range(const SourceRange &SR) {
  SourceManager &SM = *gl_SM;

  FileID FID;
  int beg, end;
  {
    pair<FileID, unsigned> begInfo = SM.getDecomposedExpansionLoc(SR.getBegin());
    pair<FileID, unsigned> endInfo = SM.getDecomposedExpansionLoc(SR.getEnd());

    if (begInfo.first != endInfo.first)
      abort();

    FID = begInfo.first;

    beg = static_cast<int>(begInfo.second);
    end = static_cast<int>(endInfo.second) + 1;
  }

  MultipliersForFileIDs &Mults =
      FileMultMap[SM.getFileEntryForID(FID)->getUniqueID()];

  if (Mults.FIDMap.find(FID) == Mults.FIDMap.end())
    Mults.FIDMap[FID] = Mults.Curr++;

  int M = Mults.FIDMap[FID];
  int N = static_cast<int>(SM.getBufferData(FID).size());

  beg += M * N;
  end += M * N;

  return {FID, beg, end};
}

clang_source_file_t clang_source_file(FileID fid) {
  return fid;
}

size_t hash_of_clang_source_file(const clang_source_file_t &f) {
  return f.getHashValue();
}

fs::path path_of_clang_source_file(const clang_source_file_t &f) {
  SourceManager &SM = *gl_SM;

  const FileEntry *FE = SM.getFileEntryForID(f);
  if (!FE)
    return fs::path();

  fs::path p(FE->getName());
  return fs::canonical(p);
}

bool clang_is_system_source_file(const clang_source_file_t &f) {
  SourceManager &SM = *gl_SM;

  bool Invalid = false;
  const SrcMgr::SLocEntry &SEntry = SM.getSLocEntry(f, &Invalid);
  return !Invalid && SEntry.isFile() &&
         SrcMgr::isSystem(SEntry.getFile().getFileCharacteristic());
}

unsigned
get_backwards_offset_to_new_line(const clang_source_range_t &cl_src_rng) {
  SourceManager &SM = *gl_SM;

  int beg =
      cl_src_rng.beg % static_cast<int>(SM.getBufferData(cl_src_rng.f).size());

  char ch;
  int pos = beg;
  do {
    --pos;
    ch = character_at_clang_file_offset(
        cl_src_rng.f, static_cast<clang_source_location_t>(pos));
  } while (ch != '\r' && ch != '\n' && pos > 0);

  return static_cast<unsigned>(beg - pos - 1);
}

unsigned
get_forwards_offset_to_new_line(const clang_source_range_t &cl_src_rng) {
  SourceManager &SM = *gl_SM;

  int len = cl_src_rng.end - cl_src_rng.beg;
  int beg =
      cl_src_rng.beg % static_cast<int>(SM.getBufferData(cl_src_rng.f).size());
  int end = beg + len;

  char ch;
  int pos = end;
  do {
    ch = character_at_clang_file_offset(
        cl_src_rng.f, static_cast<clang_source_location_t>(pos++));
  } while (ch != '\r' && ch != '\n' && ch != '\0');

  return static_cast<unsigned>(pos - end - 1);
}

unsigned char_count_until_semicolon(const clang_source_range_t &cl_src_rng) {
  SourceManager &SM = *gl_SM;

  StringRef MB = SM.getBufferData(cl_src_rng.f);

  int len = cl_src_rng.end - cl_src_rng.beg;
  int beg = cl_src_rng.beg % static_cast<int>(MB.size());
  int end = beg + len;

  char ch;
  unsigned cnt = 0;
  clang_source_location_t pos = static_cast<clang_source_location_t>(end);
  do {
    if (pos == static_cast<clang_source_location_t>(MB.size()))
      return 0;

    ++cnt;
    ch = character_at_clang_file_offset(cl_src_rng.f, pos++);
  } while (ch != ';');

  return cnt;
}

char character_at_clang_file_offset(const clang_source_file_t &f,
                                    const clang_source_location_t &off) {
  SourceManager &SM = *gl_SM;

  return SM.getBufferData(f)[static_cast<unsigned>(off)];
}

clang_source_file_t top_level_system_header(const clang_source_file_t &f) {
  SourceManager &SM = *gl_SM;

  bool invalid = false;
  const SrcMgr::SLocEntry &sloc = SM.getSLocEntry(f, &invalid);
  assert(!invalid && sloc.isFile());

  const SrcMgr::FileInfo &fileInfo = sloc.getFile();
  assert(!fileInfo.getIncludeLoc().isInvalid());

  FileID incFid = SM.getFileID(fileInfo.getIncludeLoc());
  assert(!incFid.isInvalid());

  const SrcMgr::SLocEntry &incSloc = SM.getSLocEntry(incFid, &invalid);
  assert(!invalid && incSloc.isFile());

  if (incSloc.getFile().getFileCharacteristic() == SrcMgr::C_User)
    return f;

#if 0
  llvm::errs() << "  "
               << path_of_clang_source_file(clang_source_file(incFid)).string()
               << "  $$$\n";
#endif
  return top_level_system_header(clang_source_file(incFid));
}

#if 0
llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const clang_source_file_t &f) {
  fs::path srcfp(path_of_clang_source_file(f));

  if (clang_is_system_source_file(f))
    return os << srcfp.string();
  else
    return os << fs::relative(srcfp, root_src_dir).string();
}
#endif

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const clang_source_range_t &cl_src_rng) {
  SourceManager &SM = *gl_SM;

  int beg = cl_src_rng.beg;
  int end = cl_src_rng.end;

  int N = static_cast<int>(SM.getBufferData(cl_src_rng.f).size());

  bool normalized = false;
  if (beg > N) {
    normalized = true;

    int len = cl_src_rng.end - cl_src_rng.beg;
    beg = beg % N;
    end = beg + len;
  }

  const FileEntry *FE = SM.getFileEntryForID(cl_src_rng.f);

  os << '[';

  if (clang_is_system_source_file(cl_src_rng.f)) {
    os << FE->getName();
  } else {
    os << fs::relative(FE->getName().str(), root_src_dir).string();
  }

  os << ' ';

  if (normalized)
    os << '#' << ' ';

  os << beg << ':' << end;
  os << ']';

  return os;
}

static bool _isSourceRangeSensible(const SourceRange &SR) {
  SourceManager &SM = *gl_SM;

  pair<FileID, unsigned> beg = SM.getDecomposedExpansionLoc(SR.getBegin());
  pair<FileID, unsigned> end = SM.getDecomposedExpansionLoc(SR.getEnd());

  return beg.first == end.first && SM.getFileEntryForID(beg.first);
}

static void needsDecl(const clang_source_range_t &user, const Decl *D) {
  if (!D || !_isSourceRangeSensible(D->getSourceRange()))
    return;

  clang_source_range_t usee(clang_source_range(D->getSourceRange()));

  if (debugMode) {
    llvm::errs() << "  " << user << " ---> ";
    if (isa<NamedDecl>(D)) {
      const NamedDecl *ND = cast<const NamedDecl>(D);
      if (!ND->getName().empty())
        llvm::errs() << '\"' << ND->getName() << '\"' << ' ';
    }
    llvm::errs() << usee << '\n';
  }

  c.use(user, usee);
}

static void needsPointerType(const clang_source_range_t &user,
                             const PointerType *ty) {
  //
  // if this is a pointer to a record type, then we only need the forward
  // declaration of it
  //
  if (ty->getPointeeType().getTypePtrOrNull()->getTypeClass() == Type::Record) {
    needsDecl(user,
              cast<RecordType>(ty->getPointeeType().getTypePtr())->getDecl());
    return;
  }

  needsType(user, ty->getPointeeType().getTypePtrOrNull());
}

static void needsTagDecl(const clang_source_range_t &user, const TagDecl *TD) {
  if (TD->getDefinition())
    needsDecl(user, TD->getDefinition());
  else
    needsDecl(user, TD);
}

static void needsFunctionProtoType(const clang_source_range_t &user,
                                   const FunctionProtoType *ty) {
  for (const auto& qty : ty->getParamTypes())
    needsType(user, qty.getTypePtrOrNull());

  needsType(user, ty->getReturnType().getTypePtrOrNull());
}

static void needsRecordType(const clang_source_range_t &user,
                            const RecordType *ty) {
  needsTagDecl(user, ty->getDecl());
}
static void needsEnumType(const clang_source_range_t &user,
                          const EnumType *ty) {
  needsTagDecl(user, ty->getDecl());
}
static void needsConstantArrayType(const clang_source_range_t &user,
                                   const ConstantArrayType *ty) {
  needsType(user, ty->getElementType().getTypePtrOrNull());
}
static void needsIncompleteArrayType(const clang_source_range_t &user,
                                     const IncompleteArrayType *ty) {
  needsType(user, ty->getElementType().getTypePtrOrNull());
}
static void needsVariableArrayType(const clang_source_range_t &user,
                                   const VariableArrayType *ty) {
  needsType(user, ty->getElementType().getTypePtrOrNull());
}
static void needsElaboratedType(const clang_source_range_t &user,
                                const ElaboratedType *ty) {
  needsType(user, ty->desugar().getTypePtrOrNull());
}
static void needsParenType(const clang_source_range_t &user,
                           const ParenType *ty) {
  needsType(user, ty->desugar().getTypePtrOrNull());
}

static void needsTypedefType(const clang_source_range_t &user,
                             const TypedefType *ty) {
  needsType(user, ty->desugar().getTypePtrOrNull());
  needsDecl(user, ty->getDecl());
}

static void needsType(const clang_source_range_t& user, const Type* T) {
  assert(T);

#if 0
  if (!T) {
    llvm::errs() << "warning: NOTYDCL <" << T->getTypeClassName() << "> "
                 << user << '\n';
    return;
  }

  if (debugMode)
    llvm::errs() << T->getTypeClassName() << ' ';
#endif

  switch (T->getTypeClass()) {
#define TYPE(Class)                                                            \
  case Type::Class:                                                            \
    return needs##Class##Type(user, cast<const Class##Type>(T));

#include "TypeNodes.def"

  default:
    break;
  }
}

static FrontendPluginRegistry::Add<CarbonCollectAction>
    X("carbon-collect", "collect code for carbon copying a function");

}
