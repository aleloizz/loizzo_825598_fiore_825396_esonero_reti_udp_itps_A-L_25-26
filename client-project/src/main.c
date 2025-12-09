/* main.c
 * TCP Weather Client
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>

#if defined _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#define closesocket close
#endif

#include "protocol.h"

//Correzione problema lettura caratteri speciali in console Windows
#if defined _WIN32
#define DEG_C_SUFFIX "C"
#else
#define DEG_C_SUFFIX "°C"
#endif

/*
 * my_inet_pton
 * Wrapper di compatibilità per `inet_pton`.
 * Alcune toolchain Windows (MinGW) non forniscono inet_pton; questa
 * funzione tenta prima la conversione con `inet_addr` e, se fallisce,
 * risolve il nome host con `gethostbyname`.
 *
 * Parametri:
 *  - af: address family (AF_INET)
 *  - src: stringa contenente l'indirizzo IP o hostname
 *  - dst: puntatore dove scrivere la struttura in_addr risultante
 *
 * Restituisce 1 in caso di successo, 0 in caso di errore.
 */
static int my_inet_pton(int af, const char *src, void *dst)
{
#if defined _WIN32
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

/*
 * my_inet_ntop
 * Wrapper di compatibilità per `inet_ntop`.
 * Converte una struttura in_addr in una stringa leggibile. Su Windows usa
 * inet_ntoa e copia il risultato in `dst`.
 *
 * Parametri:
 *  - af: address family
 *  - src: puntatore alla struttura in_addr
 *  - dst: buffer di destinazione
 *  - size: dimensione del buffer
 *
 * Restituisce `dst` in caso di successo, NULL in caso di errore.
 */
static const char *my_inet_ntop(int af, const void *src, char *dst, size_t size)
{
#if defined _WIN32
    if (af != AF_INET) return NULL;
    const struct in_addr *in = (const struct in_addr *)src;
    const char *s = inet_ntoa(*in);
    if (!s) return NULL;
    strncpy(dst, s, size - 1);
    dst[size - 1] = '\0';
    return dst;
#else
    return inet_ntop(af, src, dst, (socklen_t)size);
#endif
}

/*
 * print_usage
 * Stampa il formato corretto dell'utilizzo del programma.
 *
 * static void print_usage(const char *progname)
{
    printf("Usage: %s [-s server] [-p port] -r \"type city\"\n", progname);
}
*/

/*
 * send_all
 * Assicura l'invio di tutti i byte del buffer sul socket. `send` potrebbe
 * inviare un numero di byte inferiore a quelli richiesti, pertanto si itera
 * finché tutto il buffer non è stato inviato o si verifica un errore.
 * Restituisce 0 in caso di successo, -1 in caso di errore.
 */
int send_all(int sock, const void *buf, size_t len)
{
    /* UDP is message-oriented: send the datagram once and ensure the
     * kernel accepted the full payload. For connected UDP sockets a
     * single send() corresponds to a single datagram. Looping and
     * attempting to read/write partial datagrams (TCP-style) can
     * deadlock or produce incorrect behaviour. */
    int sent = send(sock, buf, (int)len, 0);
    if (sent < 0) return -1;
    if ((size_t)sent != len) return -1;
    return 0;
}

/*
 * recv_all
 * Riceve esattamente `len` byte dal socket. Poiché `recv` può restituire
 * meno byte di quelli richiesti, si itera finché non si riceve l'intero
 * buffer o si verifica un errore/chiusura della connessione.
 * Restituisce 0 in caso di successo, -1 in caso di errore.
 */
int recv_all(int sock, void *buf, size_t len)
{
    /* For UDP we expect a single datagram containing the full message.
     * Call recv once and require that the received length matches the
     * expected `len`. Repeated recv calls (TCP-style) are not valid
     * for datagram sockets because datagram boundaries are preserved
     * and a second recv would block waiting for a new datagram. */
    int r = recv(sock, buf, (int)len, 0);
    if (r <= 0) return -1;
    if ((size_t)r != len) return -1;
    return 0;
}

/*
 * ntohf
 * Converte un uint32_t ricevuto in network byte order nella corrispondente
 * variabile `float` in host byte order. Il float viene trasmesso in rete
 * come bit pattern (uint32_t) e qui si usano ntohl + memcpy per rispettare
 * l'endianness e le aliasing rules.
 */
float ntohf(uint32_t i)
{
    i = ntohl(i);
    float f;
    memcpy(&f, &i, sizeof(f));
    return f;
}

/*
 * validaporta
 * Verifica che la stringa fornita rappresenti un numero intero
 * compreso nell'intervallo delle porte valide (1-65535).
 * Se valida, scrive il valore in `out_port` e restituisce 1.
 * Altrimenti restituisce 0.
 */
int validaporta(const char *s, int *out_port)
{
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0) return 0;
    if (*end != '\0') return 0;
    if (v < 1 || v > 65535) return 0;
    *out_port = (int)v;
    return 1;
}

