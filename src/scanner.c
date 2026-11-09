#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wctype.h>
#include <string.h>
#include <stdio.h>
#include "unicode_ranges.h"

// heading_status로 유효한 값
#define STATUS_NO_HEADING_SECTION 0
#define STATUS_MATCH_HEADING 1
#define STATUS_HEADING_SECTION 2

// cached_heading_token로 유효한 값
#define NO_HEADING_TOKEN 0
#define NO_CACHED 255

// flag류 공통
#define FLAG_NONE        0

// format_stack[n]로 유효한 값
#define FLAG_BOLD        (1 << 0) // 0x01 (0000 0001)
#define FLAG_ITALIC      (1 << 1) // 0x02 (0000 0010)
#define FLAG_STRIKE      (1 << 2) // 0x04 (0000 0100)
#define FLAG_STRIKE2     (1 << 3) // 0x08 (0000 1000)
#define FLAG_SUPERSCRIPT (1 << 4) // 0x10 (0001 0000)
#define FLAG_SUBSCRIPT   (1 << 5) // 0x20 (0010 0000)
#define FLAG_UNDERLINE   (1 << 6) // 0x40 (0100 0000)

// 가용 한계 명시
#define SERIALIZED_HEADER_SIZE 23 // 고정된 직렬화 필드의 총 크기 (1바이트 기준)
#define FORMAT_STACK_SIZE 256
#define MAX_BLOCK_DEPTH UINT8_MAX

#define MAX_PARAGRAPH_COUNT 257
#define PARAGRAPH_TYPE_STACK_SIZE ((MAX_PARAGRAPH_COUNT + 3) / 4)
#define PARAGRAPH_TYPE_STACK_CAPACITY (PARAGRAPH_TYPE_STACK_SIZE * 4)

static bool scan(void *payload, TSLexer *lexer, const bool *valid_symbols);

enum TokenType {
  COMMENT,
  ARGUMENT_START,
  ARGUMENT_END,
  ARGUMENT_IMPLICIT_END,
  NOWIKI_START,
  NOWIKI_INNER_START,
  IF_BLOCK_START,
  IF_FALLBACK_BLOCK_START,
  FOLDING_BLOCK_START,
  FOLDING_FALLBACK_BLOCK_START,
  SYNTAX_BLOCK_START,
  SYNTAX_FALLBACK_BLOCK_START,
  WIKI_BLOCK_START,
  WIKI_FALLBACK_BLOCK_START,
  HTML_BLOCK_START,
  HTML_FALLBACK_BLOCK_START,
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
  LINK_IMPLICIT_END,
  FILE_LINK_START,
  BOLD_START,
  BOLD_END,
  BOLD_IMPLICIT_END,
  ITALIC_START,
  ITALIC_END,
  ITALIC_IMPLICIT_END,
  STRIKE_START,
  STRIKE_END,
  STRIKE_IMPLICIT_END,
  STRIKE2_START,
  STRIKE2_END,
  STRIKE2_IMPLICIT_END,
  SUPERSCRIPT_START,
  SUPERSCRIPT_END,
  SUPERSCRIPT_IMPLICIT_END,
  SUBSCRIPT_START,
  SUBSCRIPT_END,
  SUBSCRIPT_IMPLICIT_END,
  UNDERLINE_START,
  UNDERLINE_END,
  UNDERLINE_IMPLICIT_END,
  FOOTNOTE_START,
  FOOTNOTE_END,
  FOOTNOTE_IMPLICIT_END,
  TABLE_END,
  TABLE_ROW_START,
  TABLE_ROW_END,
  TABLE_ROW_IMPLICIT_END,
  TABLE_CELL_SPLIT,
  TABLE_CAPTION_START,
  TABLE_CAPTION_END,
  TABLE_CAPTION_IMPLICIT_END,
  TABLE_ARGUMENT_START,
  TABLE_ARGUMENT_END,
  TABLE_ARGUMENT_IMPLICIT_END,
  TABLE_SPECIAL_ARGUMENT,
  HORIZONTAL_LINE,
  HORIZONTAL_LINE_IMPLICIT,
  PARAGRAPH_START,
  PARAGRAPH_END,
  PARAGRAPH_SKIP,
  UNORDERED_LIST_PARAGRAPH_START,
  ORDERED_LIST_PARAGRAPH_START,
  BLOCKQUOTE_PARAGRAPH_START,
  PIPE,
  GREATER_THAN_SIGN,
  SPACE,
  SOFT_LINE_PREFIX,
  SOFT_LINE_ENDING,
  LINE_ENDING,
  TOKEN_EOF,
  NONE,
};

// block_stack[n]의 값으로 유효한 값
enum BlockType {
  BLOCK_TYPE_NONE,
  BLOCK_TYPE_NOWIKI,
  BLOCK_TYPE_IF,
  BLOCK_TYPE_FOLDING,
  BLOCK_TYPE_SYNTAX,
  BLOCK_TYPE_WIKI,
  BLOCK_TYPE_HTML,
  BLOCK_TYPE_STYLE,
  BLOCK_TYPE_LATEX,
  BLOCK_TYPE_SCALE,
  BLOCK_TYPE_COLOR,
  BLOCK_TYPE_FALLBACK
};

// paragraph_type_stack[n]의 값으로 유효한 값
enum ParagraphType {
  PARAGRAPH_TYPE_PARAGRAPH,
  PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH,
  PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH,
  PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH,
  PARAGRAPH_TYPE_FALLBACK = 255 // 실제 값으로 저장되지 않음. 오류 분기용
};

typedef struct {
  bool in_macro; // 매크로 문법 내부
  bool in_argument; // 매개변수 문법 내부
  bool is_horizontal_line_valid; // 수평줄 문법이 들어오기 알맞은지 여부
  uint8_t heading_level; // 현재 줄 문단 레벨
  uint8_t heading_status; // 현재 줄 문단 매칭 상태
  uint8_t heading_levels; // 문단 레벨별 활성화 기록 (bitmask)
  bool is_hidden_heading; // 현재 줄 문단이 숨김 상태인지
  uint8_t cached_heading_token; // 문단 매칭 결과 캐싱
  uint32_t link_depth; // 링크 문법 깊이
  uint32_t footnote_depth; // 주석 문법 깊이
  uint32_t block_depth; // block류 문법 깊이
  uint32_t table_end_column; // 테이블 문법 종료 가능 열
  uint16_t paragraph_type_stack_size; // 전체 paragraph_type_stack 값 개수
  uint16_t paragraph_skipped_index;
  uint32_t paragraph_end_column;
  uint8_t block_stack[256]; // block_depth와 연동
  uint8_t table_row_stack[32]; // (block_depth / 8)와 연동
  uint8_t format_stack[256]; // block_depth + footnote_depth + link_depth와 연동
  uint8_t paragraph_stack[MAX_PARAGRAPH_COUNT]; // block_depth or block_depth + table과 연동
  uint8_t paragraph_type_stack[PARAGRAPH_TYPE_STACK_SIZE]; // (MAX_PARAGRAPH_COUNT + 3) / 4와 연동
} Scanner;

// -------- 직렬화 및 복원 유효성 검사 ---------------------------------------------------------

// paragraph_type_stack_size의 유효성 검사
static bool is_valid_paragraph_type_stack(const uint8_t *stack, uint16_t count) {
  if (count > PARAGRAPH_TYPE_STACK_CAPACITY) return false;

  for (uint16_t i = 0; i < count; i++) {
    uint8_t type = (stack[i / 4] >> ((i % 4) * 2)) & 0x03u;
    if (type > PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH) return false;
  }
  return true;
}

// paragraph_stack의 유효성 검사
// paragraph_stack의 모든 값의 합이 paragraph_type_stack_size와 연동되는 지 검사
static bool is_valid_paragraph_state(const uint8_t *paragraph_stack, uint32_t paragraph_count, uint16_t paragraph_type_stack_size) {
  if (paragraph_count > MAX_PARAGRAPH_COUNT) return false;

  uint32_t total = 0;
  for (uint32_t i = 0; i < paragraph_count; i++) {
    total += paragraph_stack[i];
    if (total > paragraph_type_stack_size) return false;
  }
  return total == paragraph_type_stack_size;
}

// cached_heading_toke르의 유효성 검사
static inline bool is_valid_cached_heading_token(uint8_t token) {
  return token == NO_HEADING_TOKEN || token == NO_CACHED ||
    (token >= HEADING1_START && token <= HIDDEN_HEADING6_START);
}

// block_stack 내부 값이 유효한 지 검사
static inline bool is_valid_block_type(uint8_t type) {
  return type > BLOCK_TYPE_NONE && type <= BLOCK_TYPE_FALLBACK;
}

// -------- 헬퍼 함수 ---------------------------------------------------------

static inline void set_table_row_stack(Scanner* s) {
  if (s->block_depth <= MAX_BLOCK_DEPTH && s->block_depth / 8 < sizeof s->table_row_stack)
    s->table_row_stack[s->block_depth / 8] |= (uint8_t)(1u << (s->block_depth % 8));
}

static inline void clear_table_row_stack(Scanner* s) {
  if (s->block_depth <= MAX_BLOCK_DEPTH && s->block_depth / 8 < sizeof s->table_row_stack)
    s->table_row_stack[s->block_depth / 8] &= (uint8_t)~(1u << (s->block_depth % 8));
}

