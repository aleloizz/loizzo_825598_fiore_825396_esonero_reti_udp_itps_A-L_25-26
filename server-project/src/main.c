/*
 * main.c
 *
 * UDP Server - Template for Computer Networks assignment
 *
 * This file contains the boilerplate code for a UDP server
 * portable across Windows, Linux and macOS.
 */

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif

#include "protocol.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <time.h>
#include <ctype.h>

// Wrapper compatibile per inet_pton: su Windows usa inet_addr/gethostbyname,
// su Linux/macOS chiama direttamente inet_pton.
static int my_inet_pton(int af, const char *src, void *dst)
{
#if defined(_WIN32)
	if (af != AF_INET) return 0;
	unsigned long a = inet_addr(src);
	if (a == INADDR_NONE) {
		struct hostent *he = gethostbyname(src);
		if (!he) return 0;
		memcpy(dst, he->h_addr_list[0], he->h_length);
		return 1;
	}
	struct in_addr in;
	in.s_addr = a;
	memcpy(dst, &in, sizeof(in));
	return 1;
#else
	return inet_pton(af, src, dst);
#endif
}


void clearwinsock() {
#if defined(_WIN32)
	WSACleanup();
#endif
}

void errorhandler(char *errorMessage) {
	printf ("%s", errorMessage);
}

float get_temperature(void) {
	return ((float)(rand() % 501) / 10.0f) - 10.0f; // -10.0 to 40.0 °C
}

float get_humidity(void) {
	return ((float)(rand() % 801) / 10.0f) + 20.0f; // 20.0 to 100.0 %
}

float get_wind(void) {
	return ((float)(rand() % 1001) / 10.0f); // 0.0 to 100.0 km/h
}

float get_pressure(void) {
	return ((float)(rand() % 1011) / 10.0f) + 950.0f; // 950.0 to 1050.0 hPa
}


int main(int argc, char *argv[]) {

	srand(time(NULL));
	int port = SERVER_PORT;          // valore di default
	const char *bind_ip = SERVER_IP; // valore di default

	// Parsing opzionale di -s (IP) e -p (porta)
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-s") == 0 && (i + 1) < argc) {
			bind_ip = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && (i + 1) < argc) {
			port = atoi(argv[++i]);
		}
	}

	if (port <= 0 || port > 65535) {
		printf("Porta non valida: %d\n", port);
		return 0;
	}

#if defined(_WIN32)
	// Initialize Winsock
	WSADATA wsa_data;
	int result = WSAStartup(MAKEWORD(2,2), &wsa_data);
	if (result != NO_ERROR) {
		printf("Error at WSAStartup()\n");
		return 0;
	}
#endif
	// creazione della socket UDP
	int my_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (my_socket < 0) {
		errorhandler("errore nella creazione del socket.\n");
		return -1;
	}

	// assegnazione di un indirizzo alla socket
	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
	// Risoluzione IP con wrapper portabile, fallback a gethostbyname
	if (my_inet_pton(AF_INET, bind_ip, &server_addr.sin_addr) != 1) {
		struct hostent *he = gethostbyname(bind_ip);
		if (!he) {
			errorhandler("risoluzione IP fallita\n");
			closesocket(my_socket);
			clearwinsock();
			return -1;
		}
		server_addr.sin_addr = *(struct in_addr*)he->h_addr_list[0];
	}

	// socket binding
	if (bind(my_socket, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0) {
		errorhandler("errore nella bind.\n");
		closesocket(my_socket);
		return -1;
	}
	// server UDP in ascolto (nessuna listen/accept per UDP)
	printf("Server UDP in ascolto sulla porta %d...\n", port);

	while (1) {
		// Ogni iterazione gestisce un singolo datagram di richiesta
		if (handleclientconnection(my_socket, NULL) < 0) {
			// In caso di errore di rete grave, si interrompe il server
			break;
		}
	}// fine while loop

	printf("Server terminato.\n");

	closesocket(my_socket);
	clearwinsock();
	return 0;
} // main end


