#include "tree_sitter/parser.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wctype.h>
#include <string.h>
#include <stdio.h>
#include "unicode_ranges.h"

#define NO_HEADING_TOKEN 0

#define STATUS_NO_HEADING_SECTION 0
#define STATUS_MATCH_HEADING 1
#define STATUS_HEADING_SECTION 2

#define NO_CACHED 255

#define FLAG_NONE        0
#define FLAG_BOLD        (1 << 0) // 0x01 (0000 0001)
#define FLAG_ITALIC      (1 << 1) // 0x02 (0000 0010)
#define FLAG_STRIKE      (1 << 2) // 0x04 (0000 0100)
#define FLAG_STRIKE2     (1 << 3) // 0x08 (0000 1000)
#define FLAG_SUPERSCRIPT (1 << 4) // 0x10 (0001 0000)
#define FLAG_SUBSCRIPT   (1 << 5) // 0x20 (0010 0000)
#define FLAG_UNDERLINE   (1 << 6) // 0x40 (0100 0000)

#define FLAG_SIMULATE_HEADING   (1 << 0) // 0x01 (0000 0001) 

#define MAX_PARAGRAPH_COUNT 257
#define PARAGRAPH_TYPE_STACK_SIZE ((MAX_PARAGRAPH_COUNT + 3) / 4)
#define PARAGRAPH_TYPE_STACK_CAPACITY (PARAGRAPH_TYPE_STACK_SIZE * 4)

#define PARAGRAPH_TYPE_FALLBACK 255

static bool scan(void *payload, TSLexer *lexer, const bool *valid_symbols);

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
  TOKEN_EOF
};

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

enum ParagraphType {
  PARAGRAPH_TYPE_PARAGRAPH,
  PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH,
  PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH,
  PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH
};

typedef struct {
  bool in_macro;
  bool in_argument;
  bool is_horizontal_line_valid;
  uint8_t heading_level;
  uint8_t heading_status;
  uint8_t heading_levels;
  bool is_hidden_heading;
  uint8_t cached_heading_token;
  uint32_t link_depth;
  uint32_t footnote_depth;
  uint32_t block_depth;
  uint32_t table_end_column;
  uint16_t paragraph_type_stack_size;
  uint16_t paragraph_skipped_index;
  uint32_t paragraph_end_column;
  uint8_t simulate;
  uint8_t block_stack[256]; // block_depth와 연동
  uint8_t table_stack[32]; // (block_depth / 8)와 연동
  uint8_t format_stack[256]; // block_depth + footnote_depth + link_depth와 연동
  uint8_t paragraph_stack[MAX_PARAGRAPH_COUNT]; // block_depth or block_depth + table과 연동
  uint8_t paragraph_type_stack[PARAGRAPH_TYPE_STACK_SIZE];
} Scanner;

enum {
  SERIALIZED_HEADER_SIZE = 24,
  FORMAT_STACK_SIZE = 256,
  MAX_BLOCK_DEPTH = UINT8_MAX
};

// --- Helper 함수들 ---

static inline void set_table_stack(Scanner* s) {
  if (s->block_depth <= MAX_BLOCK_DEPTH && s->block_depth / 8 < sizeof s->table_stack)
    s->table_stack[s->block_depth / 8] |= (uint8_t)(1u << (s->block_depth % 8));
}

static inline void clear_table_stack(Scanner* s) {
  if (s->block_depth <= MAX_BLOCK_DEPTH && s->block_depth / 8 < sizeof s->table_stack)
    s->table_stack[s->block_depth / 8] &= (uint8_t)~(1u << (s->block_depth % 8));
}

static inline bool get_table_stack(const Scanner* s) {
  return s->block_depth <= MAX_BLOCK_DEPTH &&
    s->block_depth / 8 < sizeof s->table_stack &&
    (s->table_stack[s->block_depth / 8] & (1u << (s->block_depth % 8))) != 0;
}

static bool is_valid_paragraph_type_stack(const uint8_t *stack, uint16_t count) {
  if (count > PARAGRAPH_TYPE_STACK_CAPACITY) return false;

  for (uint16_t i = 0; i < count; i++) {
    uint8_t type = (stack[i / 4] >> ((i % 4) * 2)) & 0x03u;
    if (type > PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH) return false;
  }
  return true;
}

static bool is_valid_paragraph_state(const uint8_t *paragraph_stack, uint32_t paragraph_count, uint16_t paragraph_type_stack_size) {
  if (paragraph_count > MAX_PARAGRAPH_COUNT) return false;

  uint32_t total = 0;
  for (uint32_t i = 0; i < paragraph_count; i++) {
    total += paragraph_stack[i];
    if (total > paragraph_type_stack_size) return false;
  }
  return total == paragraph_type_stack_size;
}

