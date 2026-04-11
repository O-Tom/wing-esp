#ifndef TRACKED_DEVICES_H
#define TRACKED_DEVICES_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	const char *name;
	uint8_t mac[6];
} tracked_device_t;

size_t tracked_device_count(void);
const tracked_device_t *tracked_device_get(size_t index);
const char *tracked_device_name_from_mac(const uint8_t mac[6]);
void build_device_name(char *out, size_t out_len, const uint8_t mac[6]);

#endif
