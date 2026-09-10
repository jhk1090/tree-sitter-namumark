/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'namumark',

  externals: $ => [
    $.wiki_block_start,
    $.nowiki_start,
    $.wiki_block_end,
    $.nowiki_end,
    $._nowiki_nest_open,
    $._nowiki_nest_close
  ],

  rules: {
    source_file: $ => repeat($._node),

    _node: $ => choice(
      $.wiki_block,
      $.nowiki_inline,
      $.text,
    ),

    wiki_block: $ => seq(
      $.wiki_block_start,
      optional($.wiki_block_header),
      '\n',
      optional($.wiki_block_content),
      $.wiki_block_end
    ),

    wiki_block_header: $ => repeat1(
      choice(
        $.attribute,
        $._header_text
      )
    ),

    attribute: $ => seq(
      field('key', /[a-zA-Z_][a-zA-Z0-9_-]*/),
      '=',
      field('value', choice(
        /\"[^\"]*\"/,
        /\'[^\']*\'/,
        /[^\s"'>]+/
      ))
    ),

    _header_text: $ => /[^\n="'\s]+/,

    wiki_block_content: $ => repeat1($._node),

    nowiki_inline: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content)),
      $.nowiki_end
    ),

    nowiki_content: $ => repeat1(
      choice(
        $._nowiki_nest_open,
        $._nowiki_nest_close,
        /[^{}\r\n]+/,
        /\r?\n/,
        '{',
        token(prec(-10, '}'))
      )
    ),

    // [핵심 변경] text 규칙에서 \r\n과 '}'가 결합하지 못하도록 명시적 분리
    // '}' 문자는 가급적 External Scanner가 처리하도록 아주 낮은 우선순위(-10) 지정
    text: $ => choice(
      /[^{}\r\n]+/,
      /\r?\n/,
      '{',
      token(prec(-10, '}'))
    )
  }
});