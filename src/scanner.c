#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wctype.h>
#include <string.h>
#include <stdio.h>
#include "unicode_ranges.h"

#define NO_HEADING_TOKEN 0

enum TokenType {
  COMMENT,
  ARGUMENT_START,
  ARGUMENT_END,
  ARGUMENT_IMPLICIT_END,
  NOWIKI_START,
  IF_BLOCK_START,
  IF_FALLBACK_BLOCK_START,
  FOLDING_BLOCK_START,
  FOLDING_FALLBACK_BLOCK_START,
  SYNTAX_BLOCK_START,
  SYNTAX_FALLBACK_BLOCK_START,
  WIKI_BLOCK_START,
  WIKI_FALLBACK_BLOCK_START,
  HTML_BLOCK_START,
  STYLE_BLOCK_START,
  STYLE_FALLBACK_BLOCK_START,
  LATEX_BLOCK_START,
  LATEX_FALLBACK_BLOCK_START,
  SCALE_BLOCK_START,
  SCALE_FALLBACK_BLOCK_START,
  COLOR_BLOCK_START,
  COLOR_FALLBACK_BLOCK_START,
  BRACKET_END,
  BRACKET_IMPLICIT_END,
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
  HEADING_END,
  MACRO_START,
  MACRO_END,
  MACRO_IMPLICIT_END,
  LINK_START,
  LINK_PIPE,
  LINK_END,
  LINK_IMPLICIT_END
};

enum BlockType {
  BLOCK_TYPE_NOWIKI,
  BLOCK_TYPE_IF,
  BLOCK_TYPE_FOLDING,
  BLOCK_TYPE_SYNTAX,
  BLOCK_TYPE_WIKI,
  BLOCK_TYPE_HTML,
  BLOCK_TYPE_STYLE,
  BLOCK_TYPE_LATEX,
  BLOCK_TYPE_SCALE,
  BLOCK_TYPE_COLOR
};

