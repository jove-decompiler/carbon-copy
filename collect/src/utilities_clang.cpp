#include "utilities_clang.h"
#include <clang/Lex/Lexer.h>
#include <sstream>
#include <clang/Basic/SourceManager.h>

using namespace clang;
using namespace std;

namespace carbon {

/* the following functions are from clang/lib/ARCMigrate/Transforms.cpp */

/// 'Loc' is the end of a statement range. This returns the location
/// immediately after the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findLocationAfterSemi(SourceLocation loc,
                                     ASTContext &Ctx, bool IsDecl) {
  SourceLocation SemiLoc = findSemiAfterLocation(loc, Ctx, IsDecl);
  if (SemiLoc.isInvalid())
    return SourceLocation();
  return SemiLoc.getLocWithOffset(1);
}

/// \arg Loc is the end of a statement range. This returns the location
/// of the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
SourceLocation findSemiAfterLocation(SourceLocation loc,
                                     ASTContext &Ctx,
                                     bool IsDecl) {
  SourceManager &SM = Ctx.getSourceManager();
  if (loc.isMacroID()) {
    if (!Lexer::isAtEndOfMacroExpansion(loc, SM, Ctx.getLangOpts(), &loc))
      return SourceLocation();
  }

#if 0
  {
    pair<FileID, unsigned> info = SM.getDecomposedExpansionLoc(loc);
    llvm::errs() << "findSemiAfterLocation (1): "
                 << SM.getFileEntryForID(info.first)->tryGetRealPathName()
                 << " " << info.second << "\n";
  }
#endif

  loc = Lexer::getLocForEndOfToken(loc, /*Offset=*/0, SM, Ctx.getLangOpts());

#if 0
  {
    pair<FileID, unsigned> info = SM.getDecomposedExpansionLoc(loc);
    llvm::errs() << "findSemiAfterLocation (2): "
                 << SM.getFileEntryForID(info.first)->tryGetRealPathName()
                 << " " << info.second << "\n";
  }
#endif

  // Break down the source location.
  std::pair<FileID, unsigned> locInfo = SM.getDecomposedLoc(loc);

  // Try to load the file buffer.
  bool invalidTemp = false;
  StringRef file = SM.getBufferData(locInfo.first, &invalidTemp);
  if (invalidTemp)
    return SourceLocation();

  const char *tokenBegin = file.data() + locInfo.second;

  Token tok;
  {
  // Lex from the start of the given location.
  Lexer lexer(SM.getLocForStartOfFile(locInfo.first),
              Ctx.getLangOpts(),
              file.begin(), tokenBegin, file.end());
  lexer.LexFromRawLexer(tok);
  }
  if (tok.isNot(tok::semi)) {
    if (!IsDecl)
      return SourceLocation();
    // Declaration may be followed with other tokens; such as an __attribute,
    // before ending with a semicolon.
    __attribute__((musttail)) return findSemiAfterLocation(
        tok.getLocation(), Ctx, /*IsDecl*/ true);
  }

#if 0
  {
    pair<FileID, unsigned> info = SM.getDecomposedExpansionLoc(tok.getLocation());
    llvm::errs() << "findSemiAfterLocation (3): "
                 << SM.getFileEntryForID(info.first)->tryGetRealPathName()
                 << " " << info.second << "\n";
  }
#endif

  return tok.getLocation();
}

}
