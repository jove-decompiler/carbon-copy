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

template <typename T>
static void dec_source_range(T &src_rng, const unsigned N) {
  if (!(src_rng))
    return;

  auto beg = src_rng.beg;
  auto end = src_rng.end;

  using loc_t = decltype(beg);
  static_assert(std::is_same_v<loc_t, decltype(end)>);

  static_assert(std::is_integral_v<loc_t>);
  static_assert(std::is_signed_v<loc_t>);

  assert(beg >= 0);
  assert(end > 0);
  assert(end > beg);

  const loc_t n = static_cast<loc_t>(N);

  beg -= n;
  end -= n;

  src_rng.beg = beg;
  src_rng.end = end;
}

clang::SourceLocation findSemiAfterLocation(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);

clang::SourceLocation findLocationAfterSemi(clang::SourceLocation loc,
                                            clang::ASTContext &Ctx,
                                            bool IsDecl);
}
