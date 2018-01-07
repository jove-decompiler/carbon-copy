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

struct clang_source_file_priv_t {
  FileID fid;

  clang_source_file_priv_t(FileID fid) : fid(fid) {}
};

static collector c;

// we keep a list of macro uses to apply at the close since the preprocessor
// will expand macros before the parser will notify of the AST's therein
static list<pair<clang_source_range_t, clang_source_range_t>> if_def_uses;

//
// stores most-recent #define for a given macro
//
static unordered_map<string, clang_source_range_t> macro_defs;
static unordered_map<string, SourceRange> _macro_defs;

static clang_source_file_t clang_source_file(FileID);
static clang_source_range_t clang_source_range(const SourceRange &);
static SourceManager *gl_SM;
static llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const clang_source_file_t &f);
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
      llvm::errs() << "  MemberExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()),
              dyn_cast<FieldDecl>(e->getMemberDecl())->getParent());

    return true;
  }

  bool VisitCStyleCastExpr(CStyleCastExpr *e) {
    if (debugMode)
      llvm::errs() << "  CStyleCastExpr\n";

    needsType(clang_source_range(e->getSourceRange()),
              e->getTypeAsWritten().getTypePtrOrNull());

    return true;
  }

  bool VisitCallExpr(CallExpr *e) {
    if (debugMode)
      llvm::errs() << "  CallExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()), e->getCalleeDecl());

    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr *e) {
    if (debugMode)
      llvm::errs() << "  VisitDeclRefExpr\n";

    needsDecl(clang_source_range(e->getSourceRange()), e->getDecl());

    return true;
  }

  bool VisitFieldDecl(FieldDecl *D) {
    if (debugMode)
      llvm::errs() << "  FieldDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getTypeSourceInfo()->getType().getTypePtrOrNull());

    return true;
  }

  bool VisitVarDecl(VarDecl *D) {
    if (debugMode)
      llvm::errs() << "  VarDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getTypeSourceInfo()->getType().getTypePtrOrNull());

    return true;
  }

  bool VisitParmVarDecl(ParmVarDecl *D) {
    if (debugMode)
      llvm::errs() << "  ParmVarDecl\n";

    needsType(clang_source_range(D->getSourceRange()),
              D->getOriginalType().getTypePtrOrNull());

    return true;
  }
};

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

  //
  // Hook called whenever a macro definition is seen.
  //
  void MacroDefined(const Token &MacroNameTok, const MacroDirective *MD) {
    const MacroInfo *MI = MD->getMacroInfo();

    if (!MI || MI->isBuiltinMacro() || isInBuiltin(MI->getDefinitionLoc()))
      return;

    SourceRange SR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

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

    if (debugMode)
      llvm::errs() << "MacroDefined "
                   << MacroNameTok.getIdentifierInfo()->getName() << ' '
                   << src_rng << '\n';
    c.code(normalize_source_range(src_rng));

    //
    // if the macro here is being redefined, we need to make sure that all code
    // which uses the previous definition is, in turn, used by the new
    // redefinition to preserve the textual ordering when we perform a
    // topological sort of the dependency graph
    //
    auto it =
        macro_defs.find(MacroNameTok.getIdentifierInfo()->getName().str());
    if (it != macro_defs.end() && !is_counterpart(src_rng, (*it).second)) {
      if (debugMode) {
        ostringstream buff;
        buff << "MacroRedefined "
             << MacroNameTok.getIdentifierInfo()->getName().str();
        string s(buff.str());
        string sp(s.size(), ' ');

        llvm::errs()
            << s
            << _macro_defs[MacroNameTok.getIdentifierInfo()->getName().str()]
                   .getBegin()
                   .printToString(SM)
            << '\n' << sp << SR.getBegin().printToString(SM) << '\n' << sp
            << (*it).second << '\n' << sp << src_rng << '\n';
      }

      c.follow_users_of((*it).second, src_rng);
    }

    macro_defs[MacroNameTok.getIdentifierInfo()->getName().str()] = src_rng;
    _macro_defs[MacroNameTok.getIdentifierInfo()->getName().str()] = SR;
  }

  //
  // Hook called whenever a macro invocation is found.
  //
  void MacroExpands(const Token &MacroNameTok, const MacroDefinition &MD,
                    SourceRange userSR, const MacroArgs *Args) {
    const MacroInfo *MI = MD.getMacroInfo();

    if (!MI || MI->isBuiltinMacro() || isInBuiltin(MI->getDefinitionLoc()) ||
        isInBuiltin(userSR.getBegin()))
      return;

    SourceRange useeSR(MI->getDefinitionLoc(), MI->getDefinitionEndLoc());

    clang_source_range_t user(clang_source_range(userSR));
    clang_source_range_t usee(clang_source_range(useeSR));
    usee.end = usee.beg +
               static_cast<clang_source_location_t>(
                   Lexer::getSourceText(CharSourceRange::getTokenRange(useeSR),
                                        SM, CI.getLangOpts(), nullptr)
                       .size());
    usee.beg -= get_backwards_offset_to_new_line(usee);

    c.use(user, normalize_source_range(usee));

    if (debugMode)
      llvm::errs() << "MacroExpands "
                   << MacroNameTok.getIdentifierInfo()->getName() << ' ' << user
                   << " ---> " << usee << '\n';
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
    if (!(isa<FunctionDecl>(D) && cast<FunctionDecl>(D)->doesThisDeclarationHaveABody())) {
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

      if (isInBuiltin(D->getLocStart()) || D->getKind() == Decl::Empty)
        return true;

      clang_source_range_t src_rng = sourceRangeOfTopLevelDecl(D);

      //
      // mark this declaration as a piece of code
      //
      c.code(src_rng);

      if (debugMode) {
        llvm::errs() << "TopLevel " << D->getDeclKindName() << ' ';

        if (isa<NamedDecl>(D)) {
          NamedDecl *ND = cast<NamedDecl>(D);
          llvm::errs() << ND->getName() << ' ';
        }

        llvm::errs() << src_rng << '\n';
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

    set<string> hdr_dirs;
    for (const HeaderSearchOptions::Entry &e :
         CI.getHeaderSearchOpts().UserEntries)
      if (fs::is_directory(e.Path))
        hdr_dirs.insert(fs::canonical(e.Path).string());

#if 0
    llvm::errs() << "header dirs:\n";
    for (const string& d : hdr_dirs)
      llvm::errs() << "  " << d << '\n';
#endif

    c.set_invocation_header_directories(hdr_dirs);

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
                  !LO.CPlusPlus1z && !LO.CPlusPlus2a && !LO.ObjC1 && !LO.ObjC2;
      if (!is_c) {
        llvm::outs() << "collect: skipping " << src << '\n';
        return false;
      }
    }

    //
    // parse arguments
    //
    fs::path root_src_dir(args[0]);
    fs::path root_bin_dir(args[1]);

    if (args.size() != 2 ||
        !fs::is_directory(root_src_dir) ||
        !fs::is_directory(root_bin_dir)) {
      llvm::errs() << "Usage: "
        "-load carbon-collect.so "
        "-add-plugin carbon-collect "
        "-plugin-arg-carbon-collect root_source_directory "
        "-plugin-arg-carbon-collect root_build_directory\n";
      return false;
    }

    c.set_args(fs::canonical(fs::path(src.str())),
               fs::canonical(root_src_dir),
               fs::canonical(root_bin_dir));

    gl_SM = &CI.getSourceManager(); /* XXX */
    return true;
  }
};

