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

clang::SourceLocation findSemiAfterLocation(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);

clang::SourceLocation findLocationAfterSemi(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);
}
