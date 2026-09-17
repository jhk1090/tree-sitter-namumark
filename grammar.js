/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'namumark',

  externals: $ => [
    $.nowiki_start,
    $.nowiki_end,
    $._nowiki_nest_open,
    $._nowiki_nest_close,
    $.if_block_start,
    $.if_block_end,
    $.folding_block_start,
    $.folding_block_end,
    $.syntax_block_start,
    $.syntax_block_end,
    $._syntax_block_nest_open,
    $._syntax_block_nest_close,
    $.wiki_block_start,
    $.wiki_block_end,
    $.html_block_start,
    $.html_block_end,
    $._html_block_nest_open,
    $._html_block_nest_close,
    $.style_block_start,
    $.style_block_end,
    $._style_block_nest_open,
    $._style_block_nest_close,
    $.latex_block_start,
    $.latex_block_end,
    $._latex_block_nest_open,
    $._latex_block_nest_close,
    $.scale_block_start,
    $.scale_block_end,
    $.color_block_start,
    $.color_block_end,
    $.bracket_end
  ],

  rules: {
    source_file: $ => repeat($._node),

    _node: $ => choice(
      $.nowiki,
      $.if_block,
      $.folding_block,
      $.syntax_block,
      $.wiki_block,
      $.html_block,
      $.style_block,
      $.latex_block,
      $.scale_block,
      $.color_block,
      $.text,
    ),

    if_block: $ => seq(
      $.if_block_start,
      "#!if",
      optional($.block_header),
      /\r?\n/,
      optional($.block_content),
      $.bracket_end
    ),

    folding_block: $ => seq(
      $.folding_block_start,
      "#!folding",
      optional($.block_header),
      /\r?\n/,
      optional($.block_content),
      $.bracket_end
    ),

    syntax_block: $ => seq(
      $.syntax_block_start,
      "#!syntax",
      $.block_header,
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      $.bracket_end
    ),

    html_block: $ => seq(
      $.html_block_start,
      "#!html",
      optional(field('content', $.nowiki_content)),
      $.bracket_end
    ),

    style_block: $ => seq(
      $.style_block_start,
      "#!style",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      $.bracket_end
    ),

    latex_block: $ => seq(
      $.latex_block_start,
      "#!latex",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      $.bracket_end
    ),

    wiki_block: $ => seq(
      $.wiki_block_start,
      "#!wiki",
      optional($.block_header),
      /\r?\n/,
      optional($.block_content),
      $.bracket_end
    ),

    block_header: $ => repeat1(/[^\n\s]+/),
    block_content: $ => repeat1($._node),

    scale_block: $ => seq(
      $.scale_block_start,
      $.scale_size,
      /\r?\n| /,
      optional($.block_content),
      $.bracket_end
    ),
    scale_size: $ => token(/[+-][1-5]/),

    color_block: $ => seq(
      $.color_block_start,
      field('color1', $.color_color),
      optional(
        seq(",", field('color2', $.color_color))
      ),
      /\r?\n| /,
      optional($.block_content),
      $.bracket_end
    ),
    color_color: $ => token(/[^\n\s,]+/),

    nowiki: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content)),
      $.bracket_end
    ),

    nowiki_content: $ => repeat1(
      choice(
        seq($.nowiki_start, optional($.nowiki_content), $.bracket_end),
        /[^{}\r\n]+/,
        /\r?\n/,
        '{',
        token(prec(-10, '}'))
      )
    ),

    // [핵심 변경] text 규칙에서 \r\n과 '}'가 결합하지 못하도록 명시적 분리
    // '}' 문자는 가급적 External Scanner가 처리하도록 아주 낮은 우선순위(-10) 지정
    text: $ => prec(-1, choice(
      /[^{}\r\n]+/,
      /\r?\n/,
      '{',
      token(prec(-10, '}'))
    ))
  }
});