typedef struct {
  bool in_macro;
  bool in_argument;
  uint8_t heading_level;
  bool is_hidden_heading;
  uint32_t link_depth;
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

  while (iswalnum(lexer->lookahead)) {
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

static bool register_token(TSLexer *lexer, Scanner *scanner, uint8_t TOKEN_TYPE, uint8_t BLOCK_TYPE) {
  if (scanner->depth < 256) {
    scanner->stack[scanner->depth++] = BLOCK_TYPE;
  }

  lexer->result_symbol = TOKEN_TYPE;
  return true;
}

static bool consume_depth_block(TSLexer *lexer, Scanner *scanner, uint8_t TOKEN_TYPE, uint8_t BLOCK_TYPE, bool is_header_needed) {
  if (lexer->lookahead != '\r' && lexer->lookahead != '\n') {
    if (lexer->lookahead != ' ') {
      return register_token(lexer, scanner, TOKEN_TYPE + 1, BLOCK_TYPE_NOWIKI);
    }

    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      if (lexer->lookahead == '}' && consume_string(lexer, "}}}")) {
        return register_token(lexer, scanner, TOKEN_TYPE + 1, BLOCK_TYPE_NOWIKI);
      }
      lexer->advance(lexer, false);
    }
  } else {
    if (is_header_needed) {
      return register_token(lexer, scanner, TOKEN_TYPE + 1, BLOCK_TYPE_NOWIKI);
    }
  }

  if (!consume_newline(lexer)) {
    return register_token(lexer, scanner, TOKEN_TYPE + 1, BLOCK_TYPE_NOWIKI);
  }

  return register_token(lexer, scanner, TOKEN_TYPE, BLOCK_TYPE);
}

static bool consume_syntax_block(TSLexer *lexer, Scanner *scanner) {
  if (lexer->lookahead != ' ') {
    return register_token(lexer, scanner, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }

  lexer->advance(lexer, false);

  if (!consume_language(lexer)) {
    return register_token(lexer, scanner, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }
  
  if (!consume_newline(lexer)) {
    return register_token(lexer, scanner, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }

  return register_token(lexer, scanner, SYNTAX_BLOCK_START, BLOCK_TYPE_SYNTAX);
}

static bool consume_scale_block(TSLexer *lexer, Scanner *scanner) {
  if (!iswdigit(lexer->lookahead)) {
    return register_token(lexer, scanner, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }

  if (!(lexer->lookahead >= '1' && lexer->lookahead <= '5')) {
    return register_token(lexer, scanner, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }

  lexer->advance(lexer, false);
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return register_token(lexer, scanner, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
    }
    lexer->advance(lexer, false);
  }

  return register_token(lexer, scanner, SCALE_BLOCK_START, BLOCK_TYPE_SCALE);
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

  while (iswalnum((unsigned char)lexer->lookahead) && len < 31) {
    char c = (char)lexer->lookahead;
    if (!is_hex_digit(c)) {
      all_hex = false;
    }
    buf[len++] = (char)towlower((unsigned char)c);
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

static bool consume_color_block(TSLexer *lexer, Scanner *scanner) {
  if (!consume_single_color(lexer)) return register_token(lexer, scanner, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);

  if (lexer->lookahead == ',') {
    lexer->advance(lexer, false);
    if (lexer->lookahead != '#') {
      return register_token(lexer, scanner, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
    }
    lexer->advance(lexer, false);
    if (!consume_single_color(lexer)) return register_token(lexer, scanner, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
  }
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return register_token(lexer, scanner, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
    }
    lexer->advance(lexer, false);
  }

  return register_token(lexer, scanner, COLOR_BLOCK_START, BLOCK_TYPE_COLOR);
}

static uint8_t match_heading_token(TSLexer *lexer, bool preserve_start) {
  if (lexer->get_column(lexer) != 0) return NO_HEADING_TOKEN;
  if (lexer->lookahead != '=') return NO_HEADING_TOKEN;

  if (preserve_start) {
    lexer->mark_end(lexer);
  }

  uint8_t open_count = 0;
  while (lexer->lookahead == '=' && open_count < 6) {
    lexer->advance(lexer, false);
    open_count++;
  }

  bool is_hidden = false;
  if (lexer->lookahead == '#') {
    is_hidden = true;
    lexer->advance(lexer, false);
  }

  uint8_t base_symbol = is_hidden ? HIDDEN_HEADING1_START : HEADING1_START;
  uint8_t target_symbol = base_symbol + (open_count - 1);

  if (open_count == 0 || open_count > 6 || lexer->lookahead != ' ') { return NO_HEADING_TOKEN; }

  if (!preserve_start) {
    lexer->mark_end(lexer);
  }

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
      return target_symbol;
    }
  }

  return NO_HEADING_TOKEN;
}

// 2. int32_t 유니코드를 UTF-8 바이트 시퀀스로 인코딩하는 헬퍼 함수
static size_t encode_utf8(int32_t ch, char *buf) {
  if (ch < 0x80) {
    buf[0] = (char)ch;
    return 1;
  } else if (ch < 0x800) {
    buf[0] = (char)(0xC0 | (ch >> 6));
    buf[1] = (char)(0x80 | (ch & 0x3F));
    return 2;
  } else if (ch < 0x10000) {
    buf[0] = (char)(0xE0 | (ch >> 12));
    buf[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
    buf[2] = (char)(0x80 | (ch & 0x3F));
    return 3;
  } else if (ch < 0x110000) {
    buf[0] = (char)(0xF0 | (ch >> 18));
    buf[1] = (char)(0x80 | ((ch >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((ch >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (ch & 0x3F));
    return 4;
  }
  return 0;
}

static bool consume_macro(TSLexer *lexer) {
  // UTF-8 한글 1글자는 3바이트를 차지하므로, 여유 있게 버퍼 크기를 잡습니다.
  char buf[128];
  size_t len = 0;

  while (is_unicode_letter_or_number((uint32_t)lexer->lookahead)) {
    int32_t ch = lexer->lookahead;

    // 영문 대문자만 소문자로 변환 (한글은 변환하지 않음)
    if (ch >= 'A' && ch <= 'Z') {
      ch = ch + ('a' - 'A');
    }

    // UTF-8 인코딩 시 버퍼 범위를 넘어서는지 확인 (여유 바이트 4바이트)
    if (len + 4 >= sizeof(buf) - 1) break;

    size_t encoded_bytes = encode_utf8(ch, &buf[len]);
    if (encoded_bytes == 0) break;

    len += encoded_bytes;
    lexer->advance(lexer, false);
  }
  buf[len] = '\0';

  if (len == 0) return false;

  // 매크로 이름 목록 매칭
  static const char *const VALID_NAMES[] = {
    "anchor", "age", "dday", "youtube", "kakaotv", "vimeo", 
    "nicovideo", "navertv", "math", "include", "ruby", "pagecount", 
    "목차", "tableofcontents", "clearfix", "date", "datetime", 
    "각주", "footnote", "br"
  };

  size_t count = sizeof(VALID_NAMES) / sizeof(VALID_NAMES[0]);
  for (size_t i = 0; i < count; i++) {
    if (strcmp(buf, VALID_NAMES[i]) == 0) return true;
  }

  return false;
}

static bool consume_argument(TSLexer *lexer) {
  if (lexer->lookahead != '@') return false;
  lexer->advance(lexer, false);
  lexer->mark_end(lexer);

  bool is_zero_length = true;
  bool has_default_value = false;
  while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
    if (!has_default_value && lexer->lookahead == '=') {
      if (is_zero_length) return false;
      is_zero_length = true;
      has_default_value = true;
      lexer->advance(lexer, false);
      continue;
    }

    if (lexer->lookahead == '@') {
      if (is_zero_length) return false;
      return true;
    }

    if (!has_default_value && !is_unicode_letter_or_number((uint32_t)lexer->lookahead)) return false;
    
    is_zero_length = false;

    lexer->advance(lexer, false);
  }

  return false;
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

  buffer[0] = (char)scanner->heading_level;
  buffer[1] = (char)scanner->is_hidden_heading;
  buffer[2] = (char)scanner->in_macro;
  buffer[3] = (char)scanner->in_argument;
  buffer[4] = (char)scanner->link_depth;
  buffer[5] = (char)scanner->depth;
  for (uint32_t i = 0; i < scanner->depth; i++) {
    buffer[6 + i] = (char)scanner->stack[i];
  }
  return scanner->depth + 6;
}

void tree_sitter_namumark_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  scanner->heading_level = 0;
  scanner->is_hidden_heading = false;
  scanner->in_macro = false;
  scanner->in_argument = false;
  scanner->link_depth = 0;
  scanner->depth = 0;

  if (length < 6 || buffer == NULL) return;

  scanner->heading_level = (uint8_t)buffer[0];
  scanner->is_hidden_heading = (bool)buffer[1];
  scanner->in_macro = (bool)buffer[2];
  scanner->in_argument = (bool)buffer[3];
  scanner->link_depth = (uint32_t)buffer[4];
  uint32_t saved_depth = (uint32_t)buffer[5];

  if (length < 6 + saved_depth) return;

  scanner->depth = saved_depth;
  for (uint32_t i = 0; i < scanner->depth; i++) {
    scanner->stack[i] = (uint8_t)buffer[6 + i];
  }
}

// --- Main Scan ---

bool tree_sitter_namumark_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  uint8_t top_type = scanner->depth > 0 ? scanner->stack[scanner->depth - 1] : 255;

  if (lexer->eof(lexer)) {
    if (scanner->depth > 0 && valid_symbols[BRACKET_IMPLICIT_END]) {
      scanner->depth--;
      lexer->result_symbol = BRACKET_IMPLICIT_END;
      return true;
    }
    if (scanner->in_macro && valid_symbols[MACRO_IMPLICIT_END]) {
      scanner->in_macro = false;
      lexer->result_symbol = MACRO_IMPLICIT_END;
      return true;
    }
    if (scanner->link_depth > 0 && valid_symbols[LINK_IMPLICIT_END]) {
      scanner->link_depth--;
      lexer->result_symbol = LINK_IMPLICIT_END;
      return true;
    }
  }

  if (lexer->get_column(lexer) == 0 && lexer->lookahead == '#' && consume_string(lexer, "##") && valid_symbols[COMMENT]) {
    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      lexer->advance(lexer, false);
    }

    lexer->mark_end(lexer);
    lexer->result_symbol = COMMENT;
    return true;
  }

  // 1. END 토큰 매칭 (}}})
  if (lexer->lookahead == '}' && scanner->depth > 0) {
    lexer->mark_end(lexer);
    if (consume_string(lexer, "}}}")) {
      if (scanner->in_macro && valid_symbols[MACRO_IMPLICIT_END]) {
        scanner->in_macro = false;
        lexer->result_symbol = MACRO_IMPLICIT_END;
        return true;
      }
      if (scanner->link_depth > 0 && valid_symbols[LINK_IMPLICIT_END]) {
        scanner->link_depth--;
        lexer->result_symbol = LINK_IMPLICIT_END;
        return true;
      }
      if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
        scanner->in_argument = false;
        lexer->result_symbol = ARGUMENT_IMPLICIT_END;
        return true;
      }

      lexer->mark_end(lexer);
      scanner->depth--;
      lexer->result_symbol = BRACKET_END;
      return true;
    }
  }

  if (lexer->lookahead == ')' && scanner->in_macro) {
    lexer->mark_end(lexer);
    if (consume_string(lexer, ")]")) {
      if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
        scanner->in_argument = false;
        lexer->result_symbol = ARGUMENT_IMPLICIT_END;
        return true;
      }

      if (valid_symbols[MACRO_END]) {
        lexer->mark_end(lexer);
        scanner->in_macro = false;
        lexer->result_symbol = MACRO_END;
        return true;
      }
    }
  }

  if (lexer->lookahead == '|' && scanner->link_depth > 0) {
    lexer->mark_end(lexer);
    if (consume_string(lexer, "|")) {
      if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
        scanner->in_argument = false;
        lexer->result_symbol = ARGUMENT_IMPLICIT_END;
        return true;
      }

      if (valid_symbols[LINK_PIPE]) {
        lexer->mark_end(lexer);
        lexer->result_symbol = LINK_PIPE;
        return true;
      }
    }
  }

  if (lexer->lookahead == ']' && scanner->link_depth > 0) {
    lexer->mark_end(lexer);
    if (consume_string(lexer, "]]")) {
      if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
        scanner->in_argument = false;
        lexer->result_symbol = ARGUMENT_IMPLICIT_END;
        return true;
      }

      if (valid_symbols[LINK_END]) {
        lexer->mark_end(lexer);
        scanner->link_depth--;
        lexer->result_symbol = LINK_END;
        return true;
      }
    }
  }

  if (scanner->link_depth > 0 && (lexer->lookahead == '\n' || lexer->lookahead == '\r') && valid_symbols[LINK_IMPLICIT_END]) {
    lexer->mark_end(lexer);
    if (lexer->lookahead == '\r') {
      lexer->advance(lexer, false);
    }
    lexer->advance(lexer, false);
    scanner->link_depth--;
    lexer->result_symbol = LINK_IMPLICIT_END;
    return true;
  }

  if (scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_END]) {
    lexer->advance(lexer, false);
    scanner->in_argument = false;
    lexer->result_symbol = ARGUMENT_END;
    return true;
  }

  if (scanner->heading_level > 0 && (lexer->lookahead == '#' || lexer->lookahead == '=')) {
    char heading_end_string[100];
    const char *heading_prefix = scanner->is_hidden_heading ? "#" : "";
    strncpy(heading_end_string, heading_prefix, sizeof(heading_end_string) - 1);
    heading_end_string[sizeof(heading_end_string) - 1] = '\0';
    
    int len = strlen(heading_end_string);
    memset(heading_end_string + len, '=', scanner->heading_level);
    heading_end_string[len + scanner->heading_level] = '\0';

    lexer->mark_end(lexer);

    if (consume_string(lexer, heading_end_string)) {
      if (scanner->depth > 0 && valid_symbols[BRACKET_IMPLICIT_END]) {
        scanner->depth--;
        lexer->result_symbol = BRACKET_IMPLICIT_END;
        return true;
      }
      if (scanner->in_macro && valid_symbols[MACRO_IMPLICIT_END]) {
        scanner->in_macro = false;
        lexer->result_symbol = MACRO_IMPLICIT_END;
        return true;
      }
      if (scanner->link_depth > 0 && valid_symbols[LINK_IMPLICIT_END]) {
        scanner->link_depth--;
        lexer->result_symbol = LINK_IMPLICIT_END;
        return true;
      }
      if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
        scanner->in_argument = false;
        lexer->result_symbol = ARGUMENT_IMPLICIT_END;
        return true;
      }
      
      lexer->mark_end(lexer);
      scanner->heading_level = 0;
      scanner->is_hidden_heading = false;
      lexer->result_symbol = HEADING_END;
      return true;
    }
  }

  bool is_breakable_bracket = false;
  switch (top_type) {
      case BLOCK_TYPE_IF:
      case BLOCK_TYPE_FOLDING:
      case BLOCK_TYPE_WIKI:
      case BLOCK_TYPE_SCALE:
      case BLOCK_TYPE_COLOR:
        is_breakable_bracket = true;
        break;
      default: break;
  }

  bool is_bracket_implicit_end = is_breakable_bracket && valid_symbols[BRACKET_IMPLICIT_END];
  bool is_macro_implicit_end = scanner->in_macro && valid_symbols[MACRO_IMPLICIT_END];
  bool is_link_implicit_end = scanner->link_depth > 0 && valid_symbols[LINK_IMPLICIT_END];
  uint8_t heading_token = match_heading_token(lexer, is_bracket_implicit_end || is_macro_implicit_end || is_link_implicit_end);
  if (heading_token != NO_HEADING_TOKEN) {
    if (is_bracket_implicit_end) {
      scanner->depth--;
      lexer->result_symbol = BRACKET_IMPLICIT_END;
      return true;
    }
    if (is_macro_implicit_end) {
      scanner->in_macro = false;
      lexer->result_symbol = MACRO_IMPLICIT_END;
      return true;
    }
    if (is_link_implicit_end) {
      scanner->link_depth--;
      lexer->result_symbol = LINK_IMPLICIT_END;
      return true;
    }
    if (valid_symbols[heading_token]) {
      uint8_t offset = heading_token - HEADING1_START + 1;
      
      scanner->heading_level = offset > 6 ? offset - 6 : offset;
      scanner->is_hidden_heading = offset > 6;
      lexer->result_symbol = heading_token;
      return true;
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
            return consume_depth_block(lexer, scanner, IF_BLOCK_START, BLOCK_TYPE_IF, true);
          }
          if (valid_symbols[FOLDING_BLOCK_START] && consume_string(lexer, "folding")) {
            return consume_depth_block(lexer, scanner, FOLDING_BLOCK_START, BLOCK_TYPE_FOLDING, false);
          }
          if (consume_string(lexer, "s")) {
            if (valid_symbols[SYNTAX_BLOCK_START] && consume_string(lexer, "yntax")) {
              return consume_syntax_block(lexer, scanner);
            }
            if (valid_symbols[STYLE_BLOCK_START] && consume_string(lexer, "tyle")) {
              if (!consume_newline(lexer)) {
                return register_token(lexer, scanner, STYLE_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
              }
              return register_token(lexer, scanner, STYLE_BLOCK_START, BLOCK_TYPE_STYLE);
            }
          }
          if (valid_symbols[WIKI_BLOCK_START] && consume_string(lexer, "wiki")) {
            return consume_depth_block(lexer, scanner, WIKI_BLOCK_START, BLOCK_TYPE_WIKI, false);
          }
          if (valid_symbols[HTML_BLOCK_START] && consume_string(lexer, "html")) {
            return register_token(lexer, scanner, HTML_BLOCK_START, BLOCK_TYPE_HTML);
          }
          if (valid_symbols[LATEX_BLOCK_START] && consume_string(lexer, "latex")) {
            if (!consume_newline(lexer)) {
                return register_token(lexer, scanner, LATEX_FALLBACK_BLOCK_START, BLOCK_TYPE_NOWIKI);
              }
              return register_token(lexer, scanner, LATEX_BLOCK_START, BLOCK_TYPE_LATEX);
          }
        } 
        // 2-2. '#!'가 아니고 바로 색상명이 시작하는 케이스 (COLOR_BLOCK_START)
        else if (valid_symbols[COLOR_BLOCK_START]) {
          return consume_color_block(lexer, scanner);
        }
      }

      // 글자 크기 블록 ({{{+1 ...}}}, {{{-2 ...}}})
      if (valid_symbols[SCALE_BLOCK_START] && (lexer->lookahead == '+' || lexer->lookahead == '-')) {
        lexer->advance(lexer, false);
        return consume_scale_block(lexer, scanner);
      }

      // 4. Default Fallback -> NOWIKI
      if (valid_symbols[NOWIKI_START]) {
        return register_token(lexer, scanner, NOWIKI_START, BLOCK_TYPE_NOWIKI);
      }
    }
  }

  if (lexer->lookahead == '[') {
    lexer->advance(lexer, false);
    lexer->mark_end(lexer);
    if (lexer->lookahead == '[') {
      lexer->advance(lexer, false);
      lexer->mark_end(lexer);
      if (valid_symbols[LINK_START]) {
        scanner->link_depth++;
        lexer->result_symbol = LINK_START;
        return true;
      }
    }
    if (valid_symbols[MACRO_START] && consume_macro(lexer)) {
      if (lexer->lookahead == '(') {
        scanner->in_macro = true;
        lexer->result_symbol = MACRO_START;
        return true;
      }
    }
  }

  if (!scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_START] && consume_argument(lexer)) {
    scanner->in_argument = true;
    lexer->result_symbol = ARGUMENT_START;
    return true;
  }

  return false;
}