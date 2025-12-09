/*
 * protocol.h
 *
 * Client header file
 * Definitions, constants and function prototypes for the client
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

// Unified shared constants (mirrors server header)
#define SERVER_PORT 56700
#define SERVER_IP   "127.0.0.1"
#define BUFFER_SIZE 512
#define QLEN  6

// Status codes (shared)
#define STATUS_SUCCESS            0u
#define STATUS_CITY_NOT_AVAILABLE 1u
#define STATUS_INVALID_REQUEST    2u

// Request (client -> server)
typedef struct {
    char type;      // 't','h','w','p'
    char city[64];  // null-terminated city name
} weather_request_t;

// Unified response (server -> client)
typedef struct {
    unsigned int status; // STATUS_* values
    char type;           // echo of request type (or '\0' on error)
    float value;         // weather value (0.0 if error)
} weather_response_t;

// Server-side prototypes (not used by client directly, included for symmetry)
int handleclientconnection(int client_socket, const char *client_ip);
float typecheck(char type);
char citycheck(const char *city);
weather_response_t build_weather_response(char type, const char *city);

// Client-side prototypes (not used by server directly, included for symmetry)
int send_all(int sock, const void *buf, size_t len);
int recv_all(int sock, void *buf, size_t len);
float ntohf(uint32_t i);
int validaporta(const char *s, int *out_port);
static float ntohf(uint32_t i);

// Cross-platform inet_pton/ntop wrappers
static int my_inet_pton(int af, const char *src, void *dst);
static const char *my_inet_ntop(int af, const void *src, char *dst, size_t size);

// Data generation functions (shared)
float get_temperature(void); // -10.0 .. 40.0
float get_humidity(void);    // 20.0 .. 100.0
float get_wind(void);        // 0.0 .. 100.0
float get_pressure(void);    // 950.0 .. 1050.0



#endif /* PROTOCOL_H_ */
