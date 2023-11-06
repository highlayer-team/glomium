{
  "targets": [
    {
      "target_name": "duktape_bindings",
      "sources": [
        "./fatal_handler.c",
        "./duktape/src-new/duktape.c",
        "./conversion_utils.cpp",
        "bindings.cpp"
      ],
      "include_dirs": [
        "./node_modules/node-addon-api",
        "./duktape/src-new",
        "./"
      ],
      "cflags": [
        "-std=c++11",
        "-x c++"
      ],
      "conditions": [
        ["OS=='win'", {
          "cflags!": ["/EHs", "/EHc"],
          "cflags_cc": ["/EHsc"],
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": "1"
            }
          }
        }],
        ["OS=='mac' or OS=='linux'", {}]
      ]
    }
  ]
}
