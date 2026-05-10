/*
 * bash.c — Shell command execution tool for the AI Agent.
 *
 * Implements the "bash" tool that runs a shell command, captures stdout/stderr,
 * and returns the result as a ToolResult. Uses fork+exec on Linux and
 * CreateProcess on Windows.
 */
#include "tools.h"
#include "compat.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *BASH_TOOL_NAME = "bash";
const char *BASH_TOOL_DESC =
    "Run a shell command and return its combined stdout/stderr.";
const char *BASH_TOOL_SCHEMA =
    "{\"type\":\"object\","
    "\"properties\":{\"command\":{\"type\":\"string\","
    "\"description\":\"The shell command to execute\"}},"
    "\"required\":[\"command\"]}";

ToolResult bash_tool_exec(cJSON *args);

ToolDef bash_def = {
    .name = "bash",
    .desc = "Run a shell command and return its combined stdout/stderr. "
            "Use for commands that MODIFY files (git commit, npm install, rm, mkdir, etc.).",
    .param_schema = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"The shell command to execute\"}},\"required\":[\"command\"]}",
    .exec = bash_tool_exec,
    .read_only = false,
};

ToolDef bash_readonly_def = {
    .name = "bash_readonly",
    .desc = "Run a READ-ONLY shell command (ls, cd, pwd, grep, cat, git status, git log, "
            "git diff, find, wc, head, tail, etc.) and return its output. "
            "Use this for commands that do NOT modify files.",
    .param_schema = "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"A read-only shell command\"}},\"required\":[\"command\"]}",
    .exec = bash_tool_exec,
    .read_only = true,
};

/* ── Dynamic pipe reader (shared) ────────────────────── */

static char *read_pipe_data(char *(*read_fn)(void *ctx, size_t *len), void *ctx) {
  size_t cap = 4096;
  size_t len = 0;
  char *buf = xmalloc(cap);

  while (1) {
    if (len + 1 >= cap) {
      cap *= 2;
      buf = xrealloc(buf, cap);
    }
    size_t chunk = 0;
    char *chunk_data = read_fn(ctx, &chunk);
    if (!chunk_data)
      break;
    if (chunk == 0)
      break;
    if (len + chunk + 1 > cap) {
      cap = len + chunk + 1024;
      buf = xrealloc(buf, cap);
    }
    memcpy(buf + len, chunk_data, chunk);
    len += chunk;
    free(chunk_data);
  }
  buf[len] = '\0';
  return buf;
}

/* ── Windows implementation ──────────────────────────── */

#ifdef _WIN32

typedef struct {
  HANDLE hRead;
} pipe_ctx_t;

static char *win_read_chunk(void *vctx, size_t *out_len) {
  pipe_ctx_t *ctx = (pipe_ctx_t *)vctx;
  char chunk[4096];
  DWORD avail = 0;
  if (!PeekNamedPipe(ctx->hRead, NULL, 0, NULL, &avail, NULL) || avail == 0)
    avail = sizeof(chunk);
  if (avail > sizeof(chunk))
    avail = sizeof(chunk);
  DWORD nread = 0;
  if (!ReadFile(ctx->hRead, chunk, avail, &nread, NULL) || nread == 0)
    return NULL;
  char *out = xmalloc(nread + 1);
  memcpy(out, chunk, nread);
  out[nread] = '\0';
  *out_len = nread;
  return out;
}