int handleclientconnection(int client_socket, const char *client_ip_unused) {
	// Server UDP: riceve una richiesta in un singolo datagram
	// Protocollo binario: richiesta fissa 65 byte (1 tipo + 64 città)
	unsigned char reqbuf[65];
	struct sockaddr_in client_addr;
#if defined(_WIN32)
	int client_len = (int)sizeof(client_addr);
#else
	socklen_t client_len = (socklen_t)sizeof(client_addr);
#endif
	int rcvd = recvfrom(client_socket,
					 (char *)reqbuf,
					 (int)sizeof(reqbuf),
					 0,
					 (struct sockaddr *)&client_addr,
					 &client_len);
	if (rcvd < 0) {
		errorhandler("Errore nella ricezione della richiesta.\n");
		return -1;
	}

	// Se la dimensione non è quella attesa, richiesta non necessariamente valida
	if (rcvd != (int)sizeof(reqbuf)) {
		printf("Datagram di dimensione inattesa (%d), attesi %zu byte.\n", rcvd, sizeof(reqbuf));
	}

	char req_type = (char)reqbuf[0];
	char city[65];
	memset(city, 0, sizeof(city));
	memcpy(city, &reqbuf[1], (rcvd > 1 && (rcvd - 1) < (int)sizeof(city)) ? (size_t)(rcvd - 1) : (size_t)64);
	city[64] = '\0'; // Garantisce terminazione
	// Normalizza city rimuovendo trailing null/spazi
	int clen = (int)strlen(city);
	while (clen > 0 && (city[clen-1] == ' ' || city[clen-1] == '\r' || city[clen-1] == '\n' || city[clen-1] == '\t')) {
		city[clen-1] = '\0';
		clen--;
	}

	// Calcola IP del client a partire dall'indirizzo del datagram
	char *client_ip = inet_ntoa(client_addr.sin_addr);
	printf("Richiesta '%c %s' dal client ip %s\n",
			req_type ? req_type : '-',
			city[0] ? city : "(vuota)",
			client_ip ? client_ip : "(sconosciuto)");

	// Validazione e costruzione risposta (unificata)
	char type_lower = tolower((unsigned char)req_type);
	if (!(type_lower == 't' || type_lower == 'h' || type_lower == 'w' || type_lower == 'p')) {
		type_lower = '\0';
	}
	weather_response_t r = build_weather_response(type_lower, city);

	// Serializzazione binaria risposta: 4 byte status (network), 1 byte type, 4 byte float (network bit pattern)
	unsigned char respbuf[9];
	uint32_t net_status = htonl(r.status);
	memcpy(respbuf, &net_status, 4);
	respbuf[4] = (r.status == STATUS_SUCCESS) ? r.type : '\0';
	uint32_t fbits;
	memcpy(&fbits, &r.value, sizeof(fbits));
	fbits = htonl(fbits);
	memcpy(&respbuf[5], &fbits, 4);

	// Invio della risposta tramite UDP (invio atomico del datagram)
	int sent = sendto(client_socket,
				   (const char *)respbuf,
				   (int)sizeof(respbuf),
				   0,
				   (struct sockaddr *)&client_addr,
				   client_len);
	if (sent != (int)sizeof(respbuf)) {
		errorhandler("Errore nell'invio della risposta.\n");
		return -1;
	}

	return 0;
}

float typecheck(char type){
	switch (type){
		case 't':
			get_temperature();
			break;
		case 'h':
			get_humidity();
			break;
		case 'w':
			get_wind();
			break;
		case 'p':
			get_pressure();
			break;
		default:
			printf("Richiesta non valida");
			return 2;
	}
	return 0;
}

char citycheck(const char *city) {
	static const char *valid_cities[] = {
		"Bari","Roma","Milano","Napoli","Torino",
		"Palermo","Genova","Bologna","Firenze","Venezia"
	};
	for (size_t i = 0; i < sizeof(valid_cities)/sizeof(valid_cities[0]); ++i) {
		const char *a = city;
		const char *b = valid_cities[i];
		while (*a && *b) {
			if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
				break;
			}
			a++; b++;
		}
		if (*a == '\0' && *b == '\0') {
			return 0; // valida (case insensitive)
		}
	}
	return 2; // non valida
}

// Funzione che combina validazione e generazione valore secondo specifica.
weather_response_t build_weather_response(char type, const char *city) {
	weather_response_t r;
	r.status = STATUS_SUCCESS;
	r.type = '\0';
	r.value = 0.0f;

	// Validazione type
	if (type == '\0' || typecheck(type) != 0) {
		// Richiesta non valida (tipo errato)
		r.status = STATUS_INVALID_REQUEST;
		return r;
	}

	// Validazione city
	if (city == NULL || *city == '\0' || citycheck(city) != 0) {
		// Città non disponibile
		r.status = STATUS_CITY_NOT_AVAILABLE;
		return r;
	}

	// Generazione valore meteo
	float value = 0.0f;
	switch (type) {
		case 't': value = get_temperature(); break;
		case 'h': value = get_humidity(); break;
		case 'w': value = get_wind(); break;
		case 'p': value = get_pressure(); break;
		default:
			// fallback
			r.status = STATUS_INVALID_REQUEST;
			return r;
	}

	// Popolamento struttura in caso di successo
	r.status = STATUS_SUCCESS;
	r.type = type;
	r.value = value;
	return r;
}