static inline bool get_table_row_stack(const Scanner* s) {
  return s->block_depth <= MAX_BLOCK_DEPTH &&
    s->block_depth / 8 < sizeof s->table_row_stack &&
    (s->table_row_stack[s->block_depth / 8] & (1u << (s->block_depth % 8))) != 0;
}

static bool push_paragraph(Scanner* scanner, uint8_t type) {
  uint16_t paragraph_depth = scanner->block_depth + get_table_row_stack(scanner);

  if (scanner->paragraph_stack[paragraph_depth] >= UINT8_MAX) return false;
  if (scanner->paragraph_type_stack_size >= PARAGRAPH_TYPE_STACK_CAPACITY) return false;

  scanner->paragraph_stack[paragraph_depth]++;

  uint16_t index = scanner->paragraph_type_stack_size++;
  uint16_t byte_index = index / 4;
  uint8_t bit_offset = (index % 4) * 2;

  scanner->paragraph_type_stack[byte_index] &= ~(0x03u << bit_offset);
  scanner->paragraph_type_stack[byte_index] |= (type & 0x03u) << bit_offset;
  return true;
}

static bool remove_top_paragraph(Scanner *scanner) {
  uint16_t paragraph_depth = scanner->block_depth + get_table_row_stack(scanner);
  if (scanner->paragraph_stack[paragraph_depth] > 0 && scanner->paragraph_type_stack_size > 0) {
    scanner->paragraph_stack[paragraph_depth]--;
    scanner->paragraph_type_stack_size--;
    return true;
  }
  return false;
}

static uint8_t get_paragraph_type(Scanner *scanner, uint16_t index) {
  if (index < 0 || index >= PARAGRAPH_TYPE_STACK_CAPACITY) {
    return PARAGRAPH_TYPE_FALLBACK;
  }
  
  uint16_t byte_index = index / 4;
  uint8_t bit_offset = (index % 4) * 2;

  return (scanner->paragraph_type_stack[byte_index] >> bit_offset) & 0x03u;
}

static inline bool get_format_depth(const Scanner *scanner, uint32_t *depth) {
  if (scanner->block_depth >= FORMAT_STACK_SIZE 
    || scanner->link_depth >= FORMAT_STACK_SIZE - scanner->block_depth 
    || scanner->footnote_depth >= FORMAT_STACK_SIZE - scanner->block_depth - scanner->link_depth) {
    return false;
  }
  *depth = scanner->block_depth + scanner->link_depth + scanner->footnote_depth;
  return true;
}

static inline bool can_increment_format_depth(const Scanner *scanner) {
  uint32_t depth;
  return get_format_depth(scanner, &depth) && depth < FORMAT_STACK_SIZE - 1;
}

static inline void set_format(Scanner *scanner, uint8_t flag) {
  uint32_t depth;
  if (get_format_depth(scanner, &depth)) scanner->format_stack[depth] |= flag;
}

static inline void reset_format(Scanner *scanner) {
  uint32_t depth;
  if (get_format_depth(scanner, &depth)) {
    scanner->format_stack[depth] = 0;
  }
}

static inline void remove_format(Scanner *scanner, uint8_t flag) {
  uint32_t depth;
  if (get_format_depth(scanner, &depth)) scanner->format_stack[depth] &= ~flag;
}

static inline bool has_format(const Scanner *scanner, uint8_t flag) {
  uint32_t depth;
  return get_format_depth(scanner, &depth) && (scanner->format_stack[depth] & flag) != 0;
}

static void mark_end(Scanner *s, TSLexer *lexer) {
  lexer->mark_end(lexer);
}

static void advance(TSLexer *lexer) {
  lexer->advance(lexer, false);
}

static uint32_t utf8_decode(const char **p) {
  const unsigned char *s = (const unsigned char *)*p;
  uint32_t cp;

  if (s[0] < 0x80) {
    cp = s[0];
    *p += 1;
  } else if ((s[0] & 0xE0) == 0xC0) {
    cp = ((s[0] & 0x1F) << 6)
       | (s[1] & 0x3F);
    *p += 2;
  } else if ((s[0] & 0xF0) == 0xE0) {
    cp = ((s[0] & 0x0F) << 12)
       | ((s[1] & 0x3F) << 6)
       | (s[2] & 0x3F);
    *p += 3;
  } else {
    cp = ((s[0] & 0x07) << 18)
       | ((s[1] & 0x3F) << 12)
       | ((s[2] & 0x3F) << 6)
       | (s[3] & 0x3F);
    *p += 4;
  }

  return cp;
}

static bool consume_string(TSLexer *lexer, const char *str) {
  const char *p = str;

  while (*p) {
    uint32_t cp = utf8_decode(&p);

    if (lexer->lookahead != cp)
      return false;

    advance(lexer);
  }

  return true;
}

static bool consume_newline(TSLexer *lexer) {
  if (lexer->lookahead == '\r') {
    advance(lexer);
  }
  if (lexer->lookahead != '\n') return false;
  advance(lexer);
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
    advance(lexer);
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

static bool register_block(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t TOKEN_TYPE, uint8_t BLOCK_TYPE) {
  if (valid_symbols[TOKEN_TYPE]) {
    if (scanner->block_depth < MAX_BLOCK_DEPTH && can_increment_format_depth(scanner)) {
      scanner->block_stack[scanner->block_depth++] = BLOCK_TYPE;
      reset_format(scanner);
    } else {
      return false;
    }
    lexer->result_symbol = TOKEN_TYPE;
    return true;
  }

  if (BLOCK_TYPE != BLOCK_TYPE_FALLBACK && valid_symbols[TOKEN_TYPE + 1]) {
    if (scanner->block_depth < MAX_BLOCK_DEPTH && can_increment_format_depth(scanner)) {
      scanner->block_stack[scanner->block_depth++] = BLOCK_TYPE_FALLBACK;
      reset_format(scanner);
    } else {
      return false;
    }
    lexer->result_symbol = TOKEN_TYPE + 1;
    return true;
  }

  return false;
}

static uint8_t match_heading_token(TSLexer *lexer, Scanner *scanner, uint8_t top_type, bool is_simulate) {
  if (lexer->get_column(lexer) != 0) return NO_HEADING_TOKEN;
  if (lexer->lookahead != '=') return NO_HEADING_TOKEN;
  if (top_type != BLOCK_TYPE_NONE) return NO_HEADING_TOKEN;

  uint8_t open_count = 0;
  while (lexer->lookahead == '=' && open_count < 6) {
    advance(lexer);
    open_count++;
  }

  bool is_hidden = false;
  if (lexer->lookahead == '#') {
    is_hidden = true;
    advance(lexer);
  }

  uint8_t base_symbol = is_hidden ? HIDDEN_HEADING1_START : HEADING1_START;
  uint8_t target_symbol = base_symbol + (open_count - 1);

  if (open_count == 0 || open_count > 6 || lexer->lookahead != ' ') { return NO_HEADING_TOKEN; }

  advance(lexer);
  if (!is_simulate) {
    mark_end(scanner, lexer);
  }

  bool has_trailing_space = false;
  bool has_closing_hash = false;
  uint8_t trailing_equals = 0;
  int32_t last_char;

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
    advance(lexer);
  }
  
  if (trailing_equals == open_count && has_trailing_space) {
    if (!is_hidden || has_closing_hash) {
      return target_symbol;
    }
  }

  return NO_HEADING_TOKEN;
}

static bool consume_depth_block(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t TOKEN_TYPE, uint8_t BLOCK_TYPE, bool is_header_needed) {
  if (lexer->lookahead != '\r' && lexer->lookahead != '\n') {
    if (lexer->lookahead != ' ') {
      return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE + 1, BLOCK_TYPE_FALLBACK);
    }

    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      if (lexer->lookahead == '}') {
        if (consume_string(lexer, "}}}")) {
          return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE + 1, BLOCK_TYPE_FALLBACK);
        }
      } else {
        advance(lexer);
      }
    }
  } else {
    if (is_header_needed) {
      return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE + 1, BLOCK_TYPE_FALLBACK);
    }
  }

  if (!consume_newline(lexer)) {
    return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE + 1, BLOCK_TYPE_FALLBACK);
  }

  if (!valid_symbols[TOKEN_TYPE]) {
    return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE + 1, BLOCK_TYPE_FALLBACK);
  }

  return register_block(lexer, scanner, valid_symbols, TOKEN_TYPE, BLOCK_TYPE);
}

