#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

enum TokenType {
  NOWIKI_START,
  IF_BLOCK_START,
  FOLDING_BLOCK_START,
  SYNTAX_BLOCK_START,
  WIKI_BLOCK_START,
  HTML_BLOCK_START,
  STYLE_BLOCK_START,
  LATEX_BLOCK_START,
  SCALE_BLOCK_START,
  COLOR_BLOCK_START,
  BRACKET_END,
  HEADING1_START,
  HEADING2_START,
  HEADING3_START,
  HEADING4_START,
  HEADING5_START,
  HEADING6_START,
  HIDDEN_HEADING1_START,
  HIDDEN_HEADING2_START,
  HIDDEN_HEADING3_START,
  HIDDEN_HEADING4_START,
  HIDDEN_HEADING5_START,
  HIDDEN_HEADING6_START,
};

typedef struct {
  uint32_t depth;
  uint8_t stack[256];
} Scanner;

// --- Helper 함수들 ---

static bool consume_string(TSLexer *lexer, const char *str) {
  for (const char *c = str; *c != '\0'; c++) {
    if (lexer->lookahead != *c) return false;
    lexer->advance(lexer, false);
  }
  return true;
}

static bool consume_newline(TSLexer *lexer) {
  if (lexer->lookahead == '\r') {
    lexer->advance(lexer, false);
  }
  if (lexer->lookahead != '\n') return false;
  lexer->advance(lexer, false);
  return true;
}

// llm이 추천한 방식으로 변경
static bool consume_language(TSLexer *lexer) {
  char buf[32] = {0};
  size_t len = 0;

  while (isalnum(lexer->lookahead)) {
    if (len < 31) {
      buf[len++] = (char)lexer->lookahead;
    }
    lexer->advance(lexer, false);
  }
  buf[len] = '\0';

  if (len == 0) return false;

  static const char *const ALLOWED_LANGUAGES[] = {
    "javascript", "basic", "cpp", "csharp", "css", "erlang", "go", "html",
    "java", "json", "kotlin", "lisp", "lua", "markdown", "objectivec",
    "perl", "php", "powershell", "python", "ruby", "rust",
    "sh", "sql", "swift", "typescript", "xml"
  };
  static const size_t LANG_COUNT = sizeof(ALLOWED_LANGUAGES) / sizeof(ALLOWED_LANGUAGES[0]);

  bool is_valid = false;
  for (size_t i = 0; i < LANG_COUNT; i++) {
    if (strcmp(buf, ALLOWED_LANGUAGES[i]) == 0) {
      is_valid = true;
      break;
    }
  }

  return is_valid;
}

static bool register_token(TSLexer *lexer, Scanner *scanner, uint8_t TOKEN_TYPE) {
  if (scanner->depth < 256) {
    scanner->depth++;
  }

  lexer->result_symbol = TOKEN_TYPE;
  return true;
}

static bool consume_depth_block(TSLexer *lexer, Scanner *scanner, uint8_t TOKEN_TYPE) {
  uint8_t token = TOKEN_TYPE;
  if (lexer->lookahead != '\r' && lexer->lookahead != '\n') {
    if (lexer->lookahead != ' ') {
      token = NOWIKI_START;
    }

    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      lexer->advance(lexer, false);
    }
  }

  if (!consume_newline(lexer)) {
    token = NOWIKI_START;
  }

  return register_token(lexer, scanner, token);
}

static uint8_t consume_syntax_block(TSLexer *lexer, Scanner *scanner) {
  if (lexer->lookahead != ' ') {
    return NOWIKI_START;
  }

  lexer->advance(lexer, false);

  if (!consume_language(lexer)) {
    return NOWIKI_START;
  }
  
  if (!consume_newline(lexer)) {
    return NOWIKI_START;
  }

  return SYNTAX_BLOCK_START;
}

static uint8_t consume_scale_block(TSLexer *lexer, Scanner *scanner) {
  if (lexer->lookahead != '+' && lexer->lookahead != '-') {
    return NOWIKI_START;
  }

  lexer->advance(lexer, false);

  if (!(lexer->lookahead >= '1' && lexer->lookahead <= '5')) {
    return NOWIKI_START;
  }

  lexer->advance(lexer, false);
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return NOWIKI_START;
    }
    lexer->advance(lexer, false);
  }

  return SCALE_BLOCK_START;
}

static bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

// `#` 다음 오는 단일 색상 본문 검사
static bool consume_single_color(TSLexer *lexer) {
  char buf[32];
  int len = 0;
  bool all_hex = true;

  while (isalnum((unsigned char)lexer->lookahead) && len < 31) {
    char c = (char)lexer->lookahead;
    if (!is_hex_digit(c)) {
      all_hex = false;
    }
    buf[len++] = (char)tolower((unsigned char)c);
    lexer->advance(lexer, false);
  }
  buf[len] = '\0';

  if (len == 0) return false;

  // 1. 모든 문자가 Hex이고 길이가 3 또는 6인 경우 Valid Hex
  if (all_hex && (len == 3 || len == 6)) {
    return true;
  }

  // 2. Named Color 목록 매칭
  static const char *const VALID_COLORS[] = {"aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgrey", "darkgreen", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgrey", "lightgreen", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen"};

  int count = sizeof(VALID_COLORS) / sizeof(VALID_COLORS[0]);
  for (int i = 0; i < count; i++) {
    if (strcmp(buf, VALID_COLORS[i]) == 0) return true;
  }

  return false;
}