ToolResult bash_tool_exec(cJSON *args) {
  const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
  if (!cmd)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing 'command' argument")};

  /* Build command line: cmd.exe /c <user_command> */
  const char *shell = getenv("COMSPEC");
  if (!shell || !shell[0])
    shell = "cmd.exe";

  /* cmd.exe needs /c for command execution */
  size_t cmdline_len = strlen(shell) + 4 + strlen(cmd) + 1;
  char *cmdline = xmalloc(cmdline_len);
  snprintf(cmdline, cmdline_len, "%s /c \"%s\"", shell, cmd);

  HANDLE hChildOutRd = NULL, hChildOutWr = NULL;
  SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};

  if (!CreatePipe(&hChildOutRd, &hChildOutWr, &sa, 0)) {
    free(cmdline);
    return (ToolResult){.ok = false,
                        .output = xasprintf("CreatePipe failed: error %lu",
                                            GetLastError())};
  }
  SetHandleInformation(hChildOutRd, HANDLE_FLAG_INHERIT, 0);

  PROCESS_INFORMATION pi = {0};
  STARTUPINFOA si = {sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.hStdOutput = hChildOutWr;
  si.hStdError = hChildOutWr;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.wShowWindow = SW_HIDE;

  BOOL created = CreateProcessA(
      NULL,         /* lpApplicationName — use command line directly */
      cmdline,      /* lpCommandLine */
      NULL, NULL,   /* process/thread security */
      TRUE,         /* inherit handles */
      CREATE_NO_WINDOW, /* flags */
      NULL, NULL,   /* environment / current directory */
      &si, &pi);

  free(cmdline);
  CloseHandle(hChildOutWr);

  if (!created) {
    CloseHandle(hChildOutRd);
    return (ToolResult){.ok = false,
                        .output = xasprintf("CreateProcess failed: error %lu",
                                            GetLastError())};
  }

  CloseHandle(pi.hThread);

  pipe_ctx_t ctx = {hChildOutRd};
  char *output = read_pipe_data(win_read_chunk, &ctx);
  CloseHandle(hChildOutRd);

  DWORD exit_code = 1;
  WaitForSingleObject(pi.hProcess, INFINITE);
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hProcess);

  /* Truncate if too large (shared logic below) */
  size_t len = strlen(output);
  if (len > MAX_TOOL_OUTPUT) {
    size_t head = MAX_TOOL_OUTPUT / 2;
    size_t tail = MAX_TOOL_OUTPUT - head - 128;
    size_t skip = len - head - tail;
    memmove(output + head, output + head + skip, tail + 1);
    memcpy(output + head + tail, "\n\n... (output truncated)\n", 25);
    output[head + tail + 25] = '\0';
  }

  ToolResult res;
  if (exit_code == 0) {
    res.ok = true;
    res.output = output;
  } else {
    res.ok = false;
    res.output = xasprintf("Command failed with exit code %lu.\nOutput:\n%s",
                           exit_code, output);
    free(output);
  }
  return res;
}

#else /* ── POSIX implementation ──────────────────────── */

#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  int fd;
} pipe_ctx_t;

static char *posix_read_chunk(void *vctx, size_t *out_len) {
  pipe_ctx_t *ctx = (pipe_ctx_t *)vctx;
  char chunk[4096];
  ssize_t n = read(ctx->fd, chunk, sizeof(chunk));
  if (n < 0) {
    if (errno == EINTR)
      return posix_read_chunk(vctx, out_len); /* retry */
    return NULL;
  }
  if (n == 0)
    return NULL;
  char *out = xmalloc((size_t)n + 1);
  memcpy(out, chunk, (size_t)n);
  out[n] = '\0';
  *out_len = (size_t)n;
  return out;
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

  if (pid == 0) {
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0 ||
        dup2(pipefd[1], STDERR_FILENO) < 0)
      _exit(127);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
    dprintf(STDERR_FILENO, "exec failed: %s\n", strerror(errno));
    _exit(127);
  }

  close(pipefd[1]);

  pipe_ctx_t ctx = {pipefd[0]};
  char *buf = read_pipe_data(posix_read_chunk, &ctx);
  close(pipefd[0]);

  size_t len = strlen(buf);
  if (len > MAX_TOOL_OUTPUT) {
    size_t head = MAX_TOOL_OUTPUT / 2;
    size_t tail = MAX_TOOL_OUTPUT - head - 128;
    size_t skip = len - head - tail;
    memmove(buf + head, buf + head + skip, tail + 1);
    memcpy(buf + head + tail, "\n\n... (output truncated)\n", 25);
    len = head + tail + 25;
    buf[len] = '\0';
  }

  int wstatus;
  while (waitpid(pid, &wstatus, 0) == -1) {
    if (errno != EINTR) {
      free(buf);
      return (ToolResult){.ok = false,
                          .output = xasprintf("waitpid failed: %s",
                                              strerror(errno))};
    }
  }

  ToolResult res;
  if (WIFEXITED(wstatus)) {
    int exit_code = WEXITSTATUS(wstatus);
    if (exit_code == 0) {
      res.ok = true;
      res.output = buf;
    } else {
      res.ok = false;
      res.output = xasprintf("Command failed with exit code %d.\nOutput:\n%s",
                             exit_code, buf);
      free(buf);
    }
  } else if (WIFSIGNALED(wstatus)) {
    int sig = WTERMSIG(wstatus);
    res.ok = false;
    res.output = xasprintf("Command terminated by signal %d.\nOutput:\n%s",
                           sig, buf);
    free(buf);
  } else {
    res.ok = false;
    res.output = xasprintf("Command terminated abnormally.\nOutput:\n%s", buf);
    free(buf);
  }

  return res;
}

#endif /* _WIN32 */
