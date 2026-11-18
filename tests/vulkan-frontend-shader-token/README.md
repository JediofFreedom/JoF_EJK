# Vulkan surface-sprite shader token regression

This suite extracts the actual optional `ss*` branch from `ParseStage`, compiling
its statements verbatim inside a small loop wrapper. Tokenization and optional
surface-sprite interpretation are stubbed. Tokens through the production COM
token limit exercise the former 128-byte stack-copy overflow. Valid parameter,
argument and existing/new stage accounting remains covered.

The pre-fix branch from `54384fc` reports an AddressSanitizer stack-buffer
overflow. The bounded production copy passes x64/x86 AddressSanitizer. Unknown
long names are passed to the existing unknown-option handling after truncation;
recognized option names are unchanged.

```powershell
cmake -S tests/vulkan-frontend-shader-token -B build/vulkan-frontend-shader-token
cmake --build build/vulkan-frontend-shader-token --config Release
ctest --test-dir build/vulkan-frontend-shader-token -C Release --output-on-failure
```
