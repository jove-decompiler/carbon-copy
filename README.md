# clang-extricate
C code extractor from CodeCarbonCopy

# Usage
When compiling,

```bash
CFLAGS += -Xclang -load -Xclang /usr/local/lib/carbon-collect.so \
          -Xclang -add-plugin -Xclang carbon-collect
```

Afterwards,

```bash
Usage: carbon-extract [options] code...
Allowed options:
  -h [ --help ]                         produce help message
  -o [ --out ] arg                      specify output file path
  -d [ --dir ] arg (="/home/aeden/_/clang-extricate")
                                        specify root source directory where
                                        code exists
  -v [ --verbose ] arg (=0)             enable verbosity (optionally specify
                                        level)
  -c [ --code ] arg                     specify source code to extract. the
                                        format of this argument is:
                                        [relative source file path]:[line
                                        number]l
                                        [relative source file path]:[byte
                                        offset]o
                                        [global symbol]
  -f [ --from ] arg                     specify an additional source file from
                                        which to extract code from (thisoption
                                        is overrided by --from-all)
  --debug                               extract code with comments from whence
                                        it came
  -a [ --from-all ]                     extract code from all known source
                                        files
  -t [ --only-types ]                   only extract types
  -g [ --graphviz ]                     output graphviz file
  -s [ --sys-code ]                     inline code from system header files
```