static bool consume_syntax_block(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (lexer->lookahead != ' ') {
    return register_block(lexer, scanner, valid_symbols, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }

  advance(lexer);

  if (!consume_language(lexer)) {
    return register_block(lexer, scanner, valid_symbols, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }
  
  if (!consume_newline(lexer)) {
    return register_block(lexer, scanner, valid_symbols, SYNTAX_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }

  return register_block(lexer, scanner, valid_symbols, SYNTAX_BLOCK_START, BLOCK_TYPE_SYNTAX);
}

static bool consume_scale_block(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (!iswdigit(lexer->lookahead)) {
    return register_block(lexer, scanner, valid_symbols, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }

  if (!(lexer->lookahead >= '1' && lexer->lookahead <= '5')) {
    return register_block(lexer, scanner, valid_symbols, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }

  advance(lexer);
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return register_block(lexer, scanner, valid_symbols, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
    }
    advance(lexer);
  }

  return register_block(lexer, scanner, valid_symbols, SCALE_BLOCK_START, BLOCK_TYPE_SCALE);
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
    advance(lexer);
  }
  buf[len] = '\0';

  if (len == 0) return false;

  // 1. 모든 문자가 Hex이고 길이가 3 또는 6인 경우 Valid Hex
  if (all_hex && (len == 3 || len == 6)) {
    return true;
  }

  // 2. Named Color 목록 매칭
  static const char *const VALID_COLORS[] = {"aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgrey", "darkgreen", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgrey", "lightgreen", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen", "transparent"};

  int count = sizeof(VALID_COLORS) / sizeof(VALID_COLORS[0]);
  for (int i = 0; i < count; i++) {
    if (strcmp(buf, VALID_COLORS[i]) == 0) return true;
  }

  return false;
}

static bool consume_color_block(TSLexer *lexer, Scanner* scanner, const bool *valid_symbols) {
  if (!consume_single_color(lexer)) return register_block(lexer, scanner, valid_symbols, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);

  if (lexer->lookahead == ',') {
    advance(lexer);
    if (lexer->lookahead != '#') {
      return register_block(lexer, scanner, valid_symbols, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
    }
    advance(lexer);
    if (!consume_single_color(lexer)) return register_block(lexer, scanner, valid_symbols, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
  }
  
  if (!consume_newline(lexer)) {
    if (lexer->lookahead != ' ') {
      return register_block(lexer, scanner, valid_symbols, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
    }
    advance(lexer);
  }

  return register_block(lexer, scanner, valid_symbols, COLOR_BLOCK_START, BLOCK_TYPE_COLOR);
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
    advance(lexer);
  }
  buf[len] = '\0';

  if (len == 0) return false;

  // 매크로 이름 목록 매칭
  static const char *const VALID_NAMES[] = {
    "anchor", "age", "dday", "youtube", "vimeo", 
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

static bool consume_argument(TSLexer *lexer, Scanner *scanner) {
  if (lexer->lookahead != '@') return false;
  advance(lexer);
  mark_end(scanner, lexer);

  bool is_zero_length = true;
  bool has_default_value = false;
  while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
    if (!has_default_value && lexer->lookahead == '=') {
      if (is_zero_length) return false;
      is_zero_length = true;
      has_default_value = true;
      advance(lexer);
      continue;
    }

    if (lexer->lookahead == '@') {
      if (is_zero_length) return false;
      return true;
    }

    if (!has_default_value && !is_unicode_letter_or_number((uint32_t)lexer->lookahead)) return false;
    
    is_zero_length = false;

    advance(lexer);
  }

  return false;
}

static bool register_argument_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END]) {
    scanner->in_argument = false;
    lexer->result_symbol = ARGUMENT_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_paragraph_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[PARAGRAPH_END] && remove_top_paragraph(scanner)) {
    lexer->result_symbol = PARAGRAPH_END;
    return true;
  }
  return false;
}

static bool register_bracket_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (scanner->block_depth > 0 && valid_symbols[BRACKET_IMPLICIT_END]) {
    scanner->block_depth--;
    lexer->result_symbol = BRACKET_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_macro_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (scanner->in_macro && valid_symbols[MACRO_IMPLICIT_END]) {
    scanner->in_macro = false;
    lexer->result_symbol = MACRO_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_link_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (scanner->link_depth > 0 && valid_symbols[LINK_IMPLICIT_END]) {
    scanner->link_depth--;
    lexer->result_symbol = LINK_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_footnote_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (scanner->footnote_depth > 0 && valid_symbols[FOOTNOTE_IMPLICIT_END]) {
    scanner->footnote_depth--;
    lexer->result_symbol = FOOTNOTE_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_table_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[TABLE_END]) {
    lexer->result_symbol = TABLE_END;
    return true;
  }
  return false;
}

static bool register_table_row_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint32_t offset) {
  bool is_formatted = false;
  uint32_t format_depth;
  if (get_format_depth(scanner, &format_depth)) {
    is_formatted = scanner->format_stack[format_depth] > 0;
  }

  if (is_formatted || !get_table_row_stack(scanner) || lexer->get_column(lexer) != scanner->table_end_column + offset) return false;

  if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

  if (valid_symbols[TABLE_ROW_END]) {
    clear_table_row_stack(scanner);
    lexer->result_symbol = TABLE_ROW_END;
    return true;
  }
  return false;
}

static bool register_table_row_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[TABLE_ROW_IMPLICIT_END]) {
    clear_table_row_stack(scanner);
    lexer->result_symbol = TABLE_ROW_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_table_caption_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[TABLE_CAPTION_IMPLICIT_END]) {
    lexer->result_symbol = TABLE_CAPTION_IMPLICIT_END;
    return true;
  }
  return false;
}

static bool register_table_argument_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[TABLE_ARGUMENT_IMPLICIT_END]) {
    lexer->result_symbol = TABLE_ARGUMENT_IMPLICIT_END;
    return true;
  }
  return false;
}


static bool register_format_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t FLAG, uint8_t TOKEN_TYPE) {
  if (has_format(scanner, FLAG) && valid_symbols[TOKEN_TYPE]) {
    remove_format(scanner, FLAG);
    lexer->result_symbol = TOKEN_TYPE;
    return true;
  }
  return false;
}

static bool register_bold_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_BOLD, BOLD_IMPLICIT_END);
}
static bool register_italic_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_ITALIC, ITALIC_IMPLICIT_END);
}
static bool register_strike_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_STRIKE, STRIKE_IMPLICIT_END);
}
static bool register_strike2_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_STRIKE2, STRIKE2_IMPLICIT_END);
}
static bool register_superscript_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_SUPERSCRIPT, SUPERSCRIPT_IMPLICIT_END);
}
static bool register_subscript_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_SUBSCRIPT, SUBSCRIPT_IMPLICIT_END);
}
static bool register_underline_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  return register_format_implicit_end(lexer, scanner, valid_symbols, FLAG_UNDERLINE, UNDERLINE_IMPLICIT_END);
}

static bool close_format_implicit_ends(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t EXCLUDED_FLAG) {
  if (EXCLUDED_FLAG != FLAG_BOLD && register_bold_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_ITALIC && register_italic_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_STRIKE && register_strike_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_STRIKE2 && register_strike2_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_SUPERSCRIPT && register_superscript_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_SUBSCRIPT && register_subscript_implicit_end(lexer, scanner, valid_symbols)) return true;
  if (EXCLUDED_FLAG != FLAG_UNDERLINE && register_underline_implicit_end(lexer, scanner, valid_symbols)) return true;
  return false;
}

static bool consume_format(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t FLAG, uint8_t START_TOKEN_TYPE) {
  if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
        
  bool is_formatted = has_format(scanner, FLAG);
  if (!is_formatted && valid_symbols[START_TOKEN_TYPE]) {
    mark_end(scanner, lexer);
    set_format(scanner, FLAG);
    lexer->result_symbol = START_TOKEN_TYPE;
    return true;
  }

  uint8_t END_TOKEN_TYPE = START_TOKEN_TYPE + 1;
  if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG)) return true;
  if (is_formatted && valid_symbols[END_TOKEN_TYPE]) {
    mark_end(scanner, lexer);
    remove_format(scanner, FLAG);
    lexer->result_symbol = END_TOKEN_TYPE;
    return true;
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
  if (scanner == NULL || buffer == NULL) return 0;

  uint32_t format_count;
  if (scanner->block_depth > MAX_BLOCK_DEPTH || scanner->link_depth > UINT8_MAX ||
      scanner->footnote_depth > UINT8_MAX ||
      !get_format_depth(scanner, &format_count)) return 0;
  format_count++;

  uint32_t table_count = scanner->block_depth / 8 + 1;
  uint32_t paragraph_count = scanner->block_depth + get_table_row_stack(scanner) + 1;

  // 고정된 직렬화 필드 + 가변적인 직렬화 필드의 총 크기
  size_t total_size = SERIALIZED_HEADER_SIZE + scanner->block_depth + table_count +
    paragraph_count + sizeof scanner->paragraph_type_stack + format_count;

  if (total_size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE ||
      !is_valid_paragraph_state(scanner->paragraph_stack, paragraph_count, scanner->paragraph_type_stack_size) ||
      !is_valid_paragraph_type_stack(scanner->paragraph_type_stack, scanner->paragraph_type_stack_size) ||
      scanner->heading_level > 6 ||
      scanner->heading_status > STATUS_HEADING_SECTION ||
      (scanner->heading_status == STATUS_MATCH_HEADING && scanner->heading_level == 0) ||
      (scanner->heading_levels & 0xc0) != 0 ||
      !is_valid_cached_heading_token(scanner->cached_heading_token)) return 0;
  for (uint32_t i = 0; i < scanner->block_depth; i++) {
    if (!is_valid_block_type(scanner->block_stack[i])) return 0;
  }
  for (uint32_t i = 0; i < format_count; i++) {
    if ((scanner->format_stack[i] & 0x80) != 0) return 0;
  }

  size_t size = 0;
  
  // -------- 고정된 직렬화 필드 ---------------------------------------------------------
  buffer[size++] = (char)scanner->heading_level;
  buffer[size++] = (char)scanner->heading_status;
  buffer[size++] = (char)scanner->heading_levels;
  buffer[size++] = (char)scanner->is_hidden_heading;
  buffer[size++] = (char)scanner->cached_heading_token;
  buffer[size++] = (char)scanner->in_macro;
  buffer[size++] = (char)scanner->in_argument;
  buffer[size++] = (char)scanner->is_horizontal_line_valid;
  buffer[size++] = (char)(scanner->table_end_column & 0xff);
  buffer[size++] = (char)((scanner->table_end_column >> 8) & 0xff);
  buffer[size++] = (char)((scanner->table_end_column >> 16) & 0xff);
  buffer[size++] = (char)((scanner->table_end_column >> 24) & 0xff);
  buffer[size++] = (char)(scanner->paragraph_type_stack_size & 0xff);
  buffer[size++] = (char)(scanner->paragraph_type_stack_size >> 8);
  buffer[size++] = (char)(scanner->paragraph_skipped_index & 0xff);
  buffer[size++] = (char)(scanner->paragraph_skipped_index >> 8);
  buffer[size++] = (char)(scanner->paragraph_end_column & 0xff);
  buffer[size++] = (char)((scanner->paragraph_end_column >> 8) & 0xff);
  buffer[size++] = (char)((scanner->paragraph_end_column >> 16) & 0xff);
  buffer[size++] = (char)((scanner->paragraph_end_column >> 24) & 0xff);
  buffer[size++] = (char)scanner->link_depth;
  buffer[size++] = (char)scanner->footnote_depth;
  buffer[size++] = (char)scanner->block_depth;

  // -------- 가변적인 직렬화 필드 ---------------------------------------------------------
  for (uint32_t i = 0; i < scanner->block_depth; i++) {
    buffer[size++] = (char)scanner->block_stack[i];
  }
  for (uint32_t i = 0; i < table_count; i++) {
    buffer[size++] = (char)scanner->table_row_stack[i];
  }
  for (uint32_t i = 0; i < paragraph_count; i++) {
    buffer[size++] = (char)scanner->paragraph_stack[i];
  }
  for (uint32_t i = 0; i < PARAGRAPH_TYPE_STACK_SIZE; i++) {
    buffer[size++] = (char)scanner->paragraph_type_stack[i];
  }
  for (uint32_t i = 0; i < format_count; i++) {
    buffer[size++] = (char)scanner->format_stack[i];
  }
  return size;
}

