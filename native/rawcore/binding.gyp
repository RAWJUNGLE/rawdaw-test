{
  "targets": [
    {
      "target_name": "rawcore",
      "sources": [
        "src/addon.cpp",
        "src/bpm.cpp",
        "src/stretch.cpp",
        "vendor/beatdetektor/BeatDetektor.cpp"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "vendor/beatdetektor",
        "vendor/signalsmith-linear/include"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS",
        "RAWCORE_WITH_STRETCH"
      ],
      "cflags_cc": [ "-std=c++17", "-O2", "-fexceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "MACOSX_DEPLOYMENT_TARGET": "10.13",
        "OTHER_CPLUSPLUSFLAGS": [ "-O2" ]
      },
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1,
          "AdditionalOptions": [ "/std:c++17", "/O2", "/EHsc" ]
        }
      }
    }
  ]
}