struct file_entry_unique_id_hasher {
  size_t operator()(const llvm::sys::fs::UniqueID& uid) const {
    size_t res = 23;
    res = res * 31 + uid.getDevice();
    res = res * 31 + uid.getFile();
    return res;
  }
};

struct file_id_hasher {
  size_t operator()(const FileID& fid) const {
    return fid.getHashValue();
  }
};

static unordered_map<FileID, unsigned, file_id_hasher> src_rng_mult_map;
static unordered_map<llvm::sys::fs::UniqueID, unsigned,
                     file_entry_unique_id_hasher>
    src_f_mult_map;

bool is_counterpart(const clang_source_range_t &cl_src_rng,
                    const clang_source_range_t &other_cl_src_rng) {
  if (gl_SM->getFileEntryForID(cl_src_rng.f.priv->fid)->getUniqueID() !=
      gl_SM->getFileEntryForID(other_cl_src_rng.f.priv->fid)->getUniqueID())
    return false;

  if ((cl_src_rng.end - cl_src_rng.beg) !=
      (other_cl_src_rng.end - other_cl_src_rng.beg))
    return false;

  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  return cl_src_rng.beg % static_cast<long>(MB.size()) ==
         other_cl_src_rng.beg % static_cast<long>(MB.size());
}

clang_source_range_t
normalize_source_range(const clang_source_range_t &cl_src_rng) {
  int n = cl_src_rng.end - cl_src_rng.beg;
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  int beg = cl_src_rng.beg % static_cast<long>(MB.size());

  return {cl_src_rng.f, beg, beg + n};
}

