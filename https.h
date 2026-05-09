#ifndef HTTPS_H
#define HTTPS_H

void https_init(void);
void https_cleanup(void);

int https_post_request(const char *hostname, int port, const char *http_request,
                       char **response);

#endif
