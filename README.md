# Description
> To preserve the source-level structure and promote readability, CCC transfers all code verbatim (i.e., it does not preprocess the transferred code). Starting with the identified function to transfer, the code extractor (transitively) traces out compile- and run-time dependences to build a compile-time dependence graph. The nodes in this graph are code elements (type declarations, function declarations, and potentially invoked functions). The edges model compile-time dependences — there is an edge between two code elements if the compiler must process the first code element before the second for the second to compile successfully. The extractor topologically sorts this graph and, with the exception of system code elements from standard include files or system libraries, emits the code elements in the topological sort order. Instead of emitting code elements from standard include files, it emits code that includes the include file. It expects code from system libraries to be linked into the final executable.

[CCC paper](https://people.csail.mit.edu/stelios/papers/codecarboncopy.pdf)

# Usage
When compiling,

```bash
CFLAGS += -Xclang -load -Xclang /usr/local/lib/carbon-collect.so \
          -Xclang -add-plugin -Xclang carbon-collect
```

After compiling, the build directory should contain a .carbon folder.  To use the extractor:

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
make CC=clang x86_64_defconfig
rm -rf .carbon
make CC=clang all -j$(nproc)
```
Afterwards,
```bash
cd linux
carbon-extract sys_write
```
