// https.c
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "https.h"

int https_post_request(const char *hostname, int port, const char *http_request, char **response) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    
    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        fprintf(stderr, "[HTTPS] Unable to create SSL context\n");
        return -1;
    }

    struct hostent *host = gethostbyname(hostname);
    if (host == NULL) {
        fprintf(stderr, "[HTTPS] DNS resolution failed for %s\n", hostname);
        SSL_CTX_free(ctx);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) != 0) {
        fprintf(stderr, "[HTTPS] TCP connect failed\n");
        close(sock);
        SSL_CTX_free(ctx);
        return -1;
    }

    SSL *ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, hostname);

    if (SSL_connect(ssl) <= 0) {
        fprintf(stderr, "[HTTPS] SSL handshake failed\n");
        ERR_print_errors_fp(stderr);
        goto cleanup;
    }

    if (SSL_write(ssl, http_request, strlen(http_request)) <= 0) {
        fprintf(stderr, "[HTTPS] SSL write failed\n");
        goto cleanup;
    }

    size_t capacity = 8192;
    size_t received = 0;
    char *res_buf = malloc(capacity);
    if (!res_buf) goto cleanup;

    int bytes;
    while ((bytes = SSL_read(ssl, res_buf + received, capacity - received - 1)) > 0) {
        received += bytes;
        if (capacity - received < 1024) {
            capacity *= 2;
            char *new_buf = realloc(res_buf, capacity);
            if (!new_buf) {
                free(res_buf);
                res_buf = NULL;
                goto cleanup;
            }
            res_buf = new_buf;
        }
    }
    
    if (res_buf) {
        res_buf[received] = '\0';
        *response = res_buf;
    }

cleanup:
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    close(sock);
    SSL_CTX_free(ctx);
    
    return (*response != NULL) ? 0 : -1;
}