#pragma once
#include <clang/Basic/SourceLocation.h>
#include <clang/AST/ASTContext.h>
#include <string>

//
// NOTE: this function has been observed to trigger a stack overflow when the
// source location is musl/src/errno/strerror.c:12 (this is what the code looks
// like:)
//
// static const char errmsg[] =
// #include "__strerror.h"
// ;
//

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
