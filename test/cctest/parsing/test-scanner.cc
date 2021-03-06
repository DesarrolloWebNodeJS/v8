// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests v8::internal::Scanner. Note that presently most unit tests for the
// Scanner are in cctest/test-parsing.cc, rather than here.

#include "src/handles-inl.h"
#include "src/objects-inl.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/parsing/scanner.h"
#include "src/unicode-cache.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

namespace {

const char src_simple[] = "function foo() { var x = 2 * a() + b; }";

struct ScannerTestHelper {
  ScannerTestHelper() = default;
  ScannerTestHelper(ScannerTestHelper&& other) V8_NOEXCEPT
      : unicode_cache(std::move(other.unicode_cache)),
        stream(std::move(other.stream)),
        scanner(std::move(other.scanner)) {}

  std::unique_ptr<UnicodeCache> unicode_cache;
  std::unique_ptr<CharacterStream<uint8_t>> stream;
  std::unique_ptr<Scanner> scanner;

  Scanner* operator->() const { return scanner.get(); }
  Scanner* get() const { return scanner.get(); }
};

ScannerTestHelper make_scanner(const char* src) {
  ScannerTestHelper helper;
  helper.unicode_cache = std::unique_ptr<UnicodeCache>(new UnicodeCache);
  helper.stream = ScannerStream::ForTesting(src);
  helper.scanner = std::unique_ptr<Scanner>(
      new Scanner(helper.unicode_cache.get(), helper.stream.get(), false));
  helper.scanner->Initialize();
  return helper;
}

}  // anonymous namespace

// CHECK_TOK checks token equality, but by checking for equality of the token
// names. That should have the same result, but has much nicer error messaages.
#define CHECK_TOK(a, b) CHECK_EQ(Token::Name(a), Token::Name(b))

TEST(Bookmarks) {
  // Scan through the given source and record the tokens for use as reference
  // below.
  std::vector<Token::Value> tokens;
  {
    auto scanner = make_scanner(src_simple);
    do {
      tokens.push_back(scanner->Next());
    } while (scanner->current_token() != Token::EOS);
  }

  // For each position:
  // - Scan through file,
  // - set a bookmark once the position is reached,
  // - scan a bit more,
  // - reset to the bookmark, and
  // - scan until the end.
  // At each step, compare to the reference token sequence generated above.
  for (size_t bookmark_pos = 0; bookmark_pos < tokens.size(); bookmark_pos++) {
    auto scanner = make_scanner(src_simple);
    Scanner::BookmarkScope bookmark(scanner.get());

    for (size_t i = 0; i < std::min(bookmark_pos + 10, tokens.size()); i++) {
      if (i == bookmark_pos) {
        bookmark.Set();
      }
      CHECK_TOK(tokens[i], scanner->Next());
    }

    bookmark.Apply();
    for (size_t i = bookmark_pos; i < tokens.size(); i++) {
      CHECK_TOK(tokens[i], scanner->Next());
    }
  }
}

TEST(AllThePushbacks) {
  const struct {
    const char* src;
    const Token::Value tokens[5];  // Large enough for any of the test cases.
  } test_cases[] = {
      {"<-x", {Token::LT, Token::SUB, Token::IDENTIFIER, Token::EOS}},
      {"<!x", {Token::LT, Token::NOT, Token::IDENTIFIER, Token::EOS}},
      {"<!-x",
       {Token::LT, Token::NOT, Token::SUB, Token::IDENTIFIER, Token::EOS}},
      {"<!-- xx -->\nx", {Token::IDENTIFIER, Token::EOS}},
  };

  for (const auto& test_case : test_cases) {
    auto scanner = make_scanner(test_case.src);
    for (size_t i = 0; test_case.tokens[i] != Token::EOS; i++) {
      CHECK_TOK(test_case.tokens[i], scanner->Next());
    }
    CHECK_TOK(Token::EOS, scanner->Next());
  }
}

TEST(ContextualKeywordTokens) {
  auto scanner = make_scanner("function of get bla");

  // function (regular keyword)
  scanner->Next();
  CHECK_TOK(Token::FUNCTION, scanner->current_token());
  CHECK_TOK(Token::UNINITIALIZED, scanner->current_contextual_token());

  // of (contextual keyword)
  scanner->Next();
  CHECK_TOK(Token::IDENTIFIER, scanner->current_token());
  CHECK_TOK(Token::OF, scanner->current_contextual_token());

  // get (contextual keyword)
  scanner->Next();
  CHECK_TOK(Token::IDENTIFIER, scanner->current_token());
  CHECK_TOK(Token::GET, scanner->current_contextual_token());

  // bla (identfier, not any sort of keyword)
  scanner->Next();
  CHECK_TOK(Token::IDENTIFIER, scanner->current_token());
  CHECK_TOK(Token::UNINITIALIZED, scanner->current_contextual_token());
}

}  // namespace internal
}  // namespace v8
