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

// [핵심 해결]: 중첩 레벨(depth)을 계산하며 자신에게 맞는 '}}}'가 있는지 검사
static bool has_matching_closing_brace(TSLexer *lexer) {
  lexer->mark_end(lexer);

  int current_depth = 1; // 현재 블록 시작으로 인해 depth = 1

  while (!lexer->eof(lexer)) {
    // 1. '}}}' 탐색 (닫는 기호)
    if (lexer->lookahead == '}') {
      lexer->advance(lexer, true);
      if (lexer->lookahead == '}') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '}') {
          current_depth--;
          if (current_depth == 0) {
            return true; // 현재 블록과 상응하는 짝 발견!
          }
          continue;
        }
      }
    } 
    // 2. '{{{' 탐색 (중첩으로 열리는 블록)
    else if (lexer->lookahead == '{') {
      lexer->advance(lexer, true);
      if (lexer->lookahead == '{') {
        lexer->advance(lexer, true);
        if (lexer->lookahead == '{') {
          current_depth++; // 내부에서 다른 블록이 열림
          continue;
        }
      }
    } 
    else {
      lexer->advance(lexer, true);
    }
  }

  return false; // EOF까지 자신과 매칭되는 짝을 찾지 못함
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
                      // 중첩 레벨을 고려하여 자기 짝이 맞는지 미리 검사
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