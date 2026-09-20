/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const requiredArgMacros = ["anchor", "age", "dday", "youtube", "kakaotv", "vimeo", "nicovideo", "navertv", "math", "include", "ruby"]
const optionalArgMacros = ["pagecount", "목차", "tableofcontents"]
const noArgMacros = ["clearfix", "date", "datetime", "각주", "footnote", "br"]

module.exports = grammar({
  name: 'namumark',

  externals: $ => [
    $.comment,
    $.nowiki_start,
    $.if_block_start,
    $.folding_block_start,
    $.syntax_block_start,
    $.wiki_block_start,
    $.html_block_start,
    $.style_block_start,
    $.latex_block_start,
    $.scale_block_start,
    $.color_block_start,
    $.bracket_end,
    $.bracket_implicit_end,
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
    $.heading_end,
    $.macro_start,
    $.macro_end,
    $.macro_implicit_end,
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
    heading1: ($) =>
      seq(
        alias($.heading1_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    heading2: ($) =>
      seq(
        alias($.heading2_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    heading3: ($) =>
      seq(
        alias($.heading3_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    heading4: ($) =>
      seq(
        alias($.heading4_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    heading5: ($) =>
      seq(
        alias($.heading5_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    heading6: ($) =>
      seq(
        alias($.heading6_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading1: ($) =>
      seq(
        alias($.hidden_heading1_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading2: ($) =>
      seq(
        alias($.hidden_heading2_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading3: ($) =>
      seq(
        alias($.hidden_heading3_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading4: ($) =>
      seq(
        alias($.hidden_heading4_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading5: ($) =>
      seq(
        alias($.hidden_heading5_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),
    hidden_heading6: ($) =>
      seq(
        alias($.hidden_heading6_start, $.heading_marker),
        field("title", repeat1($._node_inline)),
        alias($.heading_end, $.heading_marker),
        /\r?\n/,
      ),

    _node: $ => choice(
      $.comment,
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
      $.escape,
      $.text,
      $.macro,
      $.macro_without_args
    ),
    _node_inline: $ => choice(
      $.nowiki_inline,
      $.html_block_inline,
      $.scale_block_inline,
      $.color_block_inline,
      $.escape,
      $.text_inline,
      $.macro_inline,
      $.macro_without_args
    ),

    if_block: $ => seq(
      $.if_block_start,
      "#!if",
      optional($.block_header),
      /\r?\n/,
      optional($.block_content),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
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
        $.bracket_implicit_end
      )
    ),

    syntax_block: $ => seq(
      $.syntax_block_start,
      "#!syntax",
      $.block_header,
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),

    html_block: $ => seq(
      $.html_block_start,
      "#!html",
      optional(field('content', $.nowiki_content)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),
    html_block_inline: $ => seq(
      $.html_block_start,
      "#!html",
      optional(field('content', $.nowiki_content_inline)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),

    style_block: $ => seq(
      $.style_block_start,
      "#!style",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),

    latex_block: $ => seq(
      $.latex_block_start,
      "#!latex",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),

    wiki_block: $ => seq(
      $.wiki_block_start,
      "#!wiki",
      optional($.wiki_block_header),
      /\r?\n/,
      optional($.block_content),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),
    wiki_block_header: ($) => repeat1(choice(
      $._wiki_attribute,
      /[^\n\s]/ // 속성 없이 그냥 일반 헤더만 오는 경우
    )),

    // 5가지 속성 중 하나와 매칭
    _wiki_attribute: ($) => choice(
      field("style", $.wiki_style_attr),
      field("dark_style", $.wiki_dark_style_attr),
      field("tag", $.wiki_tag_attr),
      field("class", $.wiki_class_attr),
      field("lang", $.wiki_lang_attr)
    ),

    // 끝나는 따옴표가 있어야지만 유효
    wiki_attr_value: ($) => token(prec(1, /[^\r\n"]+/)),
    wiki_style_attr: ($) => seq('style="', optional(field("value", $.wiki_attr_value)), optional(field("end", '"'))),
    wiki_dark_style_attr: ($) => seq('dark-style="', optional(field("value", $.wiki_attr_value)), optional(field("end", '"'))),
    wiki_tag_attr: ($) => seq('tag="', optional(field("value", $.wiki_attr_value)), optional(field("end", '"'))),
    wiki_class_attr: ($) => seq('class="', optional(field("value", $.wiki_attr_value)), optional(field("end", '"'))),
    wiki_lang_attr: ($) => seq('lang="', optional(field("value", $.wiki_attr_value)), optional(field("end", '"'))),

    block_header: $ => repeat1(/[^\n\s]+/),
    block_content: $ => repeat1($._node),
    block_content_inline: $ => repeat1($._node_inline),

    scale_block: $ => seq(
      $.scale_block_start,
      $.scale_size,
      /\r?\n| /,
      optional($.block_content),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),
    scale_block_inline: $ => seq(
      $.scale_block_start,
      $.scale_size,
      " ",
      optional($.block_content_inline),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
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
        $.bracket_implicit_end
      )
    ),
    color_block_inline: $ => seq(
      $.color_block_start,
      field('color1', $.color_color),
      optional(
        seq(",", field('color2', $.color_color))
      ),
      " ",
      optional($.block_content_inline),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),
    color_color: $ => token(/[^\n\s,]+/),

    required_arg_macro_name: $ => choice(...requiredArgMacros),
    optional_arg_macro_name: $ => choice(...optionalArgMacros),
    no_arg_macro_name: $ => choice(...noArgMacros),
    _macro_name: $ => choice(
      $.required_arg_macro_name,
      $.optional_arg_macro_name,
      $.no_arg_macro_name
    ),

    macro_without_args: $ => token(new RegExp(`\\[(${[...requiredArgMacros, ...optionalArgMacros, ...noArgMacros].join("|")})\\]`)),
    macro: $ => seq(
      $.macro_start,
      $._macro_name,
      "(",
      repeat(
        choice(
          /[^=)]/,
          token(prec(-10, '=')),
          token(prec(-10, ')'))
        )
      ),
      choice(
        $.macro_end,
        $.macro_implicit_end
      )
    ),
    macro_inline: $ => seq(
      $.macro_start,
      $._macro_name,
      "(",
      repeat(
        choice(
          /[^=\r\n)]/,
          token(prec(-10, '=')),
          token(prec(-10, ')'))
        )
      ),
      choice(
        $.macro_end,
        $.macro_implicit_end
      )
    ),

    nowiki: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),
    nowiki_inline: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content_inline)),
      choice(
        $.bracket_end,
        $.bracket_implicit_end
      )
    ),

    nowiki_content: $ => repeat1(
      choice(
        seq($.nowiki_start, optional($.nowiki_content), choice($.bracket_end, $.bracket_implicit_end)),
        /[^{}\r\n]+/,
        token(prec(-10, '=')),
        token(prec(-10, '#')),
        /\r?\n/,
        '{',
        token(prec(-10, '}'))
      )
    ),
    nowiki_content_inline: $ => repeat1(
      choice(
        seq($.nowiki_start, optional($.nowiki_content_inline), choice($.bracket_end, $.bracket_implicit_end)),
        /[^{}=#\r\n]+/,
        token(prec(-10, '=')),
        token(prec(-10, '#')),
        '{',
        token(prec(-10, '}'))
      )
    ),

    escape: $ => token(/\\./),
    text: $ => prec(-1, choice(
      /[^\[\]{}=#\r\n]+/,
      token(prec(-10, '=')),
      token(prec(-10, '#')),
      /\r?\n/,
      '{',
      token(prec(-10, '}')),
      token(prec(-10, '[')),
      token(prec(-10, ']'))
    )),
    text_inline: $ => prec(-1, choice(
      /[^\[\]{}=#\r\n]+/,
      token(prec(-10, '#')),
      token(prec(-10, '=')),
      '{',
      token(prec(-10, '}')),
      token(prec(-10, '[')),
      token(prec(-10, ']'))
    )),
  }
});