#include "http.h"
#include "compat.h"

#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

socket_t tcp_connect(const char *host, int port, char *err, size_t err_cap) {
  char port_buf[16];
  snprintf(port_buf, sizeof(port_buf), "%d", port);

  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, port_buf, &hints, &res) != 0 || !res) {
    snprintf(err, err_cap, "cannot resolve host %s", host);
    return INVALID_SOCKET_VAL;
  }

  socket_t fd = socket(res->ai_family, res->ai_socktype, 0);
  if (fd == INVALID_SOCKET_VAL) {
    snprintf(err, err_cap, "socket: %s", strerror(errno));
    freeaddrinfo(res);
    return INVALID_SOCKET_VAL;
  }

  if (connect(fd, res->ai_addr, (int)res->ai_addrlen) < 0) {
    snprintf(err, err_cap, "connect: %s", strerror(errno));
    socket_close(fd);
    freeaddrinfo(res);
    return INVALID_SOCKET_VAL;
  }

  freeaddrinfo(res);
  return fd;
}

int send_all(socket_t fd, const void *buf, size_t len) {
  const char *p = buf;
  size_t sent = 0;
  while (sent < len) {
    int n = (int)send(fd, p + sent, (int)(len - sent), 0);
    if (n < 0) {
      if (socket_errno() == SOCKET_EINTR)
        continue;
      return -1;
    }
    if (n == 0)
      return -1;
    sent += (size_t)n;
  }
  return 0;
}

int recv_all(socket_t fd, int timeout_sec, char **out, size_t *out_len,
             char *err, size_t err_cap) {
  if (timeout_sec > 0) {
    if (socket_set_timeout(fd, timeout_sec) != 0) {
      snprintf(err, err_cap, "setsockopt: %s", strerror(errno));
      return -1;
    }
  }

  size_t cap = 4096;
  size_t len = 0;
  char *buf = xmalloc(cap);

  for (;;) {
    if (len + 1 >= cap) {
      cap *= 2;
      buf = xrealloc(buf, cap);
    }
    int n = (int)recv(fd, buf + len, (int)(cap - len - 1), 0);
    if (n < 0) {
      int e = socket_errno();
      if (e == SOCKET_EINTR)
        continue;
      if (e == SOCKET_EAGAIN)
        snprintf(err, err_cap, "recv timed out (%ds)", timeout_sec);
      else
        snprintf(err, err_cap, "recv: error %d", e);
      free(buf);
      return -1;
    }
    if (n == 0)
      break;
    len += (size_t)n;
  }

  buf[len] = '\0';
  *out = buf;
  *out_len = len;
  return 0;
}

int http_parse_response(const char *raw, int *status, const char **body) {
  const char *space = strchr(raw, ' ');
  if (!space)
    return -1;
  *status = (int)strtol(space + 1, NULL, 10);

  const char *sep = strstr(raw, "\r\n\r\n");
  if (!sep)
    return -1;
  *body = sep + 4;
  return 0;
}
