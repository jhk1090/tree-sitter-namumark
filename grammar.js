/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

const requiredArgMacros = ["anchor", "age", "dday", "youtube", "kakaotv", "vimeo", "nicovideo", "navertv", "math", "include", "ruby"]
const optionalArgMacros = ["pagecount", "목차", "tableofcontents"]
const noArgMacros = ["clearfix", "date", "datetime", "각주", "footnote", "br"]

/// @ts-ignore
function fallback_rule($, start_rule) {
  return seq(
    start_rule,
    optional(field('content', $.nowiki_content)),
    $._block_end
  )
}

/// @ts-ignore
function fallback_inline_rule($, start_rule) {
  return seq(
    start_rule,
    optional(field('content', $.nowiki_content_inline)),
    $._block_end
  )
}

/// @ts-ignore
function heading_rule($, start_rule) {
  return seq(
    alias(start_rule, $.heading_marker),
    field("title", repeat1($._node_inline)),
    alias($.heading_end, $.heading_marker),
    /\r?\n/,
  )
}

module.exports = grammar({
  name: 'namumark',

  externals: $ => [
    $.comment,
    $.argument_start,
    $.argument_end,
    $.argument_implicit_end,
    $.nowiki_start,
    $.if_block_start,
    $.if_fallback_block_start,
    $.folding_block_start,
    $.folding_fallback_block_start,
    $.syntax_block_start,
    $.syntax_fallback_block_start,
    $.wiki_block_start,
    $.wiki_fallback_block_start,
    $.html_block_start,
    $.html_fallback_block_start,
    $.style_block_start,
    $.style_fallback_block_start,
    $.latex_block_start,
    $.latex_fallback_block_start,
    $.scale_block_start,
    $.scale_fallback_block_start,
    $.color_block_start,
    $.color_fallback_block_start,
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
    $.link_start,
    $.link_pipe,
    $.link_end,
    $.link_implicit_end,
    $.bold_start,
    $.bold_end,
    $.bold_implicit_end,
    $.italic_start,
    $.italic_end,
    $.italic_implicit_end,
    $.strike_start,
    $.strike_end,
    $.strike_implicit_end,
    $.strike2_start,
    $.strike2_end,
    $.strike2_implicit_end,
    $.superscript_start,
    $.superscript_end,
    $.superscript_implicit_end,
    $.subscript_start,
    $.subscript_end,
    $.subscript_implicit_end,
    $.underline_start,
    $.underline_end,
    $.underline_implicit_end,
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
    heading1: ($) => heading_rule($, $.heading1_start),
    heading2: ($) => heading_rule($, $.heading2_start),
    heading3: ($) => heading_rule($, $.heading3_start),
    heading4: ($) => heading_rule($, $.heading4_start),
    heading5: ($) => heading_rule($, $.heading5_start),
    heading6: ($) => heading_rule($, $.heading6_start),
    hidden_heading1: ($) => heading_rule($, $.hidden_heading1_start),
    hidden_heading2: ($) => heading_rule($, $.hidden_heading2_start),
    hidden_heading3: ($) => heading_rule($, $.hidden_heading3_start),
    hidden_heading4: ($) => heading_rule($, $.hidden_heading4_start),
    hidden_heading5: ($) => heading_rule($, $.hidden_heading5_start),
    hidden_heading6: ($) => heading_rule($, $.hidden_heading6_start),

    _node: $ => choice(
      $.comment,
      $._block,
      $._fallback_block,
      $.argument,
      $.escape,
      $.text,
      $.macro,
      $.macro_without_args,
      $.link,
      $.bold,
      $.italic,
      $.strike,
      $.strike2,
      $.superscript,
      $.subscript,
      $.underline,
    ),
    _node_inline: $ => choice(
      $._block_inline,
      $._fallback_block_inline,
      $.argument,
      $.escape,
      $.text_inline,
      $.macro_inline,
      $.macro_without_args,
      $.link_inline,
      $.bold_inline,
      $.italic_inline,
      $.strike_inline,
      $.strike2_inline,
      $.superscript_inline,
      $.subscript_inline,
      $.underline_inline,
    ),

    _block: $ => choice(
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
    ),
    _block_inline: $ => choice(
      $.nowiki_inline,
      $.html_block_inline,
      $.scale_block_inline,
      $.color_block_inline,
    ),

    _block_end: $ => choice(
      $.bracket_end,
      $.bracket_implicit_end
    ),

    if_block: $ => seq(
      $.if_block_start,
      "#!if",
      optional(field('expression', $.if_block_header)),
      token.immediate(/\r?\n/),
      optional($.block_content),
      $._block_end
    ),
    if_block_header: $ => repeat1(/[^\r\n]+/),

    folding_block: $ => seq(
      $.folding_block_start,
      "#!folding",
      optional(field('text', $.block_header)),
      token.immediate(/\r?\n/),
      optional($.block_content),
      $._block_end
    ),

    syntax_block: $ => seq(
      $.syntax_block_start,
      "#!syntax",
      field('language', $.block_header),
      token.immediate(/\r?\n/),
      optional(field('content', $.nowiki_content)),
      $._block_end
    ),

    html_block: $ => seq(
      $.html_block_start,
      "#!html",
      optional(field('content', $.nowiki_content)),
      $._block_end
    ),
    html_block_inline: $ => seq(
      $.html_block_start,
      "#!html",
      optional(field('content', $.nowiki_content_inline)),
      $._block_end
    ),

    style_block: $ => seq(
      $.style_block_start,
      "#!style",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      $._block_end
    ),

    latex_block: $ => seq(
      $.latex_block_start,
      "#!latex",
      /\r?\n/,
      optional(field('content', $.nowiki_content)),
      $._block_end
    ),

    wiki_block: $ => seq(
      $.wiki_block_start,
      "#!wiki",
      optional($.wiki_block_header),
      token.immediate(/\r?\n/),
      optional($.block_content),
      $._block_end
    ),
    wiki_block_header: ($) => repeat1(choice(
      $._wiki_attribute,
      prec(1, token(/[^\r\n@= ]+/)),
      prec(1, $.argument),
      prec(-10, token("@")),
      prec(-10, token("=")),
      prec(-10, token(" ")),
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
    wiki_attr_value: ($) => prec.right(repeat1(
      choice(
        token(prec(2, /[^\r\n"@=]+/)),
        prec(-10, token("=")),
        prec(1, $.argument),
        prec(-10, token("@"))
      )
    )),
    wiki_style_attr: ($) => prec.right(seq('style="', optional(field("value", $.wiki_attr_value)), optional(field("end", token(prec(2, '"')))))),
    wiki_dark_style_attr: ($) => prec.right(seq('dark-style="', optional(field("value", $.wiki_attr_value)), optional(field("end", token(prec(2, '"')))))),
    wiki_tag_attr: ($) => prec.right(seq('tag="', optional(field("value", $.wiki_attr_value)), optional(field("end", token(prec(2, '"')))))),
    wiki_class_attr: ($) => prec.right(seq('class="', optional(field("value", $.wiki_attr_value)), optional(field("end", token(prec(2, '"')))))),
    wiki_lang_attr: ($) => prec.right(seq('lang="', optional(field("value", $.wiki_attr_value)), optional(field("end", token(prec(2, '"')))))),

    block_header: $ => repeat1(
      choice(
        prec(1, token(/[^\r\n@=]+/)),
        prec(-10, token("=")),
        prec(1, $.argument),
        prec(-10, token("@"))
      )
    ),
    block_content: $ => repeat1($._node),
    block_content_inline: $ => repeat1($._node_inline),

    scale_block: $ => seq(
      $.scale_block_start,
      field("size", $.scale_size),
      /\r?\n| /,
      optional($.block_content),
      $._block_end
    ),
    scale_block_inline: $ => seq(
      $.scale_block_start,
      field("size", $.scale_size),
      " ",
      optional($.block_content_inline),
      $._block_end
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
      $._block_end
    ),
    color_block_inline: $ => seq(
      $.color_block_start,
      field('color1', $.color_color),
      optional(
        seq(",", field('color2', $.color_color))
      ),
      " ",
      optional($.block_content_inline),
      $._block_end
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
          /[^=)}\\@]+/,
          $.escape,
          $.argument,
          prec(-10, token('=')),
          prec(-10, token(')')),
          prec(-10, token('}')),
          prec(-10, token('@')),
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
          /[^=\r\n)\\@]+/,
          $.escape,
          $.argument,
          prec(-10, token('=')),
          prec(-10, token(')')),
          prec(-10, token('@')),
        )
      ),
      choice(
        $.macro_end,
        $.macro_implicit_end
      )
    ),
    
    link: $ => seq(
      $.link_start,
      optional(field('link', $.link_link)),
      optional(seq($.link_pipe, optional(field('display', $.link_display)))),
      choice(
        $.link_end,
        $.link_implicit_end
      )
    ),
    link_inline: $ => seq(
      $.link_start,
      optional(field('link', $.link_link_inline)),
      optional(seq($.link_pipe, optional(field('display', $.link_display_inline)))),
      choice(
        $.link_end,
        $.link_implicit_end
      )
    ),
    link_link: $ => repeat1(
      choice(
        /[^\]\|\}\r\n\\@]+/,
        $.escape,
        $.argument,
        prec(-10, token(']')),
        prec(-10, token('}')),
        prec(-10, token('@')),
      )
    ),
    link_link_inline: $ => repeat1(
      choice(
        /[^\]\|\}\=\r\n\\@]+/,
        $.escape,
        $.argument,
        prec(-10, token(']')),
        prec(-10, token('}')),
        prec(-10, token('=')),
        prec(-10, token('@')),
      )
    ),
    link_display: $ => repeat1($._node),
    link_display_inline: $ => repeat1($._node_inline),

    bold: $ => seq($.bold_start, optional(field('content', $.bold_content)), choice($.bold_end, $.bold_implicit_end)),
    bold_inline: $ => seq($.bold_start, optional(field('content', $.bold_content_inline)), choice($.bold_end, $.bold_implicit_end)),
    bold_content: $ => repeat1($._node),
    bold_content_inline: $ => repeat1($._node_inline),

    italic: $ => seq($.italic_start, optional(field('content', $.italic_content)), choice($.italic_end, $.italic_implicit_end)),
    italic_inline: $ => seq($.italic_start, optional(field('content', $.italic_content_inline)), choice($.italic_end, $.italic_implicit_end)),
    italic_content: $ => repeat1($._node),
    italic_content_inline: $ => repeat1($._node_inline),

    strike: $ => seq($.strike_start, optional(field('content', $.strike_content)), choice($.strike_end, $.strike_implicit_end)),
    strike_inline: $ => seq($.strike_start, optional(field('content', $.strike_content_inline)), choice($.strike_end, $.strike_implicit_end)),
    strike_content: $ => repeat1($._node),
    strike_content_inline: $ => repeat1($._node_inline),

    strike2: $ => seq($.strike2_start, optional(field('content', $.strike2_content)), choice($.strike2_end, $.strike2_implicit_end)),
    strike2_inline: $ => seq($.strike2_start, optional(field('content', $.strike2_content_inline)), choice($.strike2_end, $.strike2_implicit_end)),
    strike2_content: $ => repeat1($._node),
    strike2_content_inline: $ => repeat1($._node_inline),

    superscript: $ => seq($.superscript_start, optional(field('content', $.superscript_content)), choice($.superscript_end, $.superscript_implicit_end)),
    superscript_inline: $ => seq($.superscript_start, optional(field('content', $.superscript_content_inline)), choice($.superscript_end, $.superscript_implicit_end)),
    superscript_content: $ => repeat1($._node),
    superscript_content_inline: $ => repeat1($._node_inline),
    
    subscript: $ => seq($.subscript_start, optional(field('content', $.subscript_content)), choice($.subscript_end, $.subscript_implicit_end)),
    subscript_inline: $ => seq($.subscript_start, optional(field('content', $.subscript_content_inline)), choice($.subscript_end, $.subscript_implicit_end)),
    subscript_content: $ => repeat1($._node),
    subscript_content_inline: $ => repeat1($._node_inline),
    
    underline: $ => seq($.underline_start, optional(field('content', $.underline_content)), choice($.underline_end, $.underline_implicit_end)),
    underline_inline: $ => seq($.underline_start, optional(field('content', $.underline_content_inline)), choice($.underline_end, $.underline_implicit_end)),
    underline_content: $ => repeat1($._node),
    underline_content_inline: $ => repeat1($._node_inline),

    _fallback_block: $ => choice(
      $.if_fallback_block,
      $.folding_fallback_block,
      $.syntax_fallback_block,
      $.wiki_fallback_block,
      $.style_fallback_block,
      $.latex_fallback_block,
      $.scale_fallback_block,
      $.color_fallback_block
    ),

    if_fallback_block: $ => fallback_rule($, $.if_fallback_block_start),
    folding_fallback_block: $ => fallback_rule($, $.folding_fallback_block_start),
    syntax_fallback_block: $ => fallback_rule($, $.syntax_fallback_block_start),
    wiki_fallback_block: $ => fallback_rule($, $.wiki_fallback_block_start),
    style_fallback_block: $ => fallback_rule($, $.style_fallback_block_start),
    latex_fallback_block: $ => fallback_rule($, $.latex_fallback_block_start),
    scale_fallback_block: $ => fallback_rule($, $.scale_fallback_block_start),
    color_fallback_block: $ => fallback_rule($, $.color_fallback_block_start),

    _fallback_block_inline: $ => choice(
      $.if_fallback_block,
      $.folding_fallback_block,
      $.syntax_fallback_block,
      $.wiki_fallback_block,
      $.style_fallback_block,
      $.latex_fallback_block,
      $.scale_fallback_block,
      $.color_fallback_block
    ),

    if_fallback_block_inline: $ => fallback_inline_rule($, $.if_fallback_block_start),
    folding_fallback_block_inline: $ => fallback_inline_rule($, $.folding_fallback_block_start),
    syntax_fallback_block_inline: $ => fallback_inline_rule($, $.syntax_fallback_block_start),
    wiki_fallback_block_inline: $ => fallback_inline_rule($, $.wiki_fallback_block_start),
    style_fallback_block_inline: $ => fallback_inline_rule($, $.style_fallback_block_start),
    latex_fallback_block_inline: $ => fallback_inline_rule($, $.latex_fallback_block_start),
    scale_fallback_block_inline: $ => fallback_inline_rule($, $.scale_fallback_block_start),
    color_fallback_block_inline: $ => fallback_inline_rule($, $.color_fallback_block_start),

    nowiki: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content)),
      $._block_end
    ),
    nowiki_inline: $ => seq(
      $.nowiki_start,
      optional(field('content', $.nowiki_content_inline)),
      $._block_end
    ),

    nowiki_content: $ => repeat1(
      choice(
        seq($.nowiki_start, optional($.nowiki_content), $._block_end),
        /[^{}=#\r\n@]+/,
        prec(-10, token('=')),
        prec(-10, token('#')),
        prec(-10, token(/\r?\n/)),
        prec(-10, token('{')),
        prec(-10, token('}')),
        prec(-10, token('@')),
        prec(1, $.argument)
      )
    ),
    nowiki_content_inline: $ => repeat1(
      choice(
        seq($.nowiki_start, optional($.nowiki_content_inline), $._block_end),
        /[^{}=#\r\n@]+/,
        prec(-10, token('=')),
        prec(-10, token('#')),
        prec(-10, token('{')),
        prec(-10, token('}')),
        prec(-10, token('@')),
        prec(1, $.argument)
      )
    ),

    argument: $ => prec.right(seq(
      $.argument_start,
      field('name', $.argument_name),
      optional(
        seq("=", optional(
          field('value', $.argument_value)
        ))
      ),
      choice(
        $.argument_end,
        $.argument_implicit_end
      )
    )),
    argument_name: $ => token(/[\p{L}\p{N}\p{M}_]+/u),
    argument_value: $ => repeat1(
      choice(
        /[^\n\r@})\]\|'\-\~\_\^\,]+/,
        prec(-10, token('}')),
        prec(-10, token(')')),
        prec(-10, token(']')),
        prec(-10, token('|')),
        prec(-10, token('\'')),
        prec(-10, token('-')),
        prec(-10, token('~')),
        prec(-10, token('_')),
        prec(-10, token('^')),
        prec(-10, token(',')),
      )
    ),

    escape: $ => token(/\\./),
    text: $ => prec(-1, choice(
      /[^\[\]{}=#\r\n\\@'\-\~\_\^\,]+/,
      prec(-10, token('=')),
      prec(-10, token('#')),
      prec(-10, token(/\r?\n/)),
      prec(-10, token('{')),
      prec(-10, token('}')),
      prec(-10, token('[')),
      prec(-10, token(']')),
      prec(-10, token('\\')),
      prec(-10, token('@')),
      prec(-10, token('\'')),
      prec(-10, token('-')),
      prec(-10, token('~')),
      prec(-10, token('_')),
      prec(-10, token('^')),
      prec(-10, token(',')),
    )),
    text_inline: $ => prec(-1, choice(
      /[^\[\]{}=#\r\n\\@'\-\~\_\^\,]+/,
      prec(-10, token('#')),
      prec(-10, token('=')),
      prec(-10, token('{')),
      prec(-10, token('}')),
      prec(-10, token('[')),
      prec(-10, token(']')),
      prec(-10, token('\\')),
      prec(-10, token('@')),
      prec(-10, token('\'')),
      prec(-10, token('-')),
      prec(-10, token('~')),
      prec(-10, token('_')),
      prec(-10, token('^')),
      prec(-10, token(',')),
    )),
  }
});