int main(int argc, char *argv[])
{
    const char *server = SERVER_IP; // unified constant from protocol.h
    int port = SERVER_PORT;         // unified constant from protocol.h
    const char *request = NULL;

    /*
     * Parsing degli argomenti da linea di comando
     * -s server : indirizzo del server (opzionale)
     * -p port   : porta del server (opzionale)
     * -r request: stringa obbligatoria con il formato "type city"
     */
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            server = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            ++i;
            if (!validaporta(argv[i], &port)) {
                fprintf(stderr, "Porta non valida: %s\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc) {
            request = argv[++i];
        } else {
            //print_usage(argv[0]);
            return 1;
        }
    }

    if (!request) {
        //print_usage(argv[0]);
        return 1;
    }

    /*
     * Inizializzazione Winsock su Windows. Su sistemi POSIX questa sezione
     * viene saltata.
     */
#if defined _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    /* Resolve server address (IPv4) and perform forward/reverse DNS
     * lookup early so we can display canonical server name and IP even
     * when the client detects a local request parsing error. */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    if (my_inet_pton(AF_INET, server, &server_addr.sin_addr) != 1) {
        struct hostent *he = gethostbyname(server);
        if (!he) {
            fprintf(stderr, "Failed to resolve server address\n");
#if defined _WIN32
            WSACleanup();
#endif
            return 1;
        }
        server_addr.sin_addr = *(struct in_addr *)he->h_addr_list[0];
    }

    char resolved_ip[INET_ADDRSTRLEN] = "";
    char resolved_name[256] = "";
    my_inet_ntop(AF_INET, &server_addr.sin_addr, resolved_ip, sizeof(resolved_ip));
    {
        struct in_addr addr = server_addr.sin_addr;
        struct hostent *he2 = gethostbyaddr(&addr, sizeof(addr), AF_INET);
        if (he2 && he2->h_name) {
            strncpy(resolved_name, he2->h_name, sizeof(resolved_name) - 1);
            resolved_name[sizeof(resolved_name) - 1] = '\0';
        } else {
            strncpy(resolved_name, resolved_ip, sizeof(resolved_name) - 1);
            resolved_name[sizeof(resolved_name) - 1] = '\0';
        }
    }

    /*
     * Parsing della richiesta nel formato "type city". Si considera il primo
     * carattere non-spazio come il `type` e il resto della stringa come
     * il nome della città. Il nome viene copiato in `city` (size limitata).
     */
    /*
     * Parsing della richiesta nel formato "type city".
     * Richiesta valida: il primo token (prima del primo spazio) deve
     * essere esattamente un singolo carattere che rappresenta il tipo.
     * Esempi:
     *  - "t bari"  -> type='t', city='bari'  (valido)
     *  - "pippo bari" -> token 'pippo' ha lunghezza>1 -> richiesta non valida
     */
    const char *p = request;
    while (*p && isspace((unsigned char)*p)) p++;
    const char *token_start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t token_len = (size_t)(p - token_start);
    if (token_len != 1) {
        // Token non valido: stampiamo il messaggio richiesto senza contattare il server
        printf("Ricevuto risultato dal server %s (ip %s). Richiesta non valida\n", resolved_name, resolved_ip);
        return 1;
    }
    char type = token_start[0];
    while (*p && isspace((unsigned char)*p)) p++;
    char city[64];
    memset(city, 0, sizeof(city));
    /* Validate city: no tabs allowed and max length 63 (plus null). */
    if (strchr(p, '\t') != NULL) {
        printf("Ricevuto risultato dal server %s (ip %s). Richiesta non valida\n", resolved_name, resolved_ip);
        return 1;
    }
    if (strlen(p) > 63) {
        printf("Ricevuto risultato dal server %s (ip %s). Richiesta non valida\n", resolved_name, resolved_ip);
        return 1;
    }
    strncpy(city, p, sizeof(city) - 1);

    /* (DNS resolution already performed earlier) */

    int sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        perror("socket");
    #if defined _WIN32
        WSACleanup();
    #endif
        return 1;
        }

        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("connect");
        closesocket(sock);
    #if defined _WIN32
        WSACleanup();
    #endif
        return 1;
        }

    /*
     * Preparazione della richiesta in formato binario fisso: 1 byte per il
     * tipo e 64 byte per la città. Si invia con send_all per assicurare che
     * tutti i byte vengano trasmessi.
     */
    // Prepare and send request: fixed 65 bytes (1 type + 64 city)
    unsigned char reqbuf[65];
    memset(reqbuf, 0, sizeof(reqbuf));
    reqbuf[0] = (unsigned char)type;
    strncpy((char *)&reqbuf[1], city, 63);
    if (send_all(sock, reqbuf, sizeof(reqbuf)) != 0) {
        fprintf(stderr, "Failed to send request\n");
        closesocket(sock);
#if defined _WIN32
        WSACleanup();
#endif
        return 1;
    }

    /*
     * Ricezione della risposta: protocollo binario composto da
     *  - 4 byte: status (uint32_t in network byte order)
     *  - 1 byte: type (char)
     *  - 4 byte: value (float inviato come uint32_t in network byte order)
     * Si usa recv_all per assicurare la ricezione completa dei 9 byte.
     */
    // Receive response: 4 bytes status (network), 1 byte type, 4 bytes float
    unsigned char respbuf[9];
    if (recv_all(sock, respbuf, sizeof(respbuf)) != 0) {
        fprintf(stderr, "Failed to receive response\n");
        closesocket(sock);
#if defined _WIN32
        WSACleanup();
#endif
        return 1;
    }

    uint32_t net_status;
    memcpy(&net_status, respbuf, 4);
    uint32_t status = ntohl(net_status);
    char rtype = (char)respbuf[4];
    uint32_t net_f;
    memcpy(&net_f, &respbuf[5], 4);
    float value = ntohf(net_f);

    /*
     * Ottenimento dell'indirizzo del peer per stampare l'IP del server che
     * ha risposto. Se getpeername fallisce si usa la stringa del server
     * fornita dall'utente.
     */
    // Get peer IP for printing
    struct sockaddr_in peer_addr;