static uint8_t consume_color_block(TSLexer *lexer, Scanner *scanner) {
  if (!consume_single_color(lexer)) return NOWIKI_START;

  if (lexer->lookahead == ',') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '#') {
      return NOWIKI_START;
    }
    lexer->advance(lexer, false);
    if (!consume_single_color(lexer)) return NOWIKI_START;
  }
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return NOWIKI_START;
    }
    lexer->advance(lexer, false);
  }

  return COLOR_BLOCK_START;
}


// --- Lifecycle 함수들 ---

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

// --- Main Scan ---

bool tree_sitter_namumark_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  uint8_t top_type = scanner->depth > 0 ? scanner->stack[scanner->depth - 1] : 255;

  if (lexer->get_column(lexer) == 0 && lexer->lookahead == '=') {
    uint8_t open_count = 0;
    while (lexer->lookahead == '=' && open_count < 6) {
      lexer->advance(lexer, false);
      open_count++;
    }

    bool is_hidden = 0;
    if (lexer->lookahead == '#') {
      is_hidden = true;
      lexer->advance(lexer, false);
    }

    uint8_t base_symbol = is_hidden ? HIDDEN_HEADING1_START : HEADING1_START;
    uint8_t target_symbol = base_symbol + (open_count - 1);

    if (open_count >= 1 && open_count <= 6 && valid_symbols[target_symbol]) {
      if (lexer->lookahead != ' ') {
        return false;
      }

      lexer->mark_end(lexer);

      bool has_trailing_space = false;
      bool has_closing_hash = false;
      uint8_t trailing_equals = 0;
      int32_t last_char = ' ';

      while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
        if (lexer->lookahead == '=') {
          if (trailing_equals == 0) {
            if (is_hidden) {
              if (last_char == '#') {
                has_closing_hash = true;
              }
            } else {
              if (last_char == ' ') {
                has_trailing_space = true;
              }
            }
          }
          trailing_equals++;
        } else {
          if (is_hidden && lexer->lookahead == '#' && last_char == ' ' && trailing_equals == 0) {
            has_trailing_space = true;
          } else {
            has_trailing_space = false;
            has_closing_hash = false;
          }
          trailing_equals = 0;
          last_char = lexer->lookahead;
        }
        lexer->advance(lexer, false);
      }

      if (trailing_equals == open_count && has_trailing_space) {
        if (!is_hidden || has_closing_hash) {
          lexer->result_symbol = target_symbol;
          return true;
        }
      }
    }
  }

  // 1. END 토큰 매칭 (}}})
  if (scanner->depth > 0) {
    if (valid_symbols[BRACKET_END]) {
      if (lexer->lookahead == '}' && consume_string(lexer, "}}}")) {
        lexer->mark_end(lexer);
        scanner->depth--;
        lexer->result_symbol = BRACKET_END;
        return true;
      }
    }
  }

  // // 2. START 토큰 매칭
  if (lexer->lookahead == '{') {
    if (consume_string(lexer, "{{{")) {
      lexer->mark_end(lexer);

      // 첫 번째 '#' 문자를 만나면 단 한 번만 소비 후 다음 문자로 분기
      if (lexer->lookahead == '#') {
        lexer->advance(lexer, false); // '#' 소비

        // 2-1. '#!' 블록들 (wiki, syntax, html 등)
        if (lexer->lookahead == '!') {
          lexer->advance(lexer, false); // '!' 소비

          if (valid_symbols[IF_BLOCK_START] && consume_string(lexer, "if")) {
            return consume_depth_block(lexer, scanner, IF_BLOCK_START);
          }
          if (valid_symbols[FOLDING_BLOCK_START] && consume_string(lexer, "folding")) {
            return consume_depth_block(lexer, scanner, FOLDING_BLOCK_START);
          }
          if (consume_string(lexer, "s")) {
            if (valid_symbols[SYNTAX_BLOCK_START] && consume_string(lexer, "yntax")) {
              return register_token(lexer, scanner, consume_syntax_block(lexer, scanner));
            }
            if (valid_symbols[STYLE_BLOCK_START] && consume_string(lexer, "tyle")) {
              uint8_t token = STYLE_BLOCK_START;
              if (!consume_newline(lexer)) {
                token = NOWIKI_START;
              }
              return register_token(lexer, scanner, token);
            }
          }
          if (valid_symbols[WIKI_BLOCK_START] && consume_string(lexer, "wiki")) {
            return consume_depth_block(lexer, scanner, WIKI_BLOCK_START);
          }
          if (valid_symbols[HTML_BLOCK_START] && consume_string(lexer, "html")) {
            return register_token(lexer, scanner, HTML_BLOCK_START);
          }
          if (valid_symbols[LATEX_BLOCK_START] && consume_string(lexer, "latex")) {
            uint8_t token = LATEX_BLOCK_START;
            if (!consume_newline(lexer)) {
              token = NOWIKI_START;
            }
            return register_token(lexer, scanner, token);
          }
        } 
        // 2-2. '#!'가 아니고 바로 색상명이 시작하는 케이스 (COLOR_BLOCK_START)
        else if (valid_symbols[COLOR_BLOCK_START]) {
          return register_token(lexer, scanner, consume_color_block(lexer, scanner));
        }
      }

      // 글자 크기 블록 ({{{+1 ...}}}, {{{-2 ...}}})
      if (valid_symbols[SCALE_BLOCK_START]) {
        return register_token(lexer, scanner, consume_scale_block(lexer, scanner));
      }

      // 4. Default Fallback -> NOWIKI
      if (valid_symbols[NOWIKI_START]) {
        return register_token(lexer, scanner, NOWIKI_START);
      }
    }
  }

  return false;
}