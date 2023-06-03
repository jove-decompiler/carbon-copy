# Description
Transitively extract source code from a codebase written in C into a single (continuous) standalone file. _[paper](https://people.csail.mit.edu/stelios/papers/codecarboncopy.pdf)_

# Usage
There are two steps. The first step is to "collect" information about the given codebase. This is accomplished through a clang plugin.

```bash
CFLAGS += -Xclang -load -Xclang /path/to/libcarbon-collect.so \
          -Xclang -add-plugin -Xclang carbon-collect \
          -Xclang -plugin-arg-carbon-collect -Xclang /path/to/source \
          -Xclang -plugin-arg-carbon-collect -Xclang /path/to/build
```
After compiling, the build directory should contain a directory named `.carbon`. That is the (serialized) result of the collect step. The second step is to make use of it with `carbon-extract`
```bash
# extract the top-level element at line number 123 (could be a function, or struct, or typedef, etc.)
carbon-extract relative/path/to/source/file.c:123l
```
Note that the resulting view of the codebase is specific to the build (chosen configuration, the host machine's architecture, etc), as it occurs during compilation (after the preprocessing step, although the output is *not* preprocessed). Having this "dynamic" view of the codebase is what makes the extraction step straightforward (and correct).
## Building
You should be able to use your distro's clang package as-is.
```bash
cd carbon-copy/
mkdir build
cd build/
cmake -G Ninja -D CMAKE_BUILD_TYPE=RelWithDebInfo ..
ninja
```
