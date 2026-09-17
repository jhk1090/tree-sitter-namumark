#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

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
  SCALE_BLOCK_START,
  SCALE_BLOCK_END,
  COLOR_BLOCK_START,
  COLOR_BLOCK_END,
  BRACKET_END
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
  BLOCK_TYPE_SCALE,
  BLOCK_TYPE_COLOR
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
  static const char *const VALID_COLORS[] = {
    "aliceblue", "antiquewhite", "aqua", "aquamarine", "azure",
    "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood",
    "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan",
    "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgreen", "darkgrey", "darkkhaki",
    "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon",
    "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet",
    "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue",
    "firebrick", "floralwhite", "forestgreen", "fuchsia",
    "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "green", "greenyellow", "grey",
    "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki",
    "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral",
    "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgreen", "lightgrey", "lightpink",
    "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey",
    "lightsteelblue", "lightyellow", "lime", "limegreen", "linen",
    "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple",
    "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise",
    "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin",
    "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid",
    "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff",
    "peru", "pink", "plum", "powderblue", "purple",
    "red", "rosybrown", "royalblue",
    "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver",
    "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue",
    "tan", "teal", "thistle", "tomato", "transparent", "turquoise",
    "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen"
  };

  int count = sizeof(VALID_COLORS) / sizeof(VALID_COLORS[0]);
  for (int i = 0; i < count; i++) {
    if (strcmp(buf, VALID_COLORS[i]) == 0) return true;
  }

  return false;
}

// 색상 전체 표현식 검사
static bool consume_color_token(TSLexer *lexer) {
  // 1. 첫 번째 색상 검사
  if (!consume_single_color(lexer)) return false;

  // 2. 다크모드 대응
  if (lexer->lookahead == ',') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '#') return false;
    lexer->advance(lexer, false);
    if (!consume_single_color(lexer)) return false;
  }

  return true;
}

static bool consume_language(TSLexer *lexer) {
  switch (lexer->lookahead) {
    case 'c':
      lexer->advance(lexer, false);
      if (lexer->lookahead == 'p') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 'p') {
          lexer->advance(lexer, false);
          return true;
        }
      } else if (lexer->lookahead == 's') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 's') {
          lexer->advance(lexer, false);
          return true;
        }
        if (consume_string(lexer, "harp")) return true;
      }
      return false;

    case 'j':
      lexer->advance(lexer, false);
      if (consume_string(lexer, "ava")) {
        consume_string(lexer, "script");
        return true;
      }
      if (consume_string(lexer, "son")) return true;
      return false;

    case 'l':
      lexer->advance(lexer, false);
      if (consume_string(lexer, "isp")) return true;
      if (consume_string(lexer, "ua")) return true; 
      return false;

    case 'p':
      lexer->advance(lexer, false);
      if (consume_string(lexer, "erl")) return true;
      if (consume_string(lexer, "hp")) return true;
      if (consume_string(lexer, "owershell")) return true;
      if (consume_string(lexer, "ython")) return true;
      return false;

    case 'r':
      lexer->advance(lexer, false);
      if (consume_string(lexer, "uby")) return true;
      if (consume_string(lexer, "ust")) return true;
      return false;

    case 's':
      lexer->advance(lexer, false);
      if (lexer->lookahead == 'h') {
        lexer->advance(lexer, false);
        return true;
      }
      if (lexer->lookahead == 'q') {
        lexer->advance(lexer, false);
        if (lexer->lookahead == 'l') {
          lexer->advance(lexer, false);
          return true;
        }
      }
      if (consume_string(lexer, "wift")) return true;
      return false;

    case 'b': return consume_string(lexer, "basic");
    case 'e': return consume_string(lexer, "erlang");
    case 'g': return consume_string(lexer, "go");
    case 'h': return consume_string(lexer, "html");
    case 'k': return consume_string(lexer, "kotlin");
    case 'm': return consume_string(lexer, "markdown");
    case 'o': return consume_string(lexer, "objectivec");
    case 't': return consume_string(lexer, "typescript");
    case 'x': return consume_string(lexer, "xml");

    default:
      return false;
  }
}