#if defined _WIN32
    int peer_len = sizeof(peer_addr);
#else
    socklen_t peer_len = sizeof(peer_addr);
#endif
    int got_peer = 0;
    char peer_ip[INET_ADDRSTRLEN] = "";
    if (getpeername(sock, (struct sockaddr *)&peer_addr, &peer_len) == 0) {
        my_inet_ntop(AF_INET, &peer_addr.sin_addr, peer_ip, sizeof(peer_ip));
        got_peer = 1;
    } else {
        /* Fallback to the resolved IP we obtained earlier via DNS. */
        strncpy(peer_ip, resolved_ip, sizeof(peer_ip) - 1);
        peer_ip[sizeof(peer_ip) - 1] = '\0';
    }

    /*
     * Costruzione del messaggio finale da mostrare all'utente secondo la
     * specifica: "Ricevuto risultato dal server ip <ip_address>. <messaggio>"
     * A seconda del codice di stato e del tipo si formatta il testo in
     * italiano (Temperatura, Umidità, Vento, Pressione) con una cifra
     * decimale.
     */
    // Capitalizza la prima lettera della città per stampa estetica
    if (city[0]) city[0] = (char)toupper((unsigned char)city[0]);

    // Build message per spec
    char message[256];
    if (status == STATUS_SUCCESS) {
        switch (rtype) {
        case 't':
            snprintf(message, sizeof(message), "%s: Temperatura = %.1f%s", city, value, DEG_C_SUFFIX);
            break;
        case 'h':
            snprintf(message, sizeof(message), "%s: Umidita' = %.1f%%", city, value);
            break;
        case 'w':
            snprintf(message, sizeof(message), "%s: Vento = %.1f km/h", city, value);
            break;
        case 'p':
            snprintf(message, sizeof(message), "%s: Pressione = %.1f hPa", city, value);
            break;
        default:
            snprintf(message, sizeof(message), "Tipo di dato non valido");
            break;
        }
    } else if (status == STATUS_CITY_NOT_AVAILABLE) {
        snprintf(message, sizeof(message), "Citta' non disponibile");
    } else if (status == STATUS_INVALID_REQUEST) {
        snprintf(message, sizeof(message), "Richiesta non valida");
    } else {
        snprintf(message, sizeof(message), "Errore");
    }

    /* Decide which IP/name to print: prefer the peer info when available,
     * otherwise use the resolved values from the original server input. */
    char print_ip[INET_ADDRSTRLEN] = "";
    char print_name[256] = "";
    if (got_peer) {
        strncpy(print_ip, peer_ip, sizeof(print_ip) - 1);
        print_ip[sizeof(print_ip) - 1] = '\0';
        /* reverse lookup the peer address */
        struct in_addr addr = peer_addr.sin_addr;
        struct hostent *he3 = gethostbyaddr(&addr, sizeof(addr), AF_INET);
        if (he3 && he3->h_name) {
            strncpy(print_name, he3->h_name, sizeof(print_name) - 1);
            print_name[sizeof(print_name) - 1] = '\0';
        } else {
            strncpy(print_name, print_ip, sizeof(print_name) - 1);
            print_name[sizeof(print_name) - 1] = '\0';
        }
    } else {
        strncpy(print_ip, resolved_ip, sizeof(print_ip) - 1);
        print_ip[sizeof(print_ip) - 1] = '\0';
        strncpy(print_name, resolved_name, sizeof(print_name) - 1);
        print_name[sizeof(print_name) - 1] = '\0';
    }

    printf("Ricevuto risultato dal server %s (ip %s). %s\n", print_name, print_ip, message);

    closesocket(sock);
#if defined _WIN32
    WSACleanup();
#endif
    return 0;
}
