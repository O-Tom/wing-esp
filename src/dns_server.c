#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "dns_server.h"
#include "wing_esp.h"

// =====================================================================
// Internal helpers
// =====================================================================

static bool dns_name_matches(const char *qname)
{
	static const char *allow_list[] = {
		"connectivitycheck.gstatic.com",
		"clients3.google.com",
		"captive.apple.com",
		"www.apple.com",
		"gs.apple.com",
	};

	for (size_t i = 0; i < sizeof(allow_list) / sizeof(allow_list[0]); i++) {
		if (strcasecmp(qname, allow_list[i]) == 0) {
			return true;
		}
	}

	return false;
}

static int parse_qname(const uint8_t *buf, size_t len, size_t start, char *out, size_t out_len, size_t *q_end)
{
	size_t p = start;
	size_t out_p = 0;

	if (len < 12 || out_len == 0) {
		return -1;
	}

	while (p < len) {
		uint8_t label_len = buf[p++];

		if (label_len == 0) {
			if (out_p == 0) {
				return -1;
			}
			out[out_p] = '\0';
			if ((p + 4) > len) {
				return -1;
			}
			*q_end = p + 4;
			return 0;
		}

		if (label_len & 0xC0) {
			return -1;
		}

		if ((p + label_len) > len) {
			return -1;
		}

		if (out_p != 0) {
			if (out_p + 1 >= out_len) {
				return -1;
			}
			out[out_p++] = '.';
		}

		if (out_p + label_len >= out_len) {
			return -1;
		}

		memcpy(&out[out_p], &buf[p], label_len);
		out_p += label_len;
		p += label_len;
	}

	return -1;
}

static size_t build_dns_response_a(const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_len, uint32_t ip_be)
{
	if (req_len < 12 || resp_len < (req_len + 16)) {
		return 0;
	}

	memcpy(resp, req, req_len);

	// QR=1, RD copied from request, RA=1, RCODE=0
	resp[2] = (uint8_t)((req[2] & 0x01) ? 0x81 : 0x80);
	resp[3] = 0x80;
	resp[4] = 0x00;
	resp[5] = 0x01;
	resp[6] = 0x00;
	resp[7] = 0x01;
	resp[8] = 0x00;
	resp[9] = 0x00;
	resp[10] = 0x00;
	resp[11] = 0x00;

	size_t p = req_len;
	resp[p++] = 0xC0;
	resp[p++] = 0x0C;
	resp[p++] = 0x00;
	resp[p++] = 0x01;
	resp[p++] = 0x00;
	resp[p++] = 0x01;
	resp[p++] = 0x00;
	resp[p++] = 0x00;
	resp[p++] = 0x00;
	resp[p++] = 0x3C;
	resp[p++] = 0x00;
	resp[p++] = 0x04;

	memcpy(&resp[p], &ip_be, 4);
	p += 4;

	return p;
}

// =====================================================================
// Public API
// =====================================================================

void dns_server_task(void *arg)
{
	DNSServerTaskContext *ctx = (DNSServerTaskContext *)arg;

	if (ctx == NULL) {
		ESP_LOGE(LOG_TAG, "DNS server task: invalid context");
		vTaskDelete(NULL);
		return;
	}

	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		ESP_LOGE(LOG_TAG, "DNS socket create failed");
		vTaskDelete(NULL);
		return;
	}

	struct sockaddr_in bind_addr = {
		.sin_family = AF_INET,
		.sin_port = htons(DNS_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
		ESP_LOGE(LOG_TAG, "DNS bind failed");
		close(sock);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(LOG_TAG, "DNS server started on UDP/%d", DNS_PORT);

	while (true) {
		uint8_t req[DNS_BUF_SIZE] = {0};
		uint8_t resp[DNS_BUF_SIZE] = {0};
		struct sockaddr_in from = {0};
		socklen_t from_len = sizeof(from);

		int rx = recvfrom(sock, req, sizeof(req), 0, (struct sockaddr *)&from, &from_len);
		if (rx <= 0) {
			continue;
		}

		char qname[256] = {0};
		size_t q_end = 0;
		if (parse_qname(req, (size_t)rx, 12, qname, sizeof(qname), &q_end) != 0) {
			continue;
		}

		uint16_t qtype  = (uint16_t)((req[q_end - 4] << 8) | req[q_end - 3]);
		uint16_t qclass = (uint16_t)((req[q_end - 2] << 8) | req[q_end - 1]);

		bool match = DNS_CAPTURE_ALL ? true : dns_name_matches(qname);

		if (qtype == 1 && qclass == 1 && match) {
			ESP_LOGI(LOG_TAG, "DNS match: %s -> %s", qname, ETH_IP_ADDR);
			size_t tx_len = build_dns_response_a(req, (size_t)rx, resp, sizeof(resp), *ctx->redirect_ip_be);
			if (tx_len > 0) {
				sendto(sock, resp, tx_len, 0, (struct sockaddr *)&from, from_len);
			}
		}
	}
}
