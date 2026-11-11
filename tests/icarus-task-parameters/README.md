This standalone regression extracts the production Icarus parameter readers,
their token/task-status enums, and the `Move` command. Fixtures supply block
members and engine callbacks so the tests exercise actual reader control flow.

`Get` and `GetVector` report boolean success, whereas task commands use
`TASK_OK == 0` and `TASK_FAILED == 1`. Returning `TASK_FAILED` from a parameter
reader therefore reported success. The suite verifies failed nested `get()` and
`tag()` calls, failed tag lookup, invalid vector components and wrong-type
vector requests. It also verifies successful literals/inline expressions and
that valid movement still uses task status codes and reaches its callback.
Failed movement parameters must not reach that callback.

Both x64 AddressSanitizer and x86 Release pass. Selecting the pre-fix production
source from commit `6955dca` with `-DICARUS_TASK_SOURCE=/path/to/TaskManager.cpp`
builds successfully but fails the first failed-nested-tag assertion, confirming
that the regression exercises the original bug. Assertions inside production
error-reporting paths are disabled to match release-engine behavior.

```
cmake -S tests/icarus-task-parameters -B build/icarus-task-parameters
cmake --build build/icarus-task-parameters
ctest --test-dir build/icarus-task-parameters --output-on-failure
```

These fixtures cover parameter failure propagation; truncated binary block
streams and parser bounds require separate validation.
