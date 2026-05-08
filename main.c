#include "agent/agent.h"
#include "config.h"
#include "ui/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools/tools.h"

#define INPUT_BUF 4096

int main(void) {
  config_init();
  tools_init();

  Agent *a = agent_create();
  if (!a) {
    fprintf(stderr, "agent_create failed\n");
    return 1;
  }

  ui_init();
  ui_start();
  ui_banner();

  printf("Terminal Coding Agent Started. Type 'exit', 'quit', or 'q' to quit.\n");

  char input[INPUT_BUF];
  int rc = 0;

  while (1) {
    printf("> ");
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) {
      break; 
    }

    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
      input[len - 1] = '\0';
    }

    if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) {
      break;
    }

    if (strlen(input) == 0) {
      continue;
    }

    const char *reply = agent_chat(a, input);
    
    if (reply) {
      printf("%s\n", reply);
    }
    else {
      rc = 1;
      break; 
    }
  }

  ui_stop();
  agent_free(a);
  
  return rc;
}