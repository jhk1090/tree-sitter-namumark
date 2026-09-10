#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

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

  // 1. WIKI_BLOCK_END 또는 NOWIKI_END 검사
  if (scanner->depth > 0) {
    uint8_t top_type = scanner->stack[scanner->depth - 1];

    if ((top_type == BLOCK_TYPE_WIKI && valid_symbols[WIKI_BLOCK_END]) ||
        (top_type == BLOCK_TYPE_NOWIKI && valid_symbols[NOWIKI_END])) {
      
      // 스캐너 진입 시점의 공백/줄바꿈은 건너뛰지 않고, 정확히 '}' 문자인지 확인
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

  // 2. WIKI_BLOCK_START 또는 NOWIKI_START 검사
  if (lexer->lookahead == '{') {
    lexer->advance(lexer, false);
    if (lexer->lookahead == '{') {
      lexer->advance(lexer, false);
      if (lexer->lookahead == '{') {
        lexer->advance(lexer, false);

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

        if (valid_symbols[NOWIKI_START]) {
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