static bool push_paragraph(Scanner* scanner, uint8_t type) {
  uint16_t paragraph_depth = scanner->block_depth + get_table_stack(scanner);

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
  uint16_t paragraph_depth = scanner->block_depth + get_table_stack(scanner);
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

static uint8_t get_top_paragraph_type(Scanner *scanner) {
  return get_paragraph_type(scanner, scanner->paragraph_type_stack_size - 1);
}

static inline bool get_format_index(const Scanner *scanner, uint32_t *index) {
  if (scanner->block_depth >= FORMAT_STACK_SIZE 
    || scanner->link_depth >= FORMAT_STACK_SIZE - scanner->block_depth 
    || scanner->footnote_depth >= FORMAT_STACK_SIZE - scanner->block_depth - scanner->link_depth) {
    return false;
  }
  *index = scanner->block_depth + scanner->link_depth + scanner->footnote_depth;
  return true;
}

static inline bool can_increment_format_depth(const Scanner *scanner) {
  uint32_t index;
  return get_format_index(scanner, &index) && index < FORMAT_STACK_SIZE - 1;
}

static inline bool is_valid_cached_heading_token(uint8_t token) {
  return token == NO_HEADING_TOKEN || token == NO_CACHED ||
    (token >= HEADING1_START && token <= HIDDEN_HEADING6_START);
}

static inline bool is_valid_block_type(uint8_t type) {
  return type > BLOCK_TYPE_NONE && type <= BLOCK_TYPE_FALLBACK;
}

static inline void set_style(Scanner *scanner, uint32_t depth, uint8_t flag) {
    scanner->format_stack[depth] |= flag;
}

static inline void set_current_style(Scanner *scanner, uint8_t flag) {
  uint32_t index;
  if (get_format_index(scanner, &index)) set_style(scanner, index, flag);
}

static inline void reset_current_format_scope(Scanner *scanner) {
  uint32_t index;
  if (get_format_index(scanner, &index)) {
    scanner->format_stack[index] = 0;
  }
}

static inline void clear_style(Scanner *scanner, uint32_t depth, uint8_t flag) {
    scanner->format_stack[depth] &= ~flag;
}

static inline void clear_current_style(Scanner *scanner, uint8_t flag) {
  uint32_t index;
  if (get_format_index(scanner, &index)) clear_style(scanner, index, flag);
}

static inline bool has_style(const Scanner *scanner, uint32_t depth, uint8_t flag) {
    return (scanner->format_stack[depth] & flag) != 0;
}

static inline bool has_current_style(const Scanner *scanner, uint8_t flag) {
  uint32_t index;
  return get_format_index(scanner, &index) && has_style(scanner, index, flag);
}

static void mark_end(Scanner *s, TSLexer *lexer) {
    if (s->simulate == FLAG_NONE) {
      lexer->mark_end(lexer);
    }
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
      reset_current_format_scope(scanner);
    } else {
      return false;
    }
    lexer->result_symbol = TOKEN_TYPE;
    return true;
  }

  if (BLOCK_TYPE != BLOCK_TYPE_FALLBACK && valid_symbols[TOKEN_TYPE + 1]) {
    if (scanner->block_depth < MAX_BLOCK_DEPTH && can_increment_format_depth(scanner)) {
      scanner->block_stack[scanner->block_depth++] = BLOCK_TYPE_FALLBACK;
      reset_current_format_scope(scanner);
    } else {
      return false;
    }
    lexer->result_symbol = TOKEN_TYPE + 1;
    return true;
  }

  return false;
}

