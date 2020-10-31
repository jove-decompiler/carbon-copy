# clang-extricate
C code extractor from CodeCarbonCopy, used with jove.

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
e.g.
```bash
carbon-extract tcg/tcg-op.c:1243l
```

# Examples

## linux kernel
Make the following changes
```diff
diff --git a/Makefile b/Makefile
index 7280fc69f039..fe6c2c729d1e 100644
--- a/Makefile
+++ b/Makefile
@@ -530,6 +530,7 @@ CLANG_FLAGS += --gcc-toolchain=$(GCC_TOOLCHAIN)
 endif
 CLANG_FLAGS    += -no-integrated-as
 CLANG_FLAGS    += -Werror=unknown-warning-option
+CLANG_FLAGS     += -Xclang -load -Xclang /usr/local/lib/carbon-collect.so -Xclang -add-plugin -Xclang carbon-collect -Xclang -plugin-arg-carbon-collect -Xclang /home/aeden/linux -Xclang -plugin-arg-carbon-collect -Xclang /home/aeden/linux
 KBUILD_CFLAGS  += $(CLANG_FLAGS)
 KBUILD_AFLAGS  += $(CLANG_FLAGS)
 export CLANG_FLAGS
```
Then run the usual
```bash
cd linux
make CC=clang x86_64_defconfig
rm -rf .carbon
make CC=clang all -j$(nproc)
```