clang_source_range_t clang_source_range(const SourceRange &SR) {
  assert(SR.getBegin().isValid());
  assert(SR.getEnd().isValid());

  auto begSR = SR.getBegin();
  auto endSR = SR.getEnd();

  pair<FileID, unsigned> beginInfo = gl_SM->getDecomposedExpansionLoc(begSR);
  pair<FileID, unsigned> endInfo = gl_SM->getDecomposedExpansionLoc(endSR);

  assert(beginInfo.first == endInfo.first);

  FileID fid = beginInfo.first;
  llvm::StringRef MB = gl_SM->getBufferData(fid);

  unsigned mult;
  auto it = src_rng_mult_map.find(fid);
  if (it != src_rng_mult_map.end()) {
    mult = (*it).second;
  } else {
    const FileEntry *FE = gl_SM->getFileEntryForID(fid);

    auto currmul_it = src_f_mult_map.find(FE->getUniqueID());
    if (currmul_it == src_f_mult_map.end())
      currmul_it = src_f_mult_map.insert({FE->getUniqueID(), 0}).first;

    unsigned &currmult = (*currmul_it).second;

    src_rng_mult_map[fid] = currmult;
    mult = currmult;

    ++currmult;
  }

  int beg = static_cast<int>(beginInfo.second);
  int end = static_cast<int>(endInfo.second) + 1;

  size_t n = MB.size();

  beg += n*mult;
  end += n*mult;

  return {clang_source_file(beginInfo.first), beg, end};
}

clang_source_file_t clang_source_file(FileID fid) {
  clang_source_file_t res;
  res.priv.reset(new clang_source_file_priv_t(fid));
  return res;
}

clang_source_file_t::clang_source_file_t() : priv(nullptr) {}

clang_source_file_t::clang_source_file_t(const clang_source_file_t &other)
    : priv(new clang_source_file_priv_t(other.priv->fid)) {}

clang_source_file_t::~clang_source_file_t() {}

clang_source_file_t &clang_source_file_t::
operator=(const clang_source_file_t &rhs) {
  priv.reset(new clang_source_file_priv_t(rhs.priv->fid));

  return *this;
}

bool clang_source_file_t::operator==(const clang_source_file_t &rhs) const {
  if (!priv && !rhs.priv)
    return true;

  if (priv && rhs.priv) {
    const FileEntry *FE = gl_SM->getFileEntryForID(priv->fid);
    const FileEntry *rhs_FE = gl_SM->getFileEntryForID(rhs.priv->fid);

    return FE->getUniqueID() == rhs_FE->getUniqueID();
  }

  return false;
}

size_t hash_of_clang_source_file(const clang_source_file_t &f) {
  if (!f.priv)
    return 0;

  const FileEntry *FE = gl_SM->getFileEntryForID(f.priv->fid);

  size_t hash = 23;
  hash = hash * 31 + FE->getUniqueID().getDevice();
  hash = hash * 31 + FE->getUniqueID().getFile();
  return hash;
}

fs::path path_of_clang_source_file(const clang_source_file_t &f) {
  const FileEntry *FE = gl_SM->getFileEntryForID(f.priv->fid);
  fs::path p(FE->getName());
  return fs::canonical(p);
}

string source_of_clang_source_range(const clang_source_range_t &cl_src_rng) {
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  return MB
      .substr(static_cast<size_t>(cl_src_rng.beg) % MB.size(),
              static_cast<size_t>(cl_src_rng.end - cl_src_rng.beg))
      .str();
}

