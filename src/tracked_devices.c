#include "tracked_devices.h"

#include <stdio.h>
#include <string.h>

#include "mac_addr.h"

// {Name, MAC address} pairs for devices we want to recognize by name in ARP scan results.
// Name up to 18 characters (+ null terminator) to fit within the character limit of the device name (see MAX_DEVICE_NAME_LEN).
static const tracked_device_t s_tracked_devices[] = {
	{"Wing",{0x01, 0x23, 0x45, 0x67, 0x89, 0xAB}},
};

#define TRACKED_DEVICE_COUNT (sizeof(s_tracked_devices) / sizeof(s_tracked_devices[0]))

size_t tracked_device_count(void)
{
	return TRACKED_DEVICE_COUNT;
}

const tracked_device_t *tracked_device_get(size_t index)
{
	if (index >= TRACKED_DEVICE_COUNT) {
		return NULL;
	}

	return &s_tracked_devices[index];
}

const char *tracked_device_name_from_mac(const uint8_t mac[6])
{
	if (mac == NULL) {
		return NULL;
	}

	for (size_t i = 0; i < TRACKED_DEVICE_COUNT; i++) {
		if (mac_addr_equal(mac, s_tracked_devices[i].mac)) {
			return s_tracked_devices[i].name;
		}
	}

	return NULL;
}

void build_device_name(char *out, size_t out_len, const uint8_t mac[6])
{
	const char *known_name = NULL;

	if (out == NULL || out_len == 0) {
		return;
	}

	known_name = tracked_device_name_from_mac(mac);
	if (known_name != NULL && known_name[0] != '\0') {
		strncpy(out, known_name, out_len - 1);
		out[out_len - 1] = '\0';
		return;
	}

	if (mac_addr_to_compact_str(mac, out, out_len)) {
		out[out_len - 1] = '\0';
		return;
	}

	strncpy(out, "unknown", out_len - 1);
	out[out_len - 1] = '\0';
}
