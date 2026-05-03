/*
 * bash.c — Shell command execution tool for the AI Agent.
 *
 * Implements the "bash" tool that forks a child process, runs a shell command
 * via /bin/sh -c, captures stdout/stderr, and returns the result as a ToolResult.
 * Handles exit codes, signals, and failed executions gracefully so the LLM
 * can see what happened and recover.
 */
#include "tools.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const char *BASH_TOOL_NAME = "bash";
const char *BASH_TOOL_DESC =
    "Run a shell command and return its combined stdout/stderr.";
const char *BASH_TOOL_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"The shell command to execute\"}},"
    "\"required\":[\"command\"]}";

void tool_result_free(ToolResult *r) {
  if (!r)
    return;
  free(r->output);
  r->output = NULL;
}

ToolResult bash_tool_exec(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  int pipefd[2];
  if (pipe(pipefd) != 0)
    return (ToolResult){
        .ok = false,
        .output = xasprintf("pipe failed: %s", strerror(errno)),
    };

  pid_t pid = fork();
  if (pid < 0) {
    int e = errno;
    close(pipefd[0]);
    close(pipefd[1]);
    return (ToolResult){.ok = false,
                        .output = xasprintf("fork: %s", strerror(e))};
  }

  // Child process: redirect stdout/stderr to pipe and execute command
  if (pid == 0) {
    close(pipefd[0]);
    /* dprintf + _exit avoid stdio-buffer double-flush after fork. */
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
    _exit(127);
  }

  close(pipefd[1]);
  
  // Dynamically read all output from the pipe
  size_t cap = 4096;
  size_t len = 0;
  char *buf = xmalloc(cap);

  // Read
  while (1) {
    if (len + 1 >= cap) {
      cap *= 2;
      buf = xrealloc(buf, cap);
    }

    ssize_t n = read(pipefd[0], buf + len, cap - len - 1);
    if (n < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (n == 0) break;
    len += n;
  }
  buf[len] = '\0';
  close(pipefd[0]);

  int wstatus;
  while (waitpid(pid, &wstatus, 0) == -1) {
    if (errno != EINTR) {
      free(buf);
      return (ToolResult){.ok = false, .output = xasprintf("waitpid failed: %s", strerror(errno))};
    }
  }

  // To LLM
  ToolResult res;
  if (WIFEXITED(wstatus)) {
    int exit_code = WEXITSTATUS(wstatus);
    if (exit_code == 0) {
      res.ok = true;
      res.output = buf;
    }
    else {
      res.ok = false;
      res.output = xasprintf("Command failed with exit code %d.\nOutput:\n%s", exit_code, buf);
      free(buf);
    }
  }
  else if (WIFSIGNALED(wstatus)) {
    int sig = WTERMSIG(wstatus);
    res.ok = false;
    res.output = xasprintf("Command terminated by signal %d.\nOutput:\n%s", sig, buf);
    free(buf);
  }
  else {
    res.ok = false;
    res.output = xasprintf("Command terminated abnormally.\nOutput:\n%s", buf);
    free(buf);
  }

  return res;
}