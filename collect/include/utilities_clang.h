#pragma once
#include <clang/Basic/SourceLocation.h>
#include <clang/AST/ASTContext.h>
#include <string>

namespace carbon {

/// Loc is the end of a statement range. This returns the location
/// of the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
clang::SourceLocation findSemiAfterLocation(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);

/// 'Loc' is the end of a statement range. This returns the location
/// immediately after the semicolon following the statement.
/// If no semicolon is found or the location is inside a macro, the returned
/// source location will be invalid.
clang::SourceLocation findLocationAfterSemi(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);
}
