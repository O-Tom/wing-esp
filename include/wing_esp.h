#pragma once

// =============================================================================
// WingESP - central configuration
// =============================================================================

// --- String conversion helpers ------------------------------------------------

// Stringify: convert a preprocessor token to a string literal.
// Indirection is needed to ensure macro arguments are fully expanded first.
#define _STRINGIFY(x)  #x
#define _XSTRINGIFY(x) _STRINGIFY(x)

// Prefer compiler-provided basename when available (e.g. main.c);
// fall back to __FILE__ if unavailable.
#if defined(__FILE_NAME__)
#define LOG_TAG __FILE_NAME__
#else
#define LOG_TAG __FILE__
#endif

// --- Network identity ---------------------------------------------------------

#define SUBNET_PREFIX                   "192.168.1."        // network prefix for device and DHCP pool
#define ETH_IP_OCTET                    50                  // device's last octet on the subnet
#define ETH_IP_ADDR                     SUBNET_PREFIX _XSTRINGIFY(ETH_IP_OCTET)
#define ETH_NETMASK                     "255.255.255.0"     // /24 subnet
#define ETH_GATEWAY                     ETH_IP_ADDR         // advertised to DHCP clients; defaults to device IP
#define ETH_UPSTREAM_FALLBACK_GATEWAY   "192.168.1.1"       // used when the device itself needs to reach the WAN

// --- DHCP server pool ---------------------------------------------------------

#define DHCP_RANGE_ROOT                 SUBNET_PREFIX       // reuses the subnet prefix above
#define DHCP_RANGE_START_OCTET          51                  // first assignable address (.51)
#define DHCP_RANGE_END_OCTET            70                  // last assignable address (.70)

// Assembled from the pieces above; adjacent string literals are concatenated by the compiler.
#define DHCP_RANGE_START                DHCP_RANGE_ROOT _XSTRINGIFY(DHCP_RANGE_START_OCTET)
#define DHCP_RANGE_END                  DHCP_RANGE_ROOT _XSTRINGIFY(DHCP_RANGE_END_OCTET)

#define DHCP_LEASE_TIME_SECONDS         60                  // lease duration handed out to clients (seconds)

// --- DHCP server startup ------------------------------------------------------

#define DHCPS_START_RETRY_COUNT         3                   // retries if DHCP server fails to start
#define DHCPS_START_RETRY_DELAY_MS      250                 // delay between startup retries (ms)

// --- ARP pool (derived from the DHCP range) -----------------------------------

#define ARP_POOL_SIZE                   (DHCP_RANGE_END_OCTET - DHCP_RANGE_START_OCTET + 1) // computed from pool
#define ARP_PRE_DHCP_REQUEST_SPACING_MS 20                  // inter-probe spacing before startup sweep (ms)
#define ARP_PRE_DHCP_SETTLE_MS          400                 // settle time after pre-DHCP sweep (ms)

// --- ARP runtime scanning -----------------------------------------------------

#define ARP_RUNTIME_SCAN_INTERVAL_MS    5000                // periodic device discovery interval (ms)
#define ARP_DEVICE_GUARD_TIME_MS        30000               // grace period before marking device inactive (ms)
#define ARP_FLUSH_CACHE_BEFORE_SWEEP    1                   // clear lwIP ARP cache before each scan

// --- ARP scan results ---

#define MAX_DEVICE_NAME_LEN 21                              // fits device names and IP octet + name

typedef struct ARPScanResult {
    char arp[ARP_POOL_SIZE][MAX_DEVICE_NAME_LEN];
    size_t arp_count;
} ARPScanResult;

// --- DNS captive portal -------------------------------------------------------

#define DNS_PORT                        53                  // standard DNS UDP port
#define DNS_BUF_SIZE                    512                 // UDP receive/send buffer (bytes)
#define DNS_CAPTURE_ALL                 0                   // 1 = reply with device IP; 0 = forward to upstream
