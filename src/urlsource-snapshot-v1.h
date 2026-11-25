#ifndef URLSOURCE_SNAPSHOT_API_H
#define URLSOURCE_SNAPSHOT_API_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional ABI v1: resolve "urlsource_snapshot_v1" from obs-urlsource.
 * Return bytes required including NUL, or 0 when no snapshot is available.
 * NULL/short buffer: size query only, no partial write. Optional sequence
 * and age_ms refer to the same snapshot, zeroed when unavailable.
 * Copy is thread safe; no network, OBS calls or provider-owned memory.
 * Resolve/use only while the provider module is loaded.
 */
typedef size_t (*urlsource_snapshot_v1_fn)(const char *uuid, char *buffer, size_t capacity, uint64_t *sequence,
					   uint64_t *age_ms);
#define URLSOURCE_SNAPSHOT_MAX_BYTES (256u * 1024u)

#ifdef __cplusplus
}
#endif
#endif