static bool has_matching_closing_brace(TSLexer *lexer) {
  int current_depth = 1;

  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == '}') {
      if (consume_string(lexer, "}}}")) {
        current_depth--;
        if (current_depth == 0) return true;
        continue;
      }
      lexer->advance(lexer, false);
    } 
    else if (lexer->lookahead == '{') {
      if (consume_string(lexer, "{{{")) {
        current_depth++;
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
  lexer->mark_end(lexer);

  if (!has_matching_closing_brace(lexer)) {
    return false;
  }

  if (scanner->depth < 256) {
    scanner->stack[scanner->depth++] = BLOCK_TYPE;
  }
  lexer->result_symbol = TOKEN_TYPE;
  return true;
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
    // int end_symbol;

    // switch (top_type) {
    //   case BLOCK_TYPE_NOWIKI: end_symbol = NOWIKI_END; break;
    //   case BLOCK_TYPE_NOWIKI_NEST: end_symbol = NOWIKI_NEST_CLOSE; break;
    //   case BLOCK_TYPE_IF: end_symbol = IF_BLOCK_END; break;
    //   case BLOCK_TYPE_FOLDING: end_symbol = FOLDING_BLOCK_END; break;
    //   case BLOCK_TYPE_SYNTAX: end_symbol = SYNTAX_BLOCK_END; break;
    //   case BLOCK_TYPE_SYNTAX_NEST: end_symbol = SYNTAX_BLOCK_NEST_CLOSE; break;
    //   case BLOCK_TYPE_WIKI: end_symbol = WIKI_BLOCK_END; break;
    //   case BLOCK_TYPE_HTML: end_symbol = HTML_BLOCK_END; break;
    //   case BLOCK_TYPE_HTML_NEST: end_symbol = HTML_BLOCK_NEST_CLOSE; break;
    //   case BLOCK_TYPE_STYLE: end_symbol = STYLE_BLOCK_END; break;
    //   case BLOCK_TYPE_STYLE_NEST: end_symbol = STYLE_BLOCK_NEST_CLOSE; break;
    //   case BLOCK_TYPE_LATEX: end_symbol = LATEX_BLOCK_END; break;
    //   case BLOCK_TYPE_LATEX_NEST: end_symbol = LATEX_BLOCK_NEST_CLOSE; break;
    //   case BLOCK_TYPE_SCALE: end_symbol = SCALE_BLOCK_END; break;
    //   case BLOCK_TYPE_COLOR: end_symbol = COLOR_BLOCK_END; break;
    // }
    
    // if (valid_symbols[end_symbol] && lexer->lookahead == '}' && consume_string(lexer, "}}}")) {
    //   lexer->mark_end(lexer);
    //   scanner->depth--;
    //   lexer->result_symbol = end_symbol;
    //   return true;
    // }
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

          // if (consume_string(lexer, "if") && valid_symbols[IF_BLOCK_START]) {
          //   return match_token(lexer, scanner, IF_BLOCK_START, BLOCK_TYPE_IF);
          // }
          // if (consume_string(lexer, "folding") && valid_symbols[FOLDING_BLOCK_START]) {
          //   return match_token(lexer, scanner, FOLDING_BLOCK_START, BLOCK_TYPE_FOLDING);
          // }
          // if (consume_string(lexer, "s")) {
          //   if (consume_string(lexer, "yntax")) {
          //     if (lexer->lookahead == ' ') {
          //       lexer->advance(lexer, false);
          //       if (consume_language(lexer) && valid_symbols[SYNTAX_BLOCK_START]) {
          //         return match_token(lexer, scanner, SYNTAX_BLOCK_START, BLOCK_TYPE_SYNTAX);
          //       }
          //     }
          //   }
          //   if (consume_string(lexer, "tyle") && consume_newline(lexer) && valid_symbols[STYLE_BLOCK_START]) {
          //     return match_token(lexer, scanner, STYLE_BLOCK_START, BLOCK_TYPE_STYLE);
          //   }
          // }
          if (valid_symbols[WIKI_BLOCK_START] && consume_string(lexer, "wiki")) {
            uint8_t token = WIKI_BLOCK_START;
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

            if (scanner->depth < 256) {
              scanner->depth++;
            }

            lexer->result_symbol = token;
            return true;
          }
          // if (consume_string(lexer, "html") && valid_symbols[HTML_BLOCK_START]) {
          //   return match_token(lexer, scanner, HTML_BLOCK_START, BLOCK_TYPE_HTML);
          // }
          // if (consume_string(lexer, "latex") && consume_newline(lexer) && valid_symbols[LATEX_BLOCK_START]) {
          //   return match_token(lexer, scanner, LATEX_BLOCK_START, BLOCK_TYPE_LATEX);
          // }
        } 
        // 2-2. '#!'가 아니고 바로 색상명이 시작하는 케이스 (COLOR_BLOCK_START)
        // else if (valid_symbols[COLOR_BLOCK_START]) {
        //   if (consume_color_token(lexer)) {
        //     return match_token(lexer, scanner, COLOR_BLOCK_START, BLOCK_TYPE_COLOR);
        //   }
        // }
      }

      // 글자 크기 블록 ({{{+1 ...}}}, {{{-2 ...}}})
      // if (lexer->lookahead == '+' || lexer->lookahead == '-') {
      //   if (valid_symbols[SCALE_BLOCK_START]) {
      //     lexer->advance(lexer, false);
      //     if (lexer->lookahead >= '1' && lexer->lookahead <= '5') {
      //       lexer->advance(lexer, false);
      //       return match_token(lexer, scanner, SCALE_BLOCK_START, BLOCK_TYPE_SCALE);
      //     }
      //   }
      // }

      // 3. NEST 중첩 처리
      // if (top_type == BLOCK_TYPE_NOWIKI && valid_symbols[NOWIKI_NEST_OPEN]) {
      //   return match_token(lexer, scanner, NOWIKI_NEST_OPEN, BLOCK_TYPE_NOWIKI_NEST);
      // }
      // if (top_type == BLOCK_TYPE_SYNTAX && valid_symbols[SYNTAX_BLOCK_NEST_OPEN]) {
      //   return match_token(lexer, scanner, SYNTAX_BLOCK_NEST_OPEN, BLOCK_TYPE_SYNTAX_NEST);
      // }
      // if (top_type == BLOCK_TYPE_HTML && valid_symbols[HTML_BLOCK_NEST_OPEN]) {
      //   return match_token(lexer, scanner, HTML_BLOCK_NEST_OPEN, BLOCK_TYPE_HTML_NEST);
      // }
      // if (top_type == BLOCK_TYPE_STYLE && valid_symbols[STYLE_BLOCK_NEST_OPEN]) {
      //   return match_token(lexer, scanner, STYLE_BLOCK_NEST_OPEN, BLOCK_TYPE_STYLE_NEST);
      // }
      // if (top_type == BLOCK_TYPE_LATEX && valid_symbols[LATEX_BLOCK_NEST_OPEN]) {
      //   return match_token(lexer, scanner, LATEX_BLOCK_NEST_OPEN, BLOCK_TYPE_LATEX_NEST);
      // }

      // 4. Default Fallback -> NOWIKI
      if (valid_symbols[NOWIKI_START]) {
        if (scanner->depth < 256) {
          scanner->depth++;
        }
        lexer->result_symbol = NOWIKI_START;
        return true;
      }
    }
  }

  return false;
}