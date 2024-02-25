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
      "include":[
        "./duktape/src-input/duk_hthread.h"
      ],
      "cflags": [
        "-std=c++11",
        "-Wimplicit-function-declaration",
      ],
      "cflags_cc":["-Wimplicit-function-declaration"],
      "conditions": [
        ["OS=='win'", {
          "cflags!": ["/EHs", "/EHc"],
          "cflags_cc": ["/EHsc"],
          "msvs_settings": {
            "ParallelBuild":"1",
            "VCCLCompilerTool": {
              "ExceptionHandling": "1"
            }
          }
        }],
      ]
    }
  ]
}
