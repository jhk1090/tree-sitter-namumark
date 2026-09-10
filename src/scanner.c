#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

enum TokenType {
  NOWIKI_START,
  NOWIKI_END,
  NOWIKI_NEST_OPEN,
  NOWIKI_NEST_CLOSE,
  IF_BLOCK_START,
  IF_BLOCK_END,
  FOLDING_BLOCK_START,
  FOLDING_BLOCK_END,
  SYNTAX_BLOCK_START,
  SYNTAX_BLOCK_END,
  SYNTAX_BLOCK_NEST_OPEN,
  SYNTAX_BLOCK_NEST_CLOSE,
  WIKI_BLOCK_START,
  WIKI_BLOCK_END,
  HTML_BLOCK_START,
  HTML_BLOCK_END,
  HTML_BLOCK_NEST_OPEN,
  HTML_BLOCK_NEST_CLOSE,
  STYLE_BLOCK_START,
  STYLE_BLOCK_END,
  STYLE_BLOCK_NEST_OPEN,
  STYLE_BLOCK_NEST_CLOSE,
  LATEX_BLOCK_START,
  LATEX_BLOCK_END,
  LATEX_BLOCK_NEST_OPEN,
  LATEX_BLOCK_NEST_CLOSE,
};

enum BlockType {
  BLOCK_TYPE_NOWIKI,
  BLOCK_TYPE_NOWIKI_NEST,
  BLOCK_TYPE_IF,
  BLOCK_TYPE_FOLDING,
  BLOCK_TYPE_SYNTAX,
  BLOCK_TYPE_SYNTAX_NEST,
  BLOCK_TYPE_WIKI,
  BLOCK_TYPE_HTML,
  BLOCK_TYPE_HTML_NEST,
  BLOCK_TYPE_STYLE,
  BLOCK_TYPE_STYLE_NEST,
  BLOCK_TYPE_LATEX,
  BLOCK_TYPE_LATEX_NEST,
};

typedef struct {
  uint32_t depth;
  uint8_t stack[256];
} Scanner;

// [도우미 함수]: lexer의 현재 위치부터 str 문자열과 일치하는지 순차 검사하며 advance 진행
static bool consume_string(TSLexer *lexer, const char *str) {
  for (const char *c = str; *c != '\0'; c++) {
    if (lexer->lookahead != *c) {
      return false;
    }
    lexer->advance(lexer, false);
  }
  return true;
}

// 개행 문자(\n 또는 \r\n)를 소비하는 도우미 함수
static bool consume_newline(TSLexer *lexer) {
  if (lexer->lookahead == '\r') {
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead != '\n') {
    return false;
  }
  lexer->advance(lexer, false);
  return true;
}

static bool has_matching_closing_brace(TSLexer *lexer) {
  int current_depth = 1;

  while (!lexer->eof(lexer)) {
    // 1. '}}}' 탐색
    if (lexer->lookahead == '}') {
      if (consume_string(lexer, "}}}")) {
        current_depth--;
        if (current_depth == 0) {
          return true; // 현재 블록과 상응하는 짝 발견!
        }
        continue;
      }
      lexer->advance(lexer, false);
    } 
    // 2. '{{{' 탐색
    else if (lexer->lookahead == '{') {
      if (consume_string(lexer, "{{{")) {
        current_depth++; // 내부에서 다른 블록이 열림
        continue;
      }
      lexer->advance(lexer, false);
    } 
    else {
      lexer->advance(lexer, false);
    }
  }

  return false;
}

static bool match_token(TSLexer *lexer, Scanner *scanner, int TOKEN_TYPE, int BLOCK_TYPE) {
  lexer->mark_end(lexer); // 3글자 소비 지점 고정

  if (!has_matching_closing_brace(lexer)) {
    return false;
  }

  if (scanner->depth < 256) {
    scanner->stack[scanner->depth++] = BLOCK_TYPE;
  }
  lexer->result_symbol = TOKEN_TYPE;
  return true;
}

void *tree_sitter_namumark_external_scanner_create() {
  return (Scanner *)calloc(1, sizeof(Scanner));
}

void tree_sitter_namumark_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_namumark_external_scanner_serialize(void *payload, char *buffer) {
  Scanner *scanner = (Scanner *)payload;
  if (scanner->depth > 255) return 0;

  buffer[0] = (char)scanner->depth;
  for (uint32_t i = 0; i < scanner->depth; i++) {
    buffer[i + 1] = (char)scanner->stack[i];
  }
  return scanner->depth + 1;
}

void tree_sitter_namumark_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  scanner->depth = 0;
  if (length == 0 || buffer == NULL) return;

  scanner->depth = (uint8_t)buffer[0];
  for (uint32_t i = 0; i < scanner->depth; i++) {
    scanner->stack[i] = (uint8_t)buffer[i + 1];
  }
}

