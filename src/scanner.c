#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>

enum TokenType {
  WIKI_BLOCK_START,
  NOWIKI_START,
  WIKI_BLOCK_END,
  NOWIKI_END
};

enum BlockType {
  BLOCK_TYPE_WIKI,
  BLOCK_TYPE_NOWIKI
};

typedef struct {
  uint32_t depth;
  uint8_t stack[256];
} Scanner;

// [수정]: 미리 읽기 시 skip(true)을 쓰지 않고 false로 진행하여 앞의 토큰 영역을 유실시키지 않음
static bool has_matching_closing_brace(TSLexer *lexer) {
  int current_depth = 1;

  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == '}') {
      lexer->advance(lexer, false); // skip = false
      if (lexer->lookahead == '}') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '}') {
          current_depth--;
          if (current_depth == 0) {
            return true;
          }
          continue;
        }
      }
    } 
    else if (lexer->lookahead == '{') {
      lexer->advance(lexer, false); // skip = false
      if (lexer->lookahead == '{') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '{') {
          current_depth++;
          continue;
        }
      }
    } 
    else {
      lexer->advance(lexer, false); // skip = false
    }
  }

  return false;
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

  // 1. END 토큰 매칭 (}}})
  if (scanner->depth > 0) {
    uint8_t top_type = scanner->stack[scanner->depth - 1];

    if ((top_type == BLOCK_TYPE_WIKI && valid_symbols[WIKI_BLOCK_END]) ||
        (top_type == BLOCK_TYPE_NOWIKI && valid_symbols[NOWIKI_END])) {
      
      if (lexer->lookahead == '}') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == '}') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == '}') {
            lexer->advance(lexer, false);

            lexer->mark_end(lexer); // END 토큰 끝지점 고정
            scanner->depth--;
            lexer->result_symbol = (top_type == BLOCK_TYPE_WIKI) ? WIKI_BLOCK_END : NOWIKI_END;
            return true;
          }
        }
      }
    }
  }

  // 2. START 토큰 매칭 ({{{#!wiki 및 {{{)
  if (lexer->lookahead == '{') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '{') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '{') {
        lexer->advance(lexer, false);

        // {{{#!wiki 체크
        if (lexer->lookahead == '#') {
          lexer->advance(lexer, false);
          if (lexer->lookahead == '!') {
            lexer->advance(lexer, false);
            if (lexer->lookahead == 'w') {
              lexer->advance(lexer, false);
              if (lexer->lookahead == 'i') {
                lexer->advance(lexer, false);
                if (lexer->lookahead == 'k') {
                  lexer->advance(lexer, false);
                  if (lexer->lookahead == 'i') {
                    lexer->advance(lexer, false);

                    if (valid_symbols[WIKI_BLOCK_START]) {
                      // [핵심]: 9글자를 소비한 시점([0, 9])을 토큰의 마감 위치로 명시적으로 고정
                      lexer->mark_end(lexer);

                      // 그 이후 미리 읽기 실행
                      if (!has_matching_closing_brace(lexer)) {
                        return false;
                      }

                      if (scanner->depth < 256) {
                        scanner->stack[scanner->depth++] = BLOCK_TYPE_WIKI;
                      }
                      lexer->result_symbol = WIKI_BLOCK_START;
                      return true;
                    }
                  }
                }
              }
            }
          }
        }

        // {{{ (nowiki) 체크
        if (valid_symbols[NOWIKI_START]) {
          lexer->mark_end(lexer); // [0, 3] 지점 마감 고정

          if (!has_matching_closing_brace(lexer)) {
            return false;
          }

          if (scanner->depth < 256) {
            scanner->stack[scanner->depth++] = BLOCK_TYPE_NOWIKI;
          }
          lexer->result_symbol = NOWIKI_START;
          return true;
        }
      }
    }
  }

  return false;
}