bool clang_is_system_source_file(const clang_source_file_t &f) {
  bool Invalid = false;
  const SrcMgr::SLocEntry &SEntry = gl_SM->getSLocEntry(f.priv->fid, &Invalid);
  if (Invalid || !SEntry.isFile())
    return false;

  return SEntry.getFile().getFileCharacteristic() != SrcMgr::C_User;
}

unsigned
get_backwards_offset_to_new_line(const clang_source_range_t &cl_src_rng) {
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  unsigned beg = static_cast<unsigned>(cl_src_rng.beg) % MB.size();

  char ch;
  unsigned pos = beg;
  do {
    --pos;
    ch = character_at_clang_file_offset(
        cl_src_rng.f, static_cast<clang_source_location_t>(pos));
  } while (ch != '\r' && ch != '\n' && pos > 0);

  return beg - pos - 1;
}

unsigned
get_forwards_offset_to_new_line(const clang_source_range_t &cl_src_rng) {
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  unsigned len = static_cast<unsigned>(cl_src_rng.end - cl_src_rng.beg);
  unsigned beg = static_cast<unsigned>(cl_src_rng.beg) % MB.size();
  unsigned end = beg + len;

  char ch;
  unsigned pos = end;
  do {
    ch = character_at_clang_file_offset(
        cl_src_rng.f, static_cast<clang_source_location_t>(pos++));
  } while (ch != '\r' && ch != '\n' && ch != '\0');

  return pos - end - 1;
}

unsigned char_count_until_semicolon(const clang_source_range_t &cl_src_rng) {
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  unsigned len = static_cast<unsigned>(cl_src_rng.end - cl_src_rng.beg);
  unsigned beg = static_cast<unsigned>(cl_src_rng.beg) % MB.size();
  unsigned end = beg + len;

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
  llvm::StringRef MB = gl_SM->getBufferData(f.priv->fid);
  return MB[static_cast<unsigned>(off)];
}

clang_source_file_t top_level_system_header(const clang_source_file_t &f) {
  bool invalid = false;
  const SrcMgr::SLocEntry &sloc = gl_SM->getSLocEntry(f.priv->fid, &invalid);
  assert(!invalid && sloc.isFile());

  const SrcMgr::FileInfo &fileInfo = sloc.getFile();
  assert(!fileInfo.getIncludeLoc().isInvalid());

  FileID incFid = gl_SM->getFileID(fileInfo.getIncludeLoc());
  assert(!incFid.isInvalid());

  const SrcMgr::SLocEntry &incSloc = gl_SM->getSLocEntry(incFid, &invalid);
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

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const clang_source_file_t &f) {
  return os << path_of_clang_source_file(f).string();
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                              const clang_source_range_t &cl_src_rng) {
  llvm::StringRef MB = gl_SM->getBufferData(cl_src_rng.f.priv->fid);
  unsigned len = static_cast<unsigned>(cl_src_rng.end - cl_src_rng.beg);
  unsigned beg = static_cast<unsigned>(cl_src_rng.beg) % MB.size();
  unsigned end = beg + len;

  return os << (clang_is_system_source_file(cl_src_rng.f) ? "*" : "") << "['"
            << cl_src_rng.f << "' " << cl_src_rng.beg << ':' << cl_src_rng.end
            << ' ' << beg << ':' << end << ']';
}

static void needsDecl(const clang_source_range_t &user, const Decl *D) {
  if (!D || D->getLocStart().isInvalid() || isInBuiltin(D->getLocStart()))
    return;

  clang_source_range_t usee(clang_source_range(D->getSourceRange()));

  if (debugMode) {
    llvm::errs() << user << " ---> ";
    if (isa<NamedDecl>(D)) {
      const NamedDecl *ND = cast<const NamedDecl>(D);
      llvm::errs() << '\'' << ND->getName() << '\'' << ' ';
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
  if (!T) {
    llvm::errs() << "warning: NOTYDCL <" << T->getTypeClassName() << "> "
                 << user << '\n';
    return;
  }

  if (debugMode)
    llvm::errs() << T->getTypeClassName() << ' ';

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
