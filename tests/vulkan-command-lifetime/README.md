# Vulkan single-use command lifetime

Compiles the production one-shot allocate/begin/end/submit/wait functions and
normal backend queue wait. The actual logging-only result helper remains active;
explicit failure checks must prevent invalid continuation. Fault-injected driver
states cover allocation, recording begin/end, submission OOM/device loss and
unsuccessful completion. The success control preserves one allocation, one
submission/wait and one free.

Never-submitted buffers are freed. Submission OOM leaves state unchanged under
the Vulkan contract and permits freeing. A lost device or unsuccessful queue
wait can leave pending work; that buffer remains owned by its command pool for
device teardown. These tests do not establish complete global OOM recovery or
safe renderer teardown on every driver failure.

Set COMMAND_SOURCE_DIR to a pre-fix snapshot to reproduce seven failed error
paths alongside the passing success control. Real GPU integration is separate.
