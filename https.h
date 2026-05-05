// https.h
#ifndef HTTPS_H
#define HTTPS_H

/*
  For HTTPS request
 */
int https_post_request(const char *hostname, int port, const char *http_request, char **response);

#endif // HTTPS_H