#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

/*
 * http.h — Minimal HTTP/1.1 client primitives.
 *
 * Provides low-level socket and HTTP framing functions used by the LLM client.
 * This module handles only TCP connections and HTTP protocol mechanics;
 * higher layers (llm_client) are responsible for JSON serialization.
 *
 * Features:
 *   - TCP socket connection with error reporting
 *   - Reliable send/receive with timeout support
 *   - HTTP response parsing with status code extraction
 */

/*
 * Establish a TCP connection to the given host and port.
 *
 * Parameters:
 *   host    - Target hostname (e.g., "127.0.0.1" or "api.openai.com")
 *   port    - Target port number (e.g., 80 or 18080)
 *   err     - Buffer for error message on failure
 *   err_cap - Capacity of error buffer
 *
 * Returns:
 *   Socket file descriptor on success, -1 on failure (err is populated)
 */
int tcp_connect(const char *host, int port, char *err, size_t err_cap);

/*
 * Send all bytes from a buffer over a socket.
 *
 * Handles partial writes automatically (loops until all data sent or error).
 *
 * Parameters:
 *   fd  - Connected socket file descriptor
 *   buf - Data buffer to send
 *   len - Number of bytes to send
 *
 * Returns:
 *   0 on success, -1 on error
 */
int send_all(int fd, const void *buf, size_t len);

/*
 * Read all data from a socket until the peer closes the connection.
 *
 * Uses SO_RCVTIMEO to enforce a timeout on each recv() call. The received
 * data is stored in a dynamically allocated, NUL-terminated buffer.
 *
 * Parameters:
 *   fd         - Connected socket file descriptor
 *   timeout_sec- Read timeout in seconds (0 = no timeout)
 *   out        - Output pointer for allocated buffer (caller must free)
 *   out_len    - Output pointer for buffer size
 *   err        - Buffer for error message on failure
 *   err_cap    - Capacity of error buffer
 *
 * Returns:
 *   0 on success, -1 on error
 */
int recv_all(int fd, int timeout_sec, char **out, size_t *out_len, char *err,
             size_t err_cap);

/*
 * Parse an HTTP response in-place.
 *
 * Extracts the HTTP status code and returns a pointer to the start of the
 * message body within the original buffer. The input buffer is modified
 * in-place (NUL-terminated at body boundary).
 *
 * Parameters:
 *   raw    - Full HTTP response string (modified in-place)
 *   status - Output pointer for HTTP status code (e.g., 200, 404)
 *   body   - Output pointer to start of message body
 *
 * Returns:
 *   0 on success, -1 on parse error
 */
int http_parse_response(const char *raw, int *status, const char **body);

#endif