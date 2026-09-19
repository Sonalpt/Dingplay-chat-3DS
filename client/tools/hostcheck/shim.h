// Host-side syntax-check shims for newlib-only types used by libctru headers.
#include <stdint.h>
#include <sys/types.h>
typedef int32_t _LOCK_T;
typedef struct { int32_t lock; uint32_t thread_tag; uint32_t counter; } _LOCK_RECURSIVE_T;
#define APP_VERSION "0.1.0"
#define APP_UNIQUE_ID 0xD1A6
