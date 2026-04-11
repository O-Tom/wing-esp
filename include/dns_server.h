#pragma once

#include <stdint.h>

// Task function for the DNS server
// arg must be a pointer to a DNSServerTaskContext
typedef struct {
    const uint32_t *redirect_ip_be; // pointer to redirect target in network byte order
} DNSServerTaskContext;

void dns_server_task(void *arg);