void tree_sitter_namumark_external_scanner_deserialize(void *payload, const char *buffer, unsigned length) {
  Scanner *scanner = (Scanner *)payload;
  if (scanner == NULL) return;

  memset(scanner->block_stack, 0, sizeof scanner->block_stack);
  memset(scanner->table_row_stack, 0, sizeof scanner->table_row_stack);
  memset(scanner->paragraph_stack, 0, sizeof scanner->paragraph_stack);
  memset(scanner->paragraph_type_stack, 0, PARAGRAPH_TYPE_STACK_SIZE);
  memset(scanner->format_stack, 0, sizeof scanner->format_stack);
  scanner->heading_level = 0;
  scanner->heading_status = STATUS_NO_HEADING_SECTION;
  scanner->heading_levels = 0;
  scanner->is_hidden_heading = false;
  scanner->cached_heading_token = NO_CACHED;
  scanner->in_macro = false;
  scanner->in_argument = false;
  scanner->is_horizontal_line_valid = true;
  scanner->table_end_column = UINT32_MAX;
  scanner->paragraph_type_stack_size = 0;
  scanner->paragraph_skipped_index = 0;
  scanner->paragraph_end_column = 0;
  scanner->link_depth = 0;
  scanner->footnote_depth = 0;
  scanner->block_depth = 0;

  if (buffer == NULL || length < SERIALIZED_HEADER_SIZE) return;

  const unsigned char *data = (const unsigned char *)buffer;
  size_t size = 0;
  uint8_t heading_level = data[size++];
  uint8_t heading_status = data[size++];
  uint8_t heading_levels = data[size++];
  uint8_t is_hidden_heading = data[size++];
  uint8_t cached_heading_token = data[size++];
  uint8_t in_macro = data[size++];
  uint8_t in_argument = data[size++];
  uint8_t is_horizontal_line_valid = data[size++];
  uint32_t table_end_column = (uint8_t)data[size++];
  table_end_column |= (uint32_t)(uint8_t)data[size++] << 8;
  table_end_column |= (uint32_t)(uint8_t)data[size++] << 16;
  table_end_column |= (uint32_t)(uint8_t)data[size++] << 24;
  uint16_t paragraph_type_stack_size = data[size++];
  paragraph_type_stack_size |= (uint16_t)data[size++] << 8;
  uint16_t paragraph_skipped_index = data[size++];
  paragraph_skipped_index |= (uint16_t)data[size++] << 8;
  uint32_t paragraph_end_column = (uint8_t)data[size++];
  paragraph_end_column |= (uint32_t)(uint8_t)data[size++] << 8;
  paragraph_end_column |= (uint32_t)(uint8_t)data[size++] << 16;
  paragraph_end_column |= (uint32_t)(uint8_t)data[size++] << 24;
  uint32_t saved_link_depth = data[size++];
  uint32_t saved_footnote_depth = data[size++];
  uint32_t saved_block_depth = data[size++];

  if (heading_level > 6 ||
      heading_status > STATUS_HEADING_SECTION ||
      (heading_status == STATUS_MATCH_HEADING && heading_level == 0) ||
      (heading_levels & 0xc0) != 0 ||
      is_hidden_heading > 1 ||
      !is_valid_cached_heading_token(cached_heading_token) ||
      in_macro > 1 || in_argument > 1 || is_horizontal_line_valid > 1 ||
      paragraph_type_stack_size > PARAGRAPH_TYPE_STACK_CAPACITY ||
      paragraph_skipped_index > PARAGRAPH_TYPE_STACK_CAPACITY ||
      saved_block_depth > MAX_BLOCK_DEPTH ||
      saved_block_depth + saved_link_depth + saved_footnote_depth > UINT8_MAX) return;

  uint32_t table_count = saved_block_depth / 8 + 1;
  size_t block_offset = size;
  size_t table_offset = block_offset + saved_block_depth;
  if (length - size < saved_block_depth + table_count) return;

  bool table_active = (data[table_offset + saved_block_depth / 8] &
    (1u << (saved_block_depth % 8))) != 0;
  uint32_t paragraph_count = saved_block_depth + table_active + 1;
  uint32_t format_count = saved_block_depth + saved_link_depth + saved_footnote_depth + 1;
  size_t remaining = length - size;
  if (remaining < (size_t)saved_block_depth + table_count + paragraph_count + PARAGRAPH_TYPE_STACK_SIZE + format_count) return;

  for (uint32_t i = 0; i < saved_block_depth; i++) {
    if (!is_valid_block_type(data[block_offset + i])) return;
  }
  size_t paragraph_offset = table_offset + table_count;
  size_t paragraph_type_offset = paragraph_offset + paragraph_count;
  size_t format_offset = paragraph_type_offset + PARAGRAPH_TYPE_STACK_SIZE;
  if (!is_valid_paragraph_state(data + paragraph_offset, paragraph_count, paragraph_type_stack_size) ||
      !is_valid_paragraph_type_stack(data + paragraph_type_offset, paragraph_type_stack_size)) return;
  for (uint32_t i = 0; i < format_count; i++) {
    if ((data[format_offset + i] & 0x80) != 0) return;
  }

  memcpy(scanner->block_stack, data + block_offset, saved_block_depth);
  memcpy(scanner->table_row_stack, data + table_offset, table_count);
  memcpy(scanner->paragraph_stack, data + paragraph_offset, paragraph_count);
  memcpy(scanner->paragraph_type_stack, data + paragraph_type_offset, sizeof scanner->paragraph_type_stack);
  memcpy(scanner->format_stack, data + format_offset, format_count);

  scanner->heading_level = heading_level;
  scanner->heading_status = heading_status;
  scanner->heading_levels = heading_levels;
  scanner->is_hidden_heading = is_hidden_heading != 0;
  scanner->cached_heading_token = cached_heading_token;
  scanner->in_macro = in_macro != 0;
  scanner->in_argument = in_argument != 0;
  scanner->is_horizontal_line_valid = is_horizontal_line_valid != 0;
  scanner->table_end_column = table_end_column;
  scanner->paragraph_type_stack_size = paragraph_type_stack_size;
  scanner->paragraph_skipped_index = paragraph_skipped_index;
  scanner->paragraph_end_column = paragraph_end_column;
  scanner->block_depth = saved_block_depth;
  scanner->link_depth = saved_link_depth;
  scanner->footnote_depth = saved_footnote_depth;
}

// --- Main Scan ---