static uint8_t match_heading_token(TSLexer *lexer, Scanner *scanner) {
  if (lexer->get_column(lexer) != 0) return NO_HEADING_TOKEN;
  if (lexer->lookahead != '=') return NO_HEADING_TOKEN;

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

  mark_end(scanner, lexer);

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

  if (scanner->cached_heading_token == NO_CACHED) {
    scanner->simulate = FLAG_SIMULATE_HEADING;
    scanner->cached_heading_token = match_heading_token(lexer, scanner);
    scanner->simulate = FLAG_NONE;
  }
  if (scanner->cached_heading_token != NO_HEADING_TOKEN) {
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
  } else {
    if (scanner->cached_heading_token == NO_CACHED) {
      scanner->simulate = FLAG_SIMULATE_HEADING;
      scanner->cached_heading_token = match_heading_token(lexer, scanner);
      scanner->simulate = FLAG_NONE;
    }
    if (scanner->cached_heading_token != NO_HEADING_TOKEN) {
      return register_block(lexer, scanner, valid_symbols, SCALE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
    }
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
  static const char *const VALID_COLORS[] = {"aliceblue", "antiquewhite", "aqua", "aquamarine", "azure", "beige", "bisque", "black", "blanchedalmond", "blue", "blueviolet", "brown", "burlywood", "cadetblue", "chartreuse", "chocolate", "coral", "cornflowerblue", "cornsilk", "crimson", "cyan", "darkblue", "darkcyan", "darkgoldenrod", "darkgray", "darkgrey", "darkgreen", "darkkhaki", "darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred", "darksalmon", "darkseagreen", "darkslateblue", "darkslategray", "darkslategrey", "darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgray", "dimgrey", "dodgerblue", "firebrick", "floralwhite", "forestgreen", "fuchsia", "gainsboro", "ghostwhite", "gold", "goldenrod", "gray", "grey", "green", "greenyellow", "honeydew", "hotpink", "indianred", "indigo", "ivory", "khaki", "lavender", "lavenderblush", "lawngreen", "lemonchiffon", "lightblue", "lightcoral", "lightcyan", "lightgoldenrodyellow", "lightgray", "lightgrey", "lightgreen", "lightpink", "lightsalmon", "lightseagreen", "lightskyblue", "lightslategray", "lightslategrey", "lightsteelblue", "lightyellow", "lime", "limegreen", "linen", "magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid", "mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen", "mediumturquoise", "mediumvioletred", "midnightblue", "mintcream", "mistyrose", "moccasin", "navajowhite", "navy", "oldlace", "olive", "olivedrab", "orange", "orangered", "orchid", "palegoldenrod", "palegreen", "paleturquoise", "palevioletred", "papayawhip", "peachpuff", "peru", "pink", "plum", "powderblue", "purple", "rebeccapurple", "red", "rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen", "seashell", "sienna", "silver", "skyblue", "slateblue", "slategray", "slategrey", "snow", "springgreen", "steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet", "wheat", "white", "whitesmoke", "yellow", "yellowgreen"};

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
  } else {
    if (scanner->cached_heading_token == NO_CACHED) {
      scanner->simulate = FLAG_SIMULATE_HEADING;
      scanner->cached_heading_token = match_heading_token(lexer, scanner);
      scanner->simulate = FLAG_NONE;
    }
    if (scanner->cached_heading_token != NO_HEADING_TOKEN) {
      return register_block(lexer, scanner, valid_symbols, COLOR_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
    }
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

static bool register_table_row_implicit_end(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols) {
  if (valid_symbols[TABLE_ROW_IMPLICIT_END]) {
    clear_table_stack(scanner);
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
  if (has_current_style(scanner, FLAG) && valid_symbols[TOKEN_TYPE]) {
    clear_current_style(scanner, FLAG);
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

static bool consume_format(TSLexer *lexer, Scanner *scanner, const bool *valid_symbols, uint8_t FLAG, uint8_t START_TOKEN_TYPE) {
  if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
        
  bool is_formatted = has_current_style(scanner, FLAG);
  if (!is_formatted && valid_symbols[START_TOKEN_TYPE]) {
    mark_end(scanner, lexer);
    set_current_style(scanner, FLAG);
    lexer->result_symbol = START_TOKEN_TYPE;
    return true;
  }

  uint8_t END_TOKEN_TYPE = START_TOKEN_TYPE + 1;
  if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG)) return true;
  if (is_formatted && valid_symbols[END_TOKEN_TYPE]) {
    mark_end(scanner, lexer);
    clear_current_style(scanner, FLAG);
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
      !get_format_index(scanner, &format_count)) return 0;
  format_count++;

  uint32_t table_count = scanner->block_depth / 8 + 1;
  uint32_t paragraph_count = scanner->block_depth + get_table_stack(scanner) + 1;
  size_t total_size = SERIALIZED_HEADER_SIZE + scanner->block_depth + table_count +
    paragraph_count + sizeof scanner->paragraph_type_stack + format_count;
  if (total_size > TREE_SITTER_SERIALIZATION_BUFFER_SIZE ||
      !is_valid_paragraph_state(scanner->paragraph_stack, paragraph_count, scanner->paragraph_type_stack_size) ||
      !is_valid_paragraph_type_stack(scanner->paragraph_type_stack, scanner->paragraph_type_stack_size) ||
      scanner->heading_level > 6 ||
      scanner->heading_status > STATUS_HEADING_SECTION ||
      (scanner->heading_status == STATUS_MATCH_HEADING && scanner->heading_level == 0) ||
      (scanner->heading_levels & 0xc0) != 0 ||
      scanner->simulate > FLAG_SIMULATE_HEADING ||
      !is_valid_cached_heading_token(scanner->cached_heading_token)) return 0;
  for (uint32_t i = 0; i < scanner->block_depth; i++) {
    if (!is_valid_block_type(scanner->block_stack[i])) return 0;
  }
  for (uint32_t i = 0; i < format_count; i++) {
    if ((scanner->format_stack[i] & 0x80) != 0) return 0;
  }

  size_t size = 0;
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
  buffer[size++] = (char)scanner->simulate;
  buffer[size++] = (char)scanner->link_depth;
  buffer[size++] = (char)scanner->footnote_depth;
  buffer[size++] = (char)scanner->block_depth;
  for (uint32_t i = 0; i < scanner->block_depth; i++) {
    buffer[size++] = (char)scanner->block_stack[i];
  }
  for (uint32_t i = 0; i < table_count; i++) {
    buffer[size++] = (char)scanner->table_stack[i];
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
  memset(scanner->table_stack, 0, sizeof scanner->table_stack);
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
  scanner->simulate = FLAG_NONE;
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
  uint8_t simulate = data[size++];
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
      simulate > FLAG_SIMULATE_HEADING ||
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
  memcpy(scanner->table_stack, data + table_offset, table_count);
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
  scanner->simulate = simulate;
  scanner->block_depth = saved_block_depth;
  scanner->link_depth = saved_link_depth;
  scanner->footnote_depth = saved_footnote_depth;
}

// --- Main Scan ---

static bool scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  uint8_t top_type = scanner->block_depth > 0 ? scanner->block_stack[scanner->block_depth - 1] : BLOCK_TYPE_NONE;
  bool is_formatted = false;
  uint32_t format_depth;
  if (get_format_index(scanner, &format_depth)) {
    is_formatted = scanner->format_stack[format_depth] > 0;
  }
  uint16_t paragraph_depth = scanner->block_depth + get_table_stack(scanner);
  uint16_t paragraph_type_stack_first_index = scanner->paragraph_type_stack_size - scanner->paragraph_stack[paragraph_depth];

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

  // 문서 끝에 도달하면 모두 강제 종료
  if (lexer->eof(lexer)) {
    if (!is_formatted && get_table_stack(scanner) && lexer->get_column(lexer) == scanner->table_end_column) {
      if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[TABLE_ROW_END]) {
        clear_table_stack(scanner);
        lexer->result_symbol = TABLE_ROW_END;
        return true;
      }
    }

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
  if (scanner->simulate == FLAG_NONE && lexer->get_column(lexer) == 0 && lexer->lookahead == '#' && consume_string(lexer, "##") && valid_symbols[COMMENT]) {
    while (lexer->lookahead != '\r' && lexer->lookahead != '\n' && !lexer->eof(lexer)) {
      advance(lexer);
    }

    mark_end(scanner, lexer);
    lexer->result_symbol = COMMENT;
    return true;
  }

  // bracket_end 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '}' && scanner->block_depth > 0) {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "}}}")) {
      if (!is_formatted && get_table_stack(scanner) && lexer->get_column(lexer) == scanner->table_end_column + 3) {
        if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

        if (valid_symbols[TABLE_ROW_END]) {
          clear_table_stack(scanner);
          lexer->result_symbol = TABLE_ROW_END;
          return true;
        }
      }

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
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == ')' && scanner->in_macro) {
    mark_end(scanner, lexer);
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

  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '|') {
    mark_end(scanner, lexer);

    // link_pipe 처리
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
    if (!is_formatted && get_table_stack(scanner) && valid_symbols[TABLE_CAPTION_END]) {
      mark_end(scanner, lexer);
      lexer->result_symbol = TABLE_CAPTION_END;
      return true;
    }

    if (lexer->lookahead == '|') {
      advance(lexer);
      
      // table_row_start 처리
      if (!is_formatted && !get_table_stack(scanner) && lexer->get_column(lexer) == scanner->paragraph_end_column + 2 && valid_symbols[TABLE_ROW_START]) {
        mark_end(scanner, lexer);
        set_table_stack(scanner);
        lexer->result_symbol = TABLE_ROW_START;
        return true;
      }

      // table_cell_split 처리
      if (!is_formatted && get_table_stack(scanner) && valid_symbols[TABLE_CELL_SPLIT]) {
        if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

        mark_end(scanner, lexer);
        scanner->table_end_column = lexer->get_column(lexer);
        lexer->result_symbol = TABLE_CELL_SPLIT;
        return true;    
      }
    } 
    else if (!is_formatted && !get_table_stack(scanner) && lexer->get_column(lexer) == scanner->paragraph_end_column + 1 && valid_symbols[TABLE_CAPTION_START]) {
      mark_end(scanner, lexer);

      bool is_valid_table_caption_end = false;
      while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !lexer->eof(lexer)) {
        if (lexer->lookahead == '|') {
          is_valid_table_caption_end = true;
          break;
        }
        advance(lexer);
      }

      if (is_valid_table_caption_end)  {
        set_table_stack(scanner);
        lexer->result_symbol = TABLE_CAPTION_START;
        return true;
      }

      if (valid_symbols[PIPE]) {
        lexer->result_symbol = PIPE;
        return true;
      }

      return false;
    }

    // pipe는 1개거나 2개일 수 있음
    if (valid_symbols[PIPE]) {
      mark_end(scanner, lexer);
      lexer->result_symbol = PIPE;
      return true;
    }

    return false;
  }

  // table_argument_start
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '<' && (valid_symbols[TABLE_ARGUMENT_START] || valid_symbols[TABLE_SPECIAL_ARGUMENT])) {
    advance(lexer);
    mark_end(scanner, lexer);

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

    bool is_valid = false;
    bool is_zero_length = true;
    while (lexer->lookahead != '\n' && lexer->lookahead != '\r' && !lexer->eof(lexer)) {
      if (lexer->lookahead == ' ' || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z')) {
        is_zero_length = false;
        advance(lexer);
        continue;
      }

      if (lexer->lookahead == '>' || lexer->lookahead == '=') {
        is_valid = true;
      }

      break;
    }

    if (is_valid && !is_zero_length && valid_symbols[TABLE_ARGUMENT_START]) {
      lexer->result_symbol = TABLE_ARGUMENT_START;
      return true;
    }

    return false;
  }

  // table_argument_end
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '>' && valid_symbols[TABLE_ARGUMENT_END]) {
    advance(lexer);
    lexer->result_symbol = TABLE_ARGUMENT_END;
    return true;
  }

  if (scanner->simulate == FLAG_NONE && lexer->lookahead == ']') {
    mark_end(scanner, lexer);
    advance(lexer);

    // footnote_end 처리
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

    // link_end 처리
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

  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '\'') {
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

  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '-' && lexer->get_column(lexer) == scanner->paragraph_end_column && scanner->is_horizontal_line_valid) {
    size_t size = 0;
    while (lexer->lookahead == '-') {
      advance(lexer);
      size++;
    }

    mark_end(scanner, lexer);

    if (lexer->lookahead == '\r' || lexer->lookahead == '\n' || lexer->eof(lexer)) {
      if (size >= 4 && size < 10 && valid_symbols[HORIZONTAL_LINE]) {
        lexer->result_symbol = HORIZONTAL_LINE;
        return true;
      }
      if (size >= 10 && valid_symbols[HORIZONTAL_LINE_IMPLICIT]) {
        lexer->result_symbol = HORIZONTAL_LINE_IMPLICIT;
        return true;
      }
    }

    scanner->is_horizontal_line_valid = false;
    return false;
  }
  
  // strike 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '-') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "--")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_STRIKE, STRIKE_START);
    }
    return false;
  }

  // strike2 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '~') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "~~")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_STRIKE2, STRIKE2_START);
    }
    return false;
  }

  // superscript 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '^') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "^^")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_SUPERSCRIPT, SUPERSCRIPT_START);
    }
    return false;
  }

  // subscript 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == ',') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, ",,")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_SUBSCRIPT, SUBSCRIPT_START);
    }
    return false;
  }

  // underline 처리
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '_') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "__")) {
      return consume_format(lexer, scanner, valid_symbols, FLAG_UNDERLINE, UNDERLINE_START);
    }
    return false;
  }

  // newline 시 허용하지 않는 문법 강제 종료 처리
  if (scanner->simulate == FLAG_NONE && (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
    if (!is_formatted && get_table_stack(scanner) && lexer->get_column(lexer) == scanner->table_end_column) {
      if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[TABLE_ROW_END]) {
        clear_table_stack(scanner);
        lexer->result_symbol = TABLE_ROW_END;
        return true;
      }
    }

    mark_end(scanner, lexer);
    if (lexer->lookahead == '\r') {
      advance(lexer);
    }
    advance(lexer);
    if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_caption_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (register_table_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
    if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;

    if (valid_symbols[SOFT_LINE_ENDING]) {
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

    if (lexer->get_column(lexer) == 0) {
      if (lexer->lookahead != '=') {
        scanner->cached_heading_token = NO_HEADING_TOKEN;
      }

      if (lexer->lookahead == ' ' || lexer->lookahead == '>') {
        uint8_t type = PARAGRAPH_TYPE_FALLBACK;
        bool is_valid = true;
        bool to_be_invalid = false;
        for (size_t i = paragraph_type_stack_first_index; i < scanner->paragraph_type_stack_size; i++) {
          type = get_paragraph_type(scanner, i);

          if (to_be_invalid) {
            is_valid = false;
            break;
          }

          if ((type == PARAGRAPH_TYPE_PARAGRAPH || type == PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH || type == PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH) && lexer->lookahead == ' ') {
            advance(lexer);

            if (lexer->lookahead == '*') {
              is_valid = false;
              break;
            }

            if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
              advance(lexer);
              to_be_invalid = true;

              if (lexer->lookahead == '.') {
                is_valid = false;
                break;
              }
            }

            continue;
          }

          if (type == PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH && lexer->lookahead == '>') {
            advance(lexer);
            continue;
          }

          is_valid = false;
          break;
        }

        if (!is_valid && register_paragraph_end(lexer, scanner, valid_symbols)) return true;
      } else {
        if (register_paragraph_end(lexer, scanner, valid_symbols)) return true;
      }
    }

    if (scanner->cached_heading_token == NO_CACHED) {
      scanner->simulate = FLAG_SIMULATE_HEADING;
      scanner->cached_heading_token = match_heading_token(lexer, scanner);
      scanner->simulate = FLAG_NONE;
    }
    
    if (scanner->cached_heading_token != NO_HEADING_TOKEN && (top_type == BLOCK_TYPE_NONE || is_breakable_bracket)) {
      uint8_t heading_level_offset = scanner->cached_heading_token - HEADING1_START + 1;
      uint8_t heading_level = heading_level_offset > 6 ? heading_level_offset - 6 : heading_level_offset;

      if (register_bracket_implicit_end(lexer, scanner, valid_symbols)) return true;
      if (register_table_row_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[LINE_ENDING]) {
        if (scanner->heading_level < heading_level && scanner->heading_level != STATUS_NO_HEADING_SECTION && valid_symbols[SOFT_LINE_PREFIX]) {
          lexer->result_symbol = SOFT_LINE_PREFIX;
          return true;
        }

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

    if (valid_symbols[SOFT_LINE_PREFIX]) {
      lexer->result_symbol = SOFT_LINE_PREFIX;
      return true;
    }

    return false;
  }

  if (
    scanner->simulate == FLAG_NONE &&
    (lexer->lookahead == ' ' || lexer->lookahead == '>') &&
    lexer->get_column(lexer) == scanner->paragraph_end_column &&
    paragraph_type_stack_first_index + scanner->paragraph_skipped_index < scanner->paragraph_type_stack_size &&
    valid_symbols[PARAGRAPH_SKIP]
  ) {
    uint8_t type = get_paragraph_type(scanner, paragraph_type_stack_first_index + scanner->paragraph_skipped_index);
    
    if ((type == PARAGRAPH_TYPE_PARAGRAPH || type == PARAGRAPH_TYPE_UNORDERED_LIST_PARAGRAPH || type == PARAGRAPH_TYPE_ORDERED_LIST_PARAGRAPH) && lexer->lookahead == ' ') {
      advance(lexer);
      mark_end(scanner, lexer);

      if (lexer->lookahead == '*') {
        return false;
      }

      if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
        advance(lexer);

        if (lexer->lookahead == '.') {
          return false;
        }

        scanner->paragraph_skipped_index++;
        scanner->paragraph_end_column = lexer->get_column(lexer) - 1;
        lexer->result_symbol = PARAGRAPH_SKIP;
        return true;
      }

      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_SKIP;
      return true;
    }

    if (type == PARAGRAPH_TYPE_BLOCKQUOTE_PARAGRAPH && lexer->lookahead == '>') {
      advance(lexer);
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_SKIP;
      return true;
    }

    return false;
  }

  if (
    scanner->simulate == FLAG_NONE &&
    lexer->lookahead == ' ' &&
    (valid_symbols[PARAGRAPH_START] || valid_symbols[UNORDERED_LIST_PARAGRAPH_START] || valid_symbols[ORDERED_LIST_PARAGRAPH_START]) &&
    lexer->get_column(lexer) == scanner->paragraph_end_column &&
    paragraph_type_stack_first_index + scanner->paragraph_skipped_index >= scanner->paragraph_type_stack_size
  ) {
    advance(lexer);
    mark_end(scanner, lexer);

    if (lexer->lookahead == '*' && valid_symbols[UNORDERED_LIST_PARAGRAPH_START]) {
      advance(lexer);
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

    if (lexer->lookahead == '1' || lexer->lookahead == 'a' || lexer->lookahead == 'A' || lexer->lookahead == 'i' || lexer->lookahead == 'I') {
      advance(lexer);

      if (lexer->lookahead == '.' && valid_symbols[ORDERED_LIST_PARAGRAPH_START]) {
        advance(lexer);

        if (lexer->lookahead == '#') {
          advance(lexer);
          if (('0' <= lexer->lookahead && lexer->lookahead <= '9') || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z')) {
            while (('0' <= lexer->lookahead && lexer->lookahead <= '9') || ('a' <= lexer->lookahead && lexer->lookahead <= 'z') || ('A' <= lexer->lookahead && lexer->lookahead <= 'Z')) {
              advance(lexer);
            }
          }
        }

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

      if (push_paragraph(scanner, PARAGRAPH_TYPE_PARAGRAPH)) {
        scanner->paragraph_skipped_index++;
        scanner->paragraph_end_column = lexer->get_column(lexer) - 1; // 1|a|A|i|I
        lexer->result_symbol = PARAGRAPH_START;
        return true;
      }

      return false;
    }

    if (valid_symbols[PARAGRAPH_START] && push_paragraph(scanner, PARAGRAPH_TYPE_PARAGRAPH)) {
      scanner->paragraph_skipped_index++;
      scanner->paragraph_end_column = lexer->get_column(lexer);
      lexer->result_symbol = PARAGRAPH_START;
      return true;
    }
    
    return false;
  }

  if (
    scanner->simulate == FLAG_NONE &&
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

  if (lexer->lookahead == ' ' && valid_symbols[SPACE]) {
    advance(lexer);
    lexer->result_symbol = SPACE;
    return true;
  }

  if (lexer->lookahead == '>' && valid_symbols[GREATER_THAN_SIGN]) {
    advance(lexer);
    lexer->result_symbol = GREATER_THAN_SIGN;
    return true;
  }

  // argument_end 처리
  if (scanner->simulate == FLAG_NONE && scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_END]) {
    advance(lexer);
    scanner->in_argument = false;
    lexer->result_symbol = ARGUMENT_END;
    return true;
  }

  // heading_start 처리
  if (scanner->heading_status != STATUS_MATCH_HEADING && lexer->get_column(lexer) == 0 && lexer->lookahead == '=') {
    if (scanner->cached_heading_token != NO_HEADING_TOKEN && scanner->cached_heading_token != NO_CACHED && valid_symbols[scanner->cached_heading_token]) {
      while (lexer->lookahead != ' ') {
        advance(lexer);
      }
  
      uint8_t offset = scanner->cached_heading_token - HEADING1_START + 1;
          
      if (scanner->simulate == FLAG_NONE) {
        scanner->heading_level = offset > 6 ? offset - 6 : offset;
        scanner->heading_status = STATUS_MATCH_HEADING;
        scanner->heading_levels |= 1 << (scanner->heading_level - 1);
        scanner->is_hidden_heading = offset > 6;
        lexer->result_symbol = scanner->cached_heading_token;
        scanner->cached_heading_token = NO_CACHED;
      }
      return true;
    }
  
    if ((top_type == BLOCK_TYPE_NONE || is_breakable_bracket)) {
      uint8_t heading_token = match_heading_token(lexer, scanner);
      if (heading_token != NO_HEADING_TOKEN) {
        if (valid_symbols[heading_token]) {
          uint8_t offset = heading_token - HEADING1_START + 1;
          
          if (scanner->simulate == FLAG_NONE) {
            scanner->heading_level = offset > 6 ? offset - 6 : offset;
            scanner->heading_status = STATUS_MATCH_HEADING;
            scanner->heading_levels |= 1 << (scanner->heading_level - 1);
            scanner->is_hidden_heading = offset > 6;
            lexer->result_symbol = heading_token;
          }
          return true;
        }
        return false;
      }
    }
  }

  // heading_end 처리
  if (scanner->heading_status == STATUS_MATCH_HEADING && (lexer->lookahead == '#' || lexer->lookahead == '=')) {
    char heading_end_string[100];
    const char *heading_prefix = scanner->is_hidden_heading ? "#" : "";
    strncpy(heading_end_string, heading_prefix, sizeof(heading_end_string) - 1);
    heading_end_string[sizeof(heading_end_string) - 1] = '\0';
    
    int len = strlen(heading_end_string);
    memset(heading_end_string + len, '=', scanner->heading_level);
    heading_end_string[len + scanner->heading_level] = '\0';

    mark_end(scanner, lexer);

    if (consume_string(lexer, heading_end_string)) {
      if (scanner->simulate == FLAG_NONE) {
        if (register_bracket_implicit_end(lexer, scanner, valid_symbols)) return true;
        if (register_macro_implicit_end(lexer, scanner, valid_symbols)) return true;
        if (register_link_implicit_end(lexer, scanner, valid_symbols)) return true;
        if (register_footnote_implicit_end(lexer, scanner, valid_symbols)) return true;
        if (close_format_implicit_ends(lexer, scanner, valid_symbols, FLAG_NONE)) return true;
        if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      }
      
      if (valid_symbols[HEADING_END]) {
        mark_end(scanner, lexer);
        if (scanner->simulate == FLAG_NONE) {
          scanner->heading_status = STATUS_HEADING_SECTION;
          lexer->result_symbol = HEADING_END;
        }
        return true;
      }
    }
    return false;
  }

  // block류 매칭
  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '{') {
    mark_end(scanner, lexer);
    if (consume_string(lexer, "{{{")) {
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;
      
      mark_end(scanner, lexer);

      if (lexer->lookahead == '#') {
        advance(lexer);

        // block_start 처리
        if (lexer->lookahead == '!') {
          advance(lexer);

          if (consume_string(lexer, "if")) {
            return consume_depth_block(lexer, scanner, valid_symbols, IF_BLOCK_START, BLOCK_TYPE_IF, true);
          }
          if (consume_string(lexer, "folding")) {
            return consume_depth_block(lexer, scanner, valid_symbols, FOLDING_BLOCK_START, BLOCK_TYPE_FOLDING, false);
          }
          if (consume_string(lexer, "s")) {
            if (consume_string(lexer, "yntax")) {
              return consume_syntax_block(lexer, scanner, valid_symbols);
            }
            if (consume_string(lexer, "tyle")) {
              if (!consume_newline(lexer)) {
                return register_block(lexer, scanner, valid_symbols, STYLE_FALLBACK_BLOCK_START, BLOCK_TYPE_FALLBACK);
              }
              return register_block(lexer, scanner, valid_symbols, STYLE_BLOCK_START, BLOCK_TYPE_STYLE);
            }
          }
          if (consume_string(lexer, "wiki")) {
            return consume_depth_block(lexer, scanner, valid_symbols, WIKI_BLOCK_START, BLOCK_TYPE_WIKI, false);
          }
          if (consume_string(lexer, "html")) {
            return register_block(lexer, scanner, valid_symbols, HTML_BLOCK_START, BLOCK_TYPE_HTML);
          }
          if (consume_string(lexer, "latex")) {
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

  if (scanner->simulate == FLAG_NONE && lexer->lookahead == '[') {
    mark_end(scanner, lexer);
    advance(lexer);
    // link_start 처리
    if (lexer->lookahead == '[') {
      advance(lexer);
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if ((valid_symbols[LINK_START] || valid_symbols[FILE_LINK_START]) &&
          can_increment_format_depth(scanner)) {
        mark_end(scanner, lexer);
        scanner->link_depth++;
        reset_current_format_scope(scanner);

        lexer->result_symbol = consume_string(lexer, "파일:") ? FILE_LINK_START : LINK_START;
        return true;
      }

      return false;
    }

    if (!(scanner->in_argument && valid_symbols[ARGUMENT_IMPLICIT_END])) {
      mark_end(scanner, lexer);
    }

    // footnote_start 처리
    if (lexer->lookahead == '*') {
      advance(lexer);
      if (register_argument_implicit_end(lexer, scanner, valid_symbols)) return true;

      if (valid_symbols[FOOTNOTE_START] && can_increment_format_depth(scanner)) {
        mark_end(scanner, lexer);
        scanner->footnote_depth++;
        reset_current_format_scope(scanner);

        lexer->result_symbol = FOOTNOTE_START;
        return true;
      }
      return false;
    }

    // macro_start 처리
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
  if (scanner->simulate == FLAG_NONE && !scanner->in_argument && lexer->lookahead == '@' && valid_symbols[ARGUMENT_START] && consume_argument(lexer, scanner)) {
    scanner->in_argument = true;
    lexer->result_symbol = ARGUMENT_START;
    return true;
  }

  return false;
}

bool tree_sitter_namumark_external_scanner_scan(void *payload, TSLexer *lexer, const bool *valid_symbols) {
  Scanner *scanner = (Scanner *)payload;
  scanner->simulate = FLAG_NONE;
  return scan(payload, lexer, valid_symbols);
}