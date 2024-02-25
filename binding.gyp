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
        "-m",
        "-std=c++11",
        "-Wimplicit-function-declaration",
        "-x c++"
      ],
      "flags":["-m"],
      "cflags_cc":["-m","-Wimplicit-function-declaration"],
      "conditions": [
        ["OS=='win'", {
          "cflags!": ["-m","/EHs", "/EHc"],
          "cflags_cc": ["-m","/EHsc"],
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
