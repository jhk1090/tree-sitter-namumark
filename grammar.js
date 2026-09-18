/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

module.exports = grammar({
  name: 'namumark',

  externals: $ => [
    $.nowiki_start,
    $.if_block_start,
    $.if_block_implicit_end,
    $.folding_block_start,
    $.folding_block_implicit_end,
    $.syntax_block_start,
    $.wiki_block_start,
    $.wiki_block_implicit_end,
    $.html_block_start,
    $.style_block_start,
    $.latex_block_start,
    $.scale_block_start,
    $.scale_block_implicit_end,
    $.color_block_start,
    $.color_block_implicit_end,
    $.bracket_end,
    $.heading1_start,
    $.heading2_start,
    $.heading3_start,
    $.heading4_start,
    $.heading5_start,
    $.heading6_start,
    $.hidden_heading1_start,
    $.hidden_heading2_start,
    $.hidden_heading3_start,
    $.hidden_heading4_start,
    $.hidden_heading5_start,
    $.hidden_heading6_start,
  ],

  rules: {
    source_file: ($) =>
      seq(
        alias(prec.right(repeat($._block_not_section)), $.section),
        repeat($.section),
      ),
    _block_not_section: ($) => $._node,
    section: ($) =>
      choice(
        $._section1,
        $._section2,
        $._section3,
        $._section4,
        $._section5,
        $._section6,
      ),
    _section1: ($) =>
      prec.right(
        seq(
          choice($.heading1, $.hidden_heading1),
          repeat(
            choice(
              alias(
                choice(
                  $._section6,
                  $._section5,
                  $._section4,
                  $._section3,
                  $._section2,
                ),
                $.section,
              ),
              $._block_not_section,
            ),
          ),
        ),
      ),
    _section2: ($) =>
      prec.right(
        seq(
          choice($.heading2, $.hidden_heading2),
          repeat(
            choice(
              alias(
                choice($._section6, $._section5, $._section4, $._section3),
                $.section,
              ),
              $._block_not_section,
            ),
          ),
        ),
      ),
    _section3: ($) =>
      prec.right(
        seq(
          choice($.heading3, $.hidden_heading3),
          repeat(
            choice(
              alias(choice($._section6, $._section5, $._section4), $.section),
              $._block_not_section,
            ),
          ),
        ),
      ),
    _section4: ($) =>
      prec.right(
        seq(
          choice($.heading4, $.hidden_heading4),
          repeat(
            choice(
              alias(choice($._section6, $._section5), $.section),
              $._block_not_section,
            ),
          ),
        ),
      ),
    _section5: ($) =>
      prec.right(
        seq(
          choice($.heading5, $.hidden_heading5),
          repeat(choice(alias($._section6, $.section), $._block_not_section)),
        ),
      ),
    _section6: ($) => 
      prec.right(
        seq(
          choice($.heading6, $.hidden_heading6),
          repeat($._block_not_section)
        )
      ),
    heading: ($) =>
      choice(
        $.heading1, $.heading2, $.heading3, $.heading4, $.heading5, $.heading6,
        $.hidden_heading1, $.hidden_heading2, $.hidden_heading3, $.hidden_heading4, $.hidden_heading5, $.hidden_heading6,
      ),
    _heading_content: ($) => repeat1(
      choice(
        /[^\r\n=#]+/,
        '#',
        '='
      )
    ),
    heading1: ($) =>
      seq(
        alias($.heading1_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={1}/, $.heading_marker),
        /\r?\n/,
      ),
    heading2: ($) =>
      seq(
        alias($.heading2_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={2}/, $.heading_marker),
        /\r?\n/,
      ),
    heading3: ($) =>
      seq(
        alias($.heading3_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={3}/, $.heading_marker),
        /\r?\n/,
      ),
    heading4: ($) =>
      seq(
        alias($.heading4_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={4}/, $.heading_marker),
        /\r?\n/,
      ),
    heading5: ($) =>
      seq(
        alias($.heading5_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={5}/, $.heading_marker),
        /\r?\n/,
      ),
    heading6: ($) =>
      seq(
        alias($.heading6_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/={6}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading1: ($) =>
      seq(
        alias($.hidden_heading1_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={1}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading2: ($) =>
      seq(
        alias($.hidden_heading2_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={2}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading3: ($) =>
      seq(
        alias($.hidden_heading3_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={3}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading4: ($) =>
      seq(
        alias($.hidden_heading4_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={4}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading5: ($) =>
      seq(
        alias($.hidden_heading5_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={5}/, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading6: ($) =>
      seq(
        alias($.hidden_heading6_start, $.heading_marker),
        field("title", $._heading_content),
        alias(/#={6}/, $.heading_marker),
        /\r?\n/,
      ),

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
      choice(
        $.bracket_end,
        $.if_block_implicit_end
      )
    ),

    folding_block: $ => seq(
      $.folding_block_start,
      "#!folding",
      optional($.block_header),
      /\r?\n/,
      optional($.block_content),
      choice(
        $.bracket_end,
        $.folding_block_implicit_end
      )
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
      choice(
        $.bracket_end,
        $.wiki_block_implicit_end
      )
    ),

    block_header: $ => repeat1(/[^\n\s]+/),
    block_content: $ => repeat1($._node),

    scale_block: $ => seq(
      $.scale_block_start,
      $.scale_size,
      /\r?\n| /,
      optional($.block_content),
      choice(
        $.bracket_end,
        $.scale_block_implicit_end
      )
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
      choice(
        $.bracket_end,
        $.color_block_implicit_end
      )
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
        token(prec(-10, '=')),
        token(prec(-10, '#')),
        /\r?\n/,
        '{',
        token(prec(-10, '}'))
      )
    ),

    text: $ => prec(-1, choice(
      /[^{}=#\r\n]+/,
      token(prec(-10, '=')),
      token(prec(-10, '#')),
      /\r?\n/,
      '{',
      token(prec(-10, '}'))
    ))
  }
});