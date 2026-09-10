{
  "targets": [
    {
      "target_name": "tree_sitter_mylang_binding",
      "dependencies": [
        "<!(node -p \"require('node-addon-api').targets\")"
      ],
      "include_dirs": [
        "src"
      ],
      "sources": [
        "src/parser.c",
        "src/scanner.c"
      ],
      "cflags_c": [
        "-std=c11"
      ]
    }
  ]
}