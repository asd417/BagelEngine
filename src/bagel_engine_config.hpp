#pragma once

// Engine-wide tunable limits. One home for the magic counts that used to be
// re-#defined per translation unit. Several mirror GPU-side array sizes / descriptor
// budgets — keep them in sync with the matching shader constants when you change them.

#define GLOBAL_DESCRIPTOR_COUNT 1000 // bindless descriptor table size
#define GLOBAL_UBO_COUNT        10   // global UBO slots in the descriptor pool
#define MAX_LIGHTS              10   // point lights uploaded per frame (mirrors the shader)
#define MAX_TRANSFORM_PER_ENT  1000 // capacity of TransformArrayComponent's fixed arrays