static bool scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  uint8_t top_type = scanner->block_depth > 0 ? scanner->block_stack[scanner->block_depth - 1] : BLOCK_TYPE_NONE;

  // format류 문법이 현재 적용되었는지
  bool is_formatted = false;
  uint32_t format_depth;
  if (get_format_depth(scanner, &format_depth)) {
    is_formatted = scanner->format_stack[format_depth] > 0;
  }

  // paragraph_stack의 현재 깊이 추적
  uint16_t paragraph_depth = scanner->block_depth + get_table_row_stack(scanner);

  // 현재 paragraph_depth의 첫번째 paragraph_type의 index 값
  uint16_t paragraph_type_stack_first_index = scanner->paragraph_type_stack_size - scanner->paragraph_stack[paragraph_depth];

  // 문서 끝에 도달하면 모두 강제 종료
  if (lexer->eof(lexer)) {
    if (register_table_row_end(lexer, scanner, valid_symbols, 0)) return true;
    if (register_table_end(lexer, scanner, valid_symbols)) return true;
    if (register_bracket_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_macro_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_caption_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_row_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
    if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

    if (valid_symbols[TOKEN_EOF]) {
      lexer->result_symbol = TOKEN_EOF;
      return true;
    }
    return false;
  }

  // comment 처리
  // 줄을 ##로 시작하는 경우, 한 줄을 모두 점유함.
  if (lexer->get_column(lexer) == 0 && lexer->lookahead == '#' && consume_string(lexer, "##") && valid_symbols[COMMENT]) {
    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      advance(lexer);
    }

    mark_end(scanner, lexer);
    lexer->result_symbol = COMMENT;
    return true;
  }

  // block류 문법 종료 처리 - bracket_end
  // block류 문법은 공통적으로 }}} 문자열과 함께 종료됨.
  if (lexer->lookahead == '}' && scanner->block_depth > 0) {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "}}}")) {
      // block 내부의 table 문법이 유효하게 끝나는 경우 정상 종료
      if (register_table_row_end(lexer, scanner, valid_symbols, 3)) return true;
      if (register_table_end(lexer, scanner, valid_symbols)) return true;

      // block 내부의 모든 허용 문법에 대한 암묵적 종료 처리
      if (register_macro_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_table_caption_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_table_row_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_table_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[BRACKET_END]) {
        mark_end(scanner, lexer);
        scanner->block_depth--;
        lexer->result_symbol = BRACKET_END;
        return true;
      }
    }
    return false;
  }

  // macro_end 처리
  if (lexer->lookahead == ')' && scanner->in_macro) {
    mark_end(scanner, lexer);

    // scanner->in_macro 상태에서 )]를 만나면 macro_end 처리.
    if (consume_string(lexer, ")]")) {
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[MACRO_END]) {
        mark_end(scanner, lexer);
        scanner->in_macro = false;
        lexer->result_symbol = MACRO_END;
        return true;
      }
    }
    return false;
  }

  if (lexer->lookahead == '|') {
    mark_end(scanner, lexer);

    // link_pipe 처리
    // 현재 열려 있는 링크(link_depth > 0)에 대한 파이프 처리.
    if (scanner->link_depth > 0) {
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      
      if (valid_symbols[LINK_PIPE]) {
        advance(lexer);
        mark_end(scanner, lexer);
        lexer->result_symbol = LINK_PIPE;
        return true;
      }
    }

    advance(lexer);

    // -------- table 시작 부분 처리 ---------------------------------------------------------
    // table_row_start, table_cell_split, table_caption_start, table_caption_end 
    // table_row: (table_row_start | table_cell_split | table_caption) -> repeat(table_cell_split)으로 구성.
    // format류 문법이 table보다 우선하므로, is_formatted는 항시 false여야 함.

    // table_caption_end 처리
    if (!is_formatted && get_table_row_stack(scanner) && valid_symbols[TABLE_CAPTION_END]) {
      mark_end(scanner, lexer);
      lexer->result_symbol = TABLE_CAPTION_END;
      return true;
    }

    // ||가 오는 경우
    if (lexer->lookahead == '|') {
      advance(lexer);
      
      // table_row_start 처리
      // table_row_stack이 등록되지 않은 상태, ||의 위치가 paragraph의 시작이어야 함.
      if (!is_formatted && !get_table_row_stack(scanner) && lexer->get_column(lexer) == scanner->paragraph_end_column + 2 && valid_symbols[TABLE_ROW_START]) {
        mark_end(scanner, lexer);
        set_table_row_stack(scanner);
        lexer->result_symbol = TABLE_ROW_START;
        return true;
      }

      // table_cell_split 처리
      // table_row_stack이 등록된 상태여야 함.
      if (!is_formatted && get_table_row_stack(scanner)) {
        if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

        // table 내부의 paragraph가 열려있었다면 종료
        if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

        if (valid_symbols[TABLE_CELL_SPLIT]) {
          mark_end(scanner, lexer);

          // 마지막 table_cell_split의 column 위치 캐싱
          scanner->table_end_column = lexer->get_column(lexer);
          lexer->result_symbol = TABLE_CELL_SPLIT;
          return true;    
        }

        return false;
      }
    }
    // | 뒤에 |가 오지 않는 경우 - table_caption 처리
    // table_row_stack이 등록되지 않은 상태, |의 위치가 paragraph의 시작이어야 함.
    else if (!is_formatted && !get_table_row_stack(scanner) && lexer->get_column(lexer) == scanner->paragraph_end_column + 1 && valid_symbols[TABLE_CAPTION_START]) {
      mark_end(scanner, lexer);

      // table_caption_end가 나중에 등장할 수 있는지 후보를 미리 검사.
      // 단, table_row 자체가 조기 종료될 수 있으므로, table_caption_end의 등장을 보증하지는 않음.
      // 후보는 후속된 | 문자임.
      bool is_valid_table_caption_end = false;
      while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !lexer->eof(lexer)) {
        if (lexer->lookahead == '|') {
          is_valid_table_caption_end = true;
          break;
        }
        advance(lexer);
      }

      if (is_valid_table_caption_end)  {
        set_table_row_stack(scanner);
        lexer->result_symbol = TABLE_CAPTION_START;
        return true;
      }

      if (valid_symbols[PIPE]) {
        lexer->result_symbol = PIPE;
        return true;
      }

      return false;
    }

    // table 또는 link가 아닐 경우에는 일반 pipe 문자 처리
    // pipe는 1개거나 2개일 수 있음
    if (valid_symbols[PIPE]) {
      mark_end(scanner, lexer);
      lexer->result_symbol = PIPE;
      return true;
    }

    return false;
  }

  if (lexer->lookahead == '<' && (valid_symbols[TABLE_ARGUMENT_START] || valid_symbols[TABLE_SPECIAL_ARGUMENT])) {
    advance(lexer);
    mark_end(scanner, lexer);

    // -------- table_special_argument 처리 ---------------------------------------------------------

    // <(> <:> <)>
    if (lexer->lookahead == '(' || lexer->lookahead == ':' || lexer->lookahead == ')') {
      advance(lexer);
      if (lexer->lookahead == '>' && valid_symbols[TABLE_SPECIAL_ARGUMENT]) {
        advance(lexer);
        mark_end(scanner, lexer);
        lexer->result_symbol = TABLE_SPECIAL_ARGUMENT;
        return true;
      }
      return false;
    }

    // <v> <^> <v|number> <^|number>
    if (lexer->lookahead == 'v' || lexer->lookahead == '^') {
      advance(lexer);
      if (lexer->lookahead == '|') {
        advance(lexer);
        while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
          advance(lexer);
        }
        if (lexer->lookahead == '>' && valid_symbols[TABLE_SPECIAL_ARGUMENT]) {
          advance(lexer);
          mark_end(scanner, lexer);
          lexer->result_symbol = TABLE_SPECIAL_ARGUMENT;
          return true;
        }
      }
      return false;
    }

    // <|number> <-number>
    if (lexer->lookahead == '|' || lexer->lookahead == '-') {
      advance(lexer);
      while (lexer->lookahead >= '0' && lexer->lookahead <= '9') {
        advance(lexer);
      }
      if (lexer->lookahead == '>' && valid_symbols[TABLE_SPECIAL_ARGUMENT]) {
        advance(lexer);
        mark_end(scanner, lexer);
        lexer->result_symbol = TABLE_SPECIAL_ARGUMENT;
        return true;
      }
      return false;
    }

    // -------- table_argument_start 처리 ---------------------------------------------------------
    // a-z, A-Z, 공백, 쉼표, # 문자만 table_argument의 name 값으로 사용되었는지 검사.
    bool is_valid = false;
    bool is_zero_length = true;
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !lexer->eof(lexer)) {
      if (lexer->lookahead == ' ' || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z') || lexer->lookahead == ',' || lexer->lookahead == '#') {
        is_zero_length = false;
        advance(lexer);
        continue;
      }

      // table_argument_end(>)가 존재하거나, =로 table_argument의 value가 존재할 수 있다면 유효 처리.
      if (lexer->lookahead == '>' || lexer->lookahead == '=') {
        is_valid = true;
      }

      break;
    }

    // 유효하게 끝났으며, name의 길이가 1 이상일 경우 정상 처리
    if (is_valid && !is_zero_length && valid_symbols[TABLE_ARGUMENT_START]) {
      lexer->result_symbol = TABLE_ARGUMENT_START;
      return true;
    }

    return false;
  }

  // table_argument_end 처리
  if (lexer->lookahead == '>' && valid_symbols[TABLE_ARGUMENT_END]) {
    advance(lexer);
    lexer->result_symbol = TABLE_ARGUMENT_END;
    return true;
  }

  if (lexer->lookahead == ']') {
    mark_end(scanner, lexer);
    advance(lexer);

    // footnote_end 처리
    // 현재 열려 있는 주석(footnote_depth > 0)에 대한 종료 처리
    if (scanner->footnote_depth > 0) {
      if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[FOOTNOTE_END]) {
        mark_end(scanner, lexer);
        scanner->footnote_depth--;
        lexer->result_symbol = FOOTNOTE_END;
        return true;
      }
    }

    // link_end 처리 - ]]가 연속해서 올 경우
    // 현재 열려 있는 링크(link_depth > 0)에 대한 종료 처리
    if (scanner->link_depth > 0 && lexer->lookahead == ']') {
      advance(lexer);
      if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[LINK_END]) {
        mark_end(scanner, lexer);
        scanner->link_depth--;
        lexer->result_symbol = LINK_END;
        return true;
      }

      return false;
    }
    return false;
  }

  // horizontal_line 처리
  // is_horizontal_line_valid가 true인 경우에만 처리 (무한반복 방지)
  if (lexer->lookahead == '-' && lexer->get_column(lexer) == scanner->paragraph_end_column && scanner->is_horizontal_line_valid) {
    mark_end(scanner, lexer);

    // horizontal_line의 - 개수 검사
    size_t size = 0;
    while (lexer->lookahead == '-') {
      advance(lexer);
      size++;
    }

    // horizontal_line 다음은 항상 newline 또는 eof가 와야함.
    if (lexer->lookahead == '\r' || lexer->lookahead == '\n' || lexer->eof(lexer)) {
      // 4개 이상 10개 미만인 경우 horizontal_line으로 처리.
      if (size >= 4 && size < 10 && valid_symbols[HORIZONTAL_LINE]) {
        mark_end(scanner, lexer);
        lexer->result_symbol = HORIZONTAL_LINE;
        return true;
      }
      // 10개 이상인 경우 horizontal_line_implicit으로 암묵적인 처리.
      if (size >= 10 && valid_symbols[HORIZONTAL_LINE_IMPLICIT]) {
        mark_end(scanner, lexer);
        lexer->result_symbol = HORIZONTAL_LINE_IMPLICIT;
        return true;
      }
    }

    // none zero-width 익명 토큰 반환
    // 개수가 4 미만이거나, 후속 newline, eof가 오지 않아 처리에 실패할 경우
    // is_horizontal_line_valid를 false로 갱신하기 위해서 필요함. (return false를 할 경우, 갱신되지 않고 백트래킹되어 무한반복 발생)
    if (valid_symbols[NONE]) {
      scanner->is_horizontal_line_valid = false;
      lexer->result_symbol = NONE;
      return true;
    }

    return false;
  }

  // -------- format류 문법 처리 시작 ---------------------------------------------------------

  if (lexer->lookahead == '\'') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "''")) {
      // bold 처리
      if (lexer->lookahead == '\'' && consume_string(lexer, "'")) {
        return consume_format(lexer, scanner, valid_symbols, FLAG_BOLD, BOLD_START);
      }
      // italic 처리 
      else {
        return consume_format(lexer, scanner, valid_symbols, FLAG_ITALIC, ITALIC_START);
      }
    }
    return false;
  }
  
  // strike 처리
  if (lexer->lookahead == '-') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "--")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_STRIKE, STRIKE_START);
    }
    return false;
  }

  // strike2 처리
  if (lexer->lookahead == '~') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "~~")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_STRIKE2, STRIKE2_START);
    }
    return false;
  }

  // superscript 처리
  if (lexer->lookahead == '^') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "^^")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_SUPERSCRIPT, SUPERSCRIPT_START);
    }
    return false;
  }

  // subscript 처리
  if (lexer->lookahead == ',') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, ",,")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_SUBSCRIPT, SUBSCRIPT_START);
    }
    return false;
  }

  // underline 처리
  if (lexer->lookahead == '_') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "__")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_UNDERLINE, UNDERLINE_START);
    }
    return false;
  }

  // -------- format류 문법 처리 종료 ---------------------------------------------------------

  // newline 처리
  // 줄바꿈과 함께, 이번 문단과 다음 문단이 이어지는지, heading의 위계가 어떻게 달라지는지를 종합적으로 처리함.
  if ((lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
    // 줄바꿈이 나올 경우 정상적으로 table_row_end 처리
    // table_row_end 로직상 마지막 table_row_column의 위치가 현 줄의 끝임을 검증해야 함.
    // \r?\n consume 이후 lexer->get_column(lexer)가 처음으로 초기화되기에 consume 전 미리 검사.
    if (register_table_row_end(lexer, scanner, valid_symbols, 0)) return true;

    // zero width 토큰을 생성하기 위한 mark_end
    mark_end(scanner, lexer);

    // \r?\n 까지 처리
    if (lexer->lookahead == '\r') {
      advance(lexer);
    }
    advance(lexer);

    // 내부에 줄바꿈을 허용하지 않는 문법 중 열려있는 문법 암묵적 종료
    if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_caption_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;

    // soft_line_prefix가 발뒤되었다면 soft_line_ending이 유효함
    if (valid_symbols[SOFT_LINE_ENDING]) {
      // 줄이 다음 줄로 갱신되면서, 줄에서 관리되는 상태를 처음으로 초기화.
      if (scanner->cached_heading_token == NO_HEADING_TOKEN) {
        scanner->cached_heading_token = NO_CACHED;
      }
      scanner->is_horizontal_line_valid = true;
      scanner->table_end_column = UINT32_MAX;
      scanner->paragraph_skipped_index = 0;
      scanner->paragraph_end_column = 0;

      mark_end(scanner, lexer);
      lexer->result_symbol = SOFT_LINE_ENDING;
      return true;
    }

    // 다음 줄을 처음 글자부터 미리 검사해 다음 문단의 유형을 얻음.
    if (lexer->get_column(lexer) == 0) {
      // =로 시작하지 않는다면 다음 줄은 heading이 아님.
      if (lexer->lookahead != '=') {
        scanner->cached_heading_token = NO_HEADING_TOKEN;
      }

      // 공백 또는 >로 시작할 경우, 다음 줄이 이번 줄의 문단과 이어질 수 있음.
      if (lexer->lookahead == ' ' || lexer->lookahead == '>') {
        uint8_t type = PARAGRAPH_TYPE_FALLBACK; // 현재 순회에서 검사할 paragraph_type
        bool is_valid = true; // 다음 줄과 이번 줄 문단이 이어진다면 true

        // 현재 paragraph_depth에서, 첫번째 paragraph_type부터 마지막까지 검사.
        for (size_t i = paragraph_type_stack_first_index; i < scanner->paragraph_type_stack_size; i++) {
          type = get_paragraph_type(scanner, i);

          // 일반 문단 또는 리스트 문단일 경우, 다음 줄에서는 공백과 이어질 수 있음.
          if ((type == PARAGRAPH_TYPE_PARAGRAPH || type == PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH || type == PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH) && lexer->lookahead == ' ') {
            advance(lexer);

            // 만약 " *"로 이어질 경우, 새로운 리스트 문단이 시작하기 때문에 이어지지 않음.
            if (lexer->lookahead == '*') {
              is_valid = false;
              break;
            }

            // 만약 " (1|a|A|i|I)"로 이어질 경우
            if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
              advance(lexer);

              // 이번 타입이 순회의 마지막이라면, 1|a|A|i|I는 paragraph의 값으로 처리되어 이어짐.
              // 하지만 검사할 타입이 더 남아있다면, 공백 또는 >로 이어지는 조건이 아니기에 이어지지 않음.
              if (i < scanner->paragraph_type_stack_size - 1) {
                is_valid = false;
                break;
              }

              // 만약 " (1|a|A|i|I)."로 이어질 경우, 새로운 리스트 문단이 시작하기 때문에 이어지지 않음.
              if (lexer->lookahead == '.') {
                is_valid = false;
                break;
              }
            }

            continue;
          }

          // 인용문 문단일 경우, 다음 줄에서는 >와 이어짐.
          if (type == PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH && lexer->lookahead == '>') {
            advance(lexer);
            continue;
          }

          // 조건과 부히하지 않을 경우 이어지지 않음.
          is_valid = false;
          break;
        }

        // 다음 줄과 이번 줄이 이어질 경우
        if (is_valid) {
          // | 이후 |가 나오지 않거나, 다른 문자인 경우 후속 table_end 처리
          // table_caption으로 시작되는 경우도 있지만, 이는 table의 첫 번째 table_row에서만 유효하기 때문에 그 경우 역시 table_end
          if (lexer->lookahead == '|') {
            advance(lexer);
            if (lexer->lookahead != '|' && register_table_end(lexer, scanner, valid_symbols)) return true;
          } else {
            if (register_table_end(lexer, scanner, valid_symbols)) return true;
          }
        }
        // 다음 줄과 이번 줄이 이어지지 않을 경우
        else {
          // table_end 및 paragraph_end 처리
          if (register_table_end(lexer, scanner, valid_symbols)) return true;
          if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;
        }
      }
      // 다음 줄은 이번 줄 문단과 이어지지 않음.
      else {
        // 만약 이번 줄에 문단으로 시작했다면 (즉, indent 또는 >가 있었다면) 다음 줄과 이번 줄은 이어지지 않음.
        if (scanner->paragraph_stack[paragraph_depth] > 0) {
          // table_end 처리
          if (register_table_end(lexer, scanner, valid_symbols)) return true;
        }
        // 만약 이번 줄에 문단이 존재하지 않는 경우
        else {
          // | 이후 |가 나오지 않거나, 다른 문자인 경우 후속 table_end 처리
          // table_caption으로 시작되는 경우도 있지만, 이는 table의 첫 번째 table_row에서만 유효하기 때문에 그 경우 역시 table_end
          if (lexer->lookahead == '|') {
            advance(lexer);
            if (lexer->lookahead != '|' && register_table_end(lexer, scanner, valid_symbols)) return true;
          } else {
            if (register_table_end(lexer, scanner, valid_symbols)) return true;
          }
        }

        // paragraph_end 처리
        if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;
      }
    }

    // 캐싱된 heading_token이 없다면 heading 토큰 얻기
    if (scanner->cached_heading_token == NO_CACHED) {
      scanner->cached_heading_token = match_heading_token(lexer, scanner, top_type, true);
    }
    
    // 만약 다음 줄이 heading일 경우
    if (scanner->cached_heading_token != NO_HEADING_TOKEN) {
      // 다음 줄의 heading level 확인
      uint8_t heading_level_offset = scanner->cached_heading_token - HEADING1_START + 1;
      uint8_t heading_level = heading_level_offset > 6 ? heading_level_offset - 6 : heading_level_offset;

      // table_row_implicit_end와 table_end 처리
      if (register_table_row_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_table_end(lexer, scanner, valid_symbols)) return true;

      // -------- heading 위계 처리 ---------------------------------------------------------
      // heading은 line_ending 토큰으로 종료되는데, 상위 단계 문단이 하위 단계 문단을 포함하는 위계가 존재함.
      // 따라서, 상위 단계에서 하위 단계를 포함할 경우 soft_line_ending을 발행함.
      // 단계는 작을 수록 더 상위 단계임.

      // 이번 heading이 다음 heading의 상위 단계일 경우 soft_line_prefix 발행 (이후 상단 soft_line_ending이 발행됨.)
      // e.g. 이번 heading(2단계) >>> 다음 heading(3단계)
      if (scanner->heading_level < heading_level && scanner->heading_level != STATUS_NO_HEADING_SECTION && valid_symbols[SOFT_LINE_PREFIX]) {
        lexer->result_symbol = SOFT_LINE_PREFIX;
        return true;
      }

      // 반대로 다음 heading이 이번 heading보다 상위 또는 동등 단계일 경우, 상위 또는 동등 단계를 포함할 수 없어 line_ending 발행
      // e.g. 이번 heading(3단계) <<< 다음 heading(2단계)
      if (valid_symbols[LINE_ENDING]) {
        // heading_levels에서 다음 heading 단계의 하위 단계는 모두 bitmask 초기화
        // 다음 heading 단계에 해당하는 단계는 1 증가
        scanner->heading_levels &= ~(1 << (scanner->heading_level - 1)); 
        for (int i = 5; i >= 0; i--) {
          if (scanner->heading_levels & (1 << i)) {
            scanner->heading_level = i + 1;
            break;
          }
        }

        lexer->result_symbol = LINE_ENDING;
        return true;
      }
    }

    // soft_line_prefix 먼저 발행 후, 상단 soft_line_ending 발행됨.
    if (valid_symbols[SOFT_LINE_PREFIX]) {
      lexer->result_symbol = SOFT_LINE_PREFIX;
      return true;
    }

    return false;
  }

  /*
  이번 문단과 다음 문단이 이어지는 부분은 현재 깊이의 문단 타입을 순회하며 스킵 처리.
  위의 newline 처리의 다음 문단과 이번 문단의 이어짐 여부 판단을 위한 반복문 순회의 로직과 유사.
  단일 조건문 로직이지만, 공백과 > 문자마다 external scanner가 발동되므로 사실상 반복문임.

  현재 index는 paragraph_type_stack_first_index + scanner->paragraph_skipped_index이며,
  scanner->paragraph_type_stack_size에 도달하거나, 공백과 > 문자가 오지 않거나,
  lexer->get_column(lexer) == scanner->paragraph_end_column이 아니라면 반복은 멈춤.
  */
  if (
    (lexer->lookahead == ' ' || lexer->lookahead == '>') &&
    lexer->get_column(lexer) == scanner->paragraph_end_column &&
    paragraph_type_stack_first_index + scanner->paragraph_skipped_index < scanner->paragraph_type_stack_size &&
    valid_symbols[PARAGRAPH_SKIP]
  ) {
    // 현재 깊이의 문단 타입 순회 중 현재 처리할 타입
    uint8_t type = get_paragraph_type(scanner, paragraph_type_stack_first_index + scanner->paragraph_skipped_index);
    
    // 현재 처리할 타입이 일반 문단 또는 리스트 문단일 경우, 스킵 가능할 수 있음.
    if ((type == PARAGRAPH_TYPE_PARAGRAPH || type == PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH || type == PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH) && lexer->lookahead == ' ') {
      advance(lexer);
      mark_end(scanner, lexer);

      // " *"로 시작할 경우, 새 리스트의 시작이므로 스킵 불가
      if (lexer->lookahead == '*') {
        return false;
      }

      if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
        advance(lexer);

        // " (1|a|A|i|I)."로 시작할 경우, 새 리스트의 시작이므로 스킵 불가
        if (lexer->lookahead == '.') {
          return false;
        }

        // 후행으로 .가 오지 않는 리스트의 시작이 아니라면 다음 스킵 순회를 위해 값을 갱신.
        scanner->paragraph_skipped_index++;
        scanner->paragraph_end_column = lexer->get_column(lexer) - 1;
        lexer->result_symbol = PARAGRAPH_SKIP;
        return true;
      }

      // 리스트의 시작이 아니라면 다음 스킵 순회를 위해 값을 갱신.
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_SKIP;
      return true;
    }

    // 현재 처리할 타입이 인용문일 경우, 스킵 가능.
    if (type == PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH && lexer->lookahead == '>') {
      advance(lexer);

      // 다음 스킵 순회를 위해 값을 갱신.
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_SKIP;
      return true;
    }

    return false;
  }

  /*
  새로운 일반 문단 또는 리스트의 시작.
  */
  if (
    lexer->lookahead == ' ' &&
    (valid_symbols[PARAGRAPH_START] || valid_symbols[UNORDERED_LIST_PARAGRAPH_START] || valid_symbols[ORDERED_LIST_PARAGRAPH_START]) &&
    lexer->get_column(lexer) == scanner->paragraph_end_column &&
    paragraph_type_stack_first_index + scanner->paragraph_skipped_index >= scanner->paragraph_type_stack_size
  ) {
    advance(lexer);
    mark_end(scanner, lexer);

    // " *"로 시작할 경우 unordered_list
    if (lexer->lookahead == '*' && valid_symbols[UNORDERED_LIST_PARAGRAPH_START]) {
      advance(lexer);

      // 뒤에 공백이 있다면, 이는 추가적인 일반 문단이 아닌 unordered_list_paragraph_start로 처리.
      if (lexer->lookahead == ' ') {
        advance(lexer);
      }

      if (push_paragraph(scanner, PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH)) {
        scanner->paragraph_skipped_index++;
        scanner->paragraph_end_column = lexer->get_column(lexer);
        lexer->result_symbol = UNORDERED_LIST_PARAGRAPH_START;
        return true;
      }

      return false;
    }

    // " (1|a|A|i|I)"로 시작할 경우
    if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
      advance(lexer);

      // " (1|a|A|i|I)."로 시작할 경우 ordered_list
      if (lexer->lookahead == '.' && valid_symbols[ORDERED_LIST_PARAGRAPH_START]) {
        advance(lexer);

        // 이후 공백 여부를 처리할 수 있도록, " (1|a|A|i|I).#(0-9|a-z|A-Z)" 형식을 미리 consume
        if (lexer->lookahead == '#') {
          advance(lexer);
          if (('0' <= lexer->lookahead && lexer->lookahead <= '9') || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z')) {
            while (('0' <= lexer->lookahead && lexer->lookahead <= '9') || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z')) {
              advance(lexer);
            }
          }
        }

        // 뒤에 공백이 있다면, 일반 문단이 아닌 ordered_list_paragraph_start로 처리
        if (lexer->lookahead == ' ') {
          advance(lexer);
        }

        if (push_paragraph(scanner, PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH)) {
          scanner->paragraph_skipped_index++;
          scanner->paragraph_end_column = lexer->get_column(lexer);
          lexer->result_symbol = ORDERED_LIST_PARAGRAPH_START;
          return true;
        }

        return false;
      }

      // ordered_list가 아니라면, 일반 문단으로 처리
      if (push_paragraph(scanner, PARAGRAPH_TYPE_PARAGRAPH)) {
        scanner->paragraph_skipped_index++;

        // 단, 이미 먼저 소비된 (1|a|A|i|I) 문자는 일반 문단에서는 원칙적으로 처리되지 않으므로 end_column에서 1 감소
        scanner->paragraph_end_column = lexer->get_column(lexer) - 1;

        lexer->result_symbol = PARAGRAPH_START;
        return true;
      }

      return false;
    }

    // 모두 아니라면, 일반 문단 처리.
    if (valid_symbols[PARAGRAPH_START] && push_paragraph(scanner, PARAGRAPH_TYPE_PARAGRAPH)) {
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_START;
      return true;
    }
    
    return false;
  }

  /*
  새로운 인용문 문단의 시작.
  */
  if (
    lexer->lookahead == '>' &&
    valid_symbols[BLOCKQUOTE_PARAGRAPH_START] &&
    lexer->get_column(lexer) == scanner->paragraph_end_column &&
    paragraph_type_stack_first_index + scanner->paragraph_skipped_index >= scanner->paragraph_type_stack_size
  ) {
    advance(lexer);
    mark_end(scanner, lexer);

    if (push_paragraph(scanner, PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH)) {
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = BLOCKQUOTE_PARAGRAPH_START;
      return true;
    }

    return false;
  }

  // 스킵 또는 문단 발행이 되지 않아, 공백 문자가 처리되지 않았을 경우 space 토큰 발행으로 fallback.
  if (lexer->lookahead == ' ' && valid_symbols[SPACE]) {
    advance(lexer);
    lexer->result_symbol = SPACE;
    return true;
  }

  // 스킵 또는 문단 발행이 되지 않아, > 문자가 처리되지 않았을 경우 greater_than_sign 토큰 발행으로 fallback.
  if (lexer->lookahead == '>' && valid_symbols[GREATER_THAN_SIGN]) {
    advance(lexer);
    lexer->result_symbol = GREATER_THAN_SIGN;
    return true;
  }

  // argument_end 처리
  // 열려 있는 argument(scanner->in_argument == true)를 종료.
  if (scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_END]) {
    advance(lexer);
    scanner->in_argument = false;
    lexer->result_symbol = ARGUMENT_END;
    return true;
  }

  // heading_start 처리
  // 줄의 시작 문자로 =로 시작하며, heading이 현재 매칭 중이 아닐 때
  if (scanner->heading_status != STATUS_MATCH_HEADING && lexer->get_column(lexer) == 0 && lexer->lookahead == '=') {
    // 캐싱된 heading_token이 존재할 경우
    if (scanner->cached_heading_token != NO_HEADING_TOKEN && scanner->cached_heading_token != NO_CACHED && valid_symbols[scanner->cached_heading_token]) {
      while (lexer->lookahead != ' ') {
        advance(lexer);
      }
      advance(lexer);
  
      uint8_t offset = scanner->cached_heading_token - HEADING1_START + 1;
          
      scanner->heading_level = offset > 6 ? offset - 6 : offset;
      scanner->heading_status = STATUS_MATCH_HEADING;
      scanner->heading_levels |= 1 << (scanner->heading_level - 1);
      scanner->is_hidden_heading = offset > 6;
      lexer->result_symbol = scanner->cached_heading_token;
      scanner->cached_heading_token = NO_CACHED;
      
      return true;
    }
  
    // 캐싱된 heading_token이 존재하지 않을 경우
    uint8_t heading_token = match_heading_token(lexer, scanner, top_type, false);
    if (heading_token != NO_HEADING_TOKEN) {
      if (valid_symbols[heading_token]) {
        uint8_t offset = heading_token - HEADING1_START + 1;
        
        scanner->heading_level = offset > 6 ? offset - 6 : offset;
        scanner->heading_status = STATUS_MATCH_HEADING;
        scanner->heading_levels |= 1 << (scanner->heading_level - 1);
        scanner->is_hidden_heading = offset > 6;
        lexer->result_symbol = heading_token;
        
        return true;
      }
    }

    return false;
  }

  // 문자가 공백이며, 현재 heading이 매칭 중인 경우
  if (scanner->heading_status == STATUS_MATCH_HEADING && lexer->lookahead == ' ') {
    mark_end(scanner, lexer);
    advance(lexer);

    // 다음 문자가 # 또는 =가 아니면 heading_end 매칭이 아님.
    if (lexer->lookahead != '#' && lexer->lookahead != '=') return false;

    char heading_end_string[100];
    const char *heading_prefix = scanner->is_hidden_heading ? "#" : "";
    strncpy(heading_end_string, heading_prefix, sizeof(heading_end_string) - 1);
    heading_end_string[sizeof(heading_end_string) - 1] = '\0';
    
    int len = strlen(heading_end_string);
    memset(heading_end_string + len, '=', scanner->heading_level);
    heading_end_string[len + scanner->heading_level] = '\0';

    // 현재 매칭되고 있는 heading에 해당하는 종료 문자열이 후속으로 나타나는 경우, heading_end 처리
    // e.g. ===#로 시작된 heading이라면, #===의 종료 문자열을 체크함.
    if (consume_string(lexer, heading_end_string) && (lexer->lookahead == '\r' || lexer->lookahead == '\n' || lexer->eof(lexer))) {
      // heading 내부의 inline으로 들어가는 문법들을 강제 종료.
      if (register_bracket_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_macro_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      
      if (valid_symbols[HEADING_END]) {
        mark_end(scanner, lexer);

        scanner->heading_status = STATUS_HEADING_SECTION;
        lexer->result_symbol = HEADING_END;
        return true;
      }
    }
    return false;
  }

  // block류 매칭
  if (lexer->lookahead == '{') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "{{{")) {
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      
      mark_end(scanner, lexer);
      // 현재 nowiki_content 내부일 경우, 중첩된 bracket은 nowiki_inner_start로 처리.
      // e.g. {{{ {{{ }}}일 경우, 내부 {{{ }}}가 짝을 맞추며, 외부의 {{{는 implicit_end로 처리됨.
      if (top_type == BLOCK_TYPE_NOWIKI) {
        return register_block(lexer, scanner, valid_symbols, NOWIKI_INNER_START, BLOCK_TYPE_NOWIKI);
      }

      if (lexer->lookahead == '#') {
        advance(lexer);

        // {{{#! (shebang) 구문 처리.
        if (lexer->lookahead == '!') {
          advance(lexer);

          // {{{#!if
          if (consume_string(lexer, "if")) {
            return consume_depth_block(lexer, scanner, valid_symbols, IF_BLOCK_START, BLOCK_TYPE_IF, true);
          }
          // {{{#!folding
          if (consume_string(lexer, "folding")) {
            return consume_depth_block(lexer, scanner, valid_symbols, FOLDING_BLOCK_START, BLOCK_TYPE_FOLDING, false);
          }
          if (consume_string(lexer, "s")) {
            // {{{#!syntax
            if (consume_string(lexer, "yntax")) {
              return consume_syntax_block(lexer, scanner, valid_symbols);
            }
            // {{{#!style
            if (consume_string(lexer, "tyle")) {
              // 후속 줄바꿈이 없으면 fallback
              if (!consume_newline(lexer)) {
                return register_block(lexer, scanner, valid_symbols, STYLE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
              }
              return register_block(lexer, scanner, valid_symbols, STYLE_BLOCK_START, BLOCK_TYPE_STYLE);
            }
          }
          // {{{#!wiki
          if (consume_string(lexer, "wiki")) {
            return consume_depth_block(lexer, scanner, valid_symbols, WIKI_BLOCK_START, BLOCK_TYPE_WIKI, false);
          }
          // {{{#!html
          if (consume_string(lexer, "html")) {
            return register_block(lexer, scanner, valid_symbols, HTML_BLOCK_START, BLOCK_TYPE_HTML);
          }
          // {{{#!latex
          if (consume_string(lexer, "latex")) {
            // 후속 줄바꿈이 없으면 fallback
            if (!consume_newline(lexer)) {
                return register_block(lexer, scanner, valid_symbols, LATEX_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
              }
              return register_block(lexer, scanner, valid_symbols, LATEX_BLOCK_START, BLOCK_TYPE_LATEX);
          }
        } 
        // color_block_start 처리
        else {
          return consume_color_block(lexer, scanner, valid_symbols);
        }
      }

      // scale_block_start 처리
      if (lexer->lookahead == '+' || lexer->lookahead == '-') {
        advance(lexer);
        return consume_scale_block(lexer, scanner, valid_symbols);
      }

      // 모두 아닐 경우 nowiki_start 처리
      return register_block(lexer, scanner, valid_symbols, NOWIKI_START, BLOCK_TYPE_NOWIKI);
    }
    return false;
  }

  if (lexer->lookahead == '[') {
    mark_end(scanner, lexer);
    advance(lexer);
    // [[로 시작된 link_start 또는 file_link_start 처리
    if (lexer->lookahead == '[') {
      advance(lexer);
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if ((valid_symbols[LINK_START] || valid_symbols[FILE_LINK_START]) &&
          can_increment_format_depth(scanner)) {
        mark_end(scanner, lexer);
        scanner->link_depth++;
        reset_format(scanner);

        // [[파일:로 시작하면 file_link_start이며, 아니면 link_start임
        lexer->result_symbol = consume_string(lexer, "파일:") ? FILE_LINK_START : LINK_START;
        return true;
      }

      return false;
    }

    if (!(scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END])) {
      mark_end(scanner, lexer);
    }

    // [*로 시작된 footnote_start 처리
    if (lexer->lookahead == '*') {
      advance(lexer);
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[FOOTNOTE_START] && can_increment_format_depth(scanner)) {
        mark_end(scanner, lexer);
        scanner->footnote_depth++;
        reset_format(scanner);

        lexer->result_symbol = FOOTNOTE_START;
        return true;
      }
      return false;
    }

    // [(macro_name)(로 시작된 macro_start 처리
    if (consume_macro(lexer) && lexer->lookahead == '(') {
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[MACRO_START]) {
        scanner->in_macro = true;
        lexer->result_symbol = MACRO_START;
        return true;
      }
    }

    return false;
  }

  // argument_start 처리
  // 열려있는 argument가 없는 경우(scanner->in_argument == false) 새로운 argument 시작
  if (!scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_START] && consume_argument(lexer, scanner)) {
    scanner->in_argument = true;
    lexer->result_symbol = ARGUMENT_START;
    return true;
  }

  return false;
}

bool tree_sitter_namumark_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  return scan(payload, lexer, valid_symbols);
}