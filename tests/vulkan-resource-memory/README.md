Compile actual shader-module creation, image-memory allocation and optional
pipeline-cache creation functions extracted from production. Include the real
memory-requirement/worker headers, logging and fail-fast result helpers, and
non-tracker allocation macros extracted from `vk_local.h`.

The 17 cases cover aligned/no-copy and deliberately misaligned SPIR-V, invalid
shader input, shader creation failure, mandatory dedicated image/buffer allocation,
ordinary pooled/legacy controls, checked allocation failure, asynchronous worker
exceptions/completion, enqueue copy-allocation failure, and optional cache failure
with/without an empty retry. Worker checks use a real thread and bounded future
wait; their allocation fault is a throwing callable copy, not global OOM.

Vulkan calls, renderer imports, image arenas and file I/O are service stubs. The
driver stub checks byte alignment and exact shader words, supplies dedicated
requirements, and poisons failed optional-cache output handles. `ri.Error`
throws to model its non-returning engine contract. These tests establish selected
control-flow contracts; they do not certify arbitrary OOM/device loss recovery,
real driver behavior or every texture decode/preparation branch.

Local x64/x86 Release and x64 MSVC ASan runs passed 17/17. Before-fix functions
from `eebb4d5` reproduced five failures (misaligned shader, failed shader creation,
dedicated image allocation, cache failure and cache retry failure); four matching
success controls passed. That before run substitutes only the three extracted
production functions; shared service/header scaffolding stays current. It does
not establish before-fix worker or buffer-helper outcomes.

Private evidence lives under `G:/code/JoF_EJK-performance-run/`:
`vulkan-followup-resource-{x64,x86,asan}/Testing/Temporary/LastTest.log`,
and `vulkan-followup-resource-before.log`.
