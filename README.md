### Description
Transitively extract source code from a codebase written in C into a single (continuous) standalone file.

see [paper](https://people.csail.mit.edu/stelios/papers/codecarboncopy.pdf)

### Usage
There are two steps. The first step is to "collect" information about the given codebase. This is accomplished through a clang plugin. Important: that information is specific to *the* build (e.g. the host machine's architecture, any preprocessor definitions specified on the command-line, etc).

```bash
CFLAGS += -Xclang -load -Xclang /path/to/libcarbon-collect.so \
          -Xclang -add-plugin -Xclang carbon-collect \
          -Xclang -plugin-arg-carbon-collect -Xclang /path/to/source \
          -Xclang -plugin-arg-carbon-collect -Xclang /path/to/build
```

After compiling, the build directory should contain a directory named `.carbon`. That is the result of the collect step. The second step is to make use of it with `carbon-extract`.

```bash
# extract the top-level element at line number 123 (could be a function, or struct, or typedef, etc.)
carbon-extract relative/path/to/source/file.c:123l
```
