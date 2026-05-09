#define _XOPEN_SOURCE 700
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "https.h"

static SSL_CTX *g_ssl_ctx = NULL;

void https_init(void) {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  g_ssl_ctx = SSL_CTX_new(TLS_client_method());
  if (!g_ssl_ctx)
    fprintf(stderr, "[HTTPS] Unable to create SSL context\n");
}

void https_cleanup(void) {
  if (g_ssl_ctx) {
    SSL_CTX_free(g_ssl_ctx);
    g_ssl_ctx = NULL;
  }
}

int https_post_request(const char *hostname, int port, const char *http_request,
                       char **response) {
  if (!g_ssl_ctx) {
    fprintf(stderr, "[HTTPS] SSL not initialized\n");
    return -1;
  }

  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", port);

  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
  struct addrinfo *res = NULL;
  if (getaddrinfo(hostname, port_str, &hints, &res) != 0 || !res) {
    fprintf(stderr, "[HTTPS] DNS resolution failed for %s\n", hostname);
    return -1;
  }

  int sock = socket(res->ai_family, res->ai_socktype, 0);
  if (sock < 0) {
    fprintf(stderr, "[HTTPS] socket creation failed\n");
    freeaddrinfo(res);
    return -1;
  }

  if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
    fprintf(stderr, "[HTTPS] TCP connect failed\n");
    close(sock);
    freeaddrinfo(res);
    return -1;
  }
  freeaddrinfo(res);

  SSL *ssl = SSL_new(g_ssl_ctx);
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

  for (;;) {
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
    int bytes = SSL_read(ssl, res_buf + received, capacity - received - 1);
    if (bytes > 0) {
      received += bytes;
      continue;
    }
    if (bytes == 0)
      break;
    /* Retry on EINTR or WANT_READ; otherwise treat as error. */
    int ssl_err = SSL_get_error(ssl, bytes);
    if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE)
      continue;
    if (ssl_err == SSL_ERROR_SYSCALL && errno == EINTR)
      continue;
    break;
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
  return (*response != NULL) ? 0 : -1;
}