bool tree_sitter_namumark_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  uint8_t top_type = scanner->depth > 0 ? scanner->stack[scanner->depth - 1] : 255;

  // 1. END 토큰 매칭 (}}})
  if (scanner->depth > 0) {
    int end_symbol;

    switch (top_type) {
      case BLOCK_TYPE_NOWIKI: end_symbol = NOWIKI_END; break;
      case BLOCK_TYPE_NOWIKI_NEST: end_symbol = NOWIKI_NEST_CLOSE; break;
      case BLOCK_TYPE_IF: end_symbol = IF_BLOCK_END; break;
      case BLOCK_TYPE_FOLDING: end_symbol = FOLDING_BLOCK_END; break;
      case BLOCK_TYPE_SYNTAX: end_symbol = SYNTAX_BLOCK_END; break;
      case BLOCK_TYPE_SYNTAX_NEST: end_symbol = SYNTAX_BLOCK_NEST_CLOSE; break;
      case BLOCK_TYPE_WIKI: end_symbol = WIKI_BLOCK_END; break;
      case BLOCK_TYPE_HTML: end_symbol = HTML_BLOCK_END; break;
      case BLOCK_TYPE_HTML_NEST: end_symbol = HTML_BLOCK_NEST_CLOSE; break;
      case BLOCK_TYPE_STYLE: end_symbol = STYLE_BLOCK_END; break;
      case BLOCK_TYPE_STYLE_NEST: end_symbol = STYLE_BLOCK_NEST_CLOSE; break;
      case BLOCK_TYPE_LATEX: end_symbol = LATEX_BLOCK_END; break;
      case BLOCK_TYPE_LATEX_NEST: end_symbol = LATEX_BLOCK_NEST_CLOSE; break;
    }
    
    if (valid_symbols[end_symbol] && lexer->lookahead == '}' && consume_string(lexer, "}}}")) {
      lexer->mark_end(lexer);
      scanner->depth--;
      lexer->result_symbol = end_symbol;
      return true;
    }
  }

  // 2. START 토큰 매칭 ({{{#!wiki 및 {{{)
  if (lexer->lookahead == '{') {
    if (consume_string(lexer, "{{{")) {

      // 1. '#!' 접두사를 먼저 한 번만 체크
      if (lexer->lookahead == '#') {
        if (consume_string(lexer, "#!")) {
          if (consume_string(lexer, "if") && valid_symbols[IF_BLOCK_START]) {
            return match_token(lexer, scanner, IF_BLOCK_START, BLOCK_TYPE_IF);
          }
          if (consume_string(lexer, "folding") && valid_symbols[FOLDING_BLOCK_START]) {
            return match_token(lexer, scanner, FOLDING_BLOCK_START, BLOCK_TYPE_FOLDING);
          }
          if (consume_string(lexer, "s")) {
            if (consume_string(lexer, "yntax") && valid_symbols[SYNTAX_BLOCK_START]) {
              return match_token(lexer, scanner, SYNTAX_BLOCK_START, BLOCK_TYPE_SYNTAX);
            }
            if (consume_string(lexer, "tyle") && consume_newline(lexer) && valid_symbols[STYLE_BLOCK_START]) {
              return match_token(lexer, scanner, STYLE_BLOCK_START, BLOCK_TYPE_STYLE);
            }
          }
          if (consume_string(lexer, "wiki") && valid_symbols[WIKI_BLOCK_START]) {
            return match_token(lexer, scanner, WIKI_BLOCK_START, BLOCK_TYPE_WIKI);
          }
          if (consume_string(lexer, "html") && valid_symbols[HTML_BLOCK_START]) {
            return match_token(lexer, scanner, HTML_BLOCK_START, BLOCK_TYPE_HTML);
          }
          if (consume_string(lexer, "latex") && consume_newline(lexer) && valid_symbols[LATEX_BLOCK_START]) {
            return match_token(lexer, scanner, LATEX_BLOCK_START, BLOCK_TYPE_LATEX);
          }
        }
      }

      // 2. 중첩(NEST) 블록 검사
      if (top_type == BLOCK_TYPE_NOWIKI && valid_symbols[NOWIKI_NEST_OPEN]) {
        return match_token(lexer, scanner, NOWIKI_NEST_OPEN, BLOCK_TYPE_NOWIKI_NEST);
      }
      if (top_type == BLOCK_TYPE_SYNTAX && valid_symbols[SYNTAX_BLOCK_NEST_OPEN]) {
        return match_token(lexer, scanner, SYNTAX_BLOCK_NEST_OPEN, BLOCK_TYPE_SYNTAX_NEST);
      }
      if (top_type == BLOCK_TYPE_HTML && valid_symbols[HTML_BLOCK_NEST_OPEN]) {
        return match_token(lexer, scanner, HTML_BLOCK_NEST_OPEN, BLOCK_TYPE_HTML_NEST);
      }
      if (top_type == BLOCK_TYPE_STYLE && valid_symbols[STYLE_BLOCK_NEST_OPEN]) {
        return match_token(lexer, scanner, STYLE_BLOCK_NEST_OPEN, BLOCK_TYPE_STYLE_NEST);
      }
      if (top_type == BLOCK_TYPE_LATEX && valid_symbols[LATEX_BLOCK_NEST_OPEN]) {
        return match_token(lexer, scanner, LATEX_BLOCK_NEST_OPEN, BLOCK_TYPE_LATEX_NEST);
      }

      // 3. 아무것도 해당되지 않을 때만 NOWIKI로 떨어짐
      if (valid_symbols[NOWIKI_START]) {
        return match_token(lexer, scanner, NOWIKI_START, BLOCK_TYPE_NOWIKI);
      }
    }
  }

  return false;
}