/*
 * protocol.h
 *
 * Server header file
 * Definitions, constants and function prototypes for the server
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

// Shared application parameters (unified client/server constants)
#define SERVER_PORT  56700         // Default server port
#define SERVER_IP   "127.0.0.1"    // Default server IP (override in runtime if needed)
#define BUFFER_SIZE 512            // Generic buffer size
#define QUEUE_SIZE  5              // Pending connections queue size (server only)
#define QLEN 6

// Status codes (shared)
#define STATUS_SUCCESS            0u
#define STATUS_CITY_NOT_AVAILABLE 1u
#define STATUS_INVALID_REQUEST    2u

// Client request structure (binary protocol: 1 byte type + 64 bytes city when sent)
typedef struct {
    char type;       // 't','h','w','p'
    char city[64];   // null-terminated city name
} weather_request_t;

// Unified response structure (server -> client)
typedef struct {
    unsigned int status; // STATUS_* values
    char type;           // echo of request type (or '\0' on error)
    float value;         // generated weather value (0.0 on error)
} weather_response_t;

// Server-side function prototypes
int handleclientconnection(int client_socket, const char *client_ip);
float typecheck(char type);
char citycheck(const char *city);
weather_response_t build_weather_response(char type, const char *city);

// Data generation (shared)
float get_temperature(void);    // Range: -10.0 .. 40.0 °C
float get_humidity(void);       // Range: 20.0 .. 100.0 %
float get_wind(void);           // Range: 0.0 .. 100.0 km/h
float get_pressure(void);       // Range: 950.0 .. 1050.0 hPa

static int my_inet_pton(int af, const char *src, void *dst);

#endif /* PROTOCOL_H_ */
