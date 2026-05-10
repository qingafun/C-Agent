/*
 * compat.h — Platform abstraction layer for Linux / Windows.
 *
 * All OS-specific headers, types, and wrapper functions are centralized here.
 * Other source files include this header instead of <unistd.h>, <sys/socket.h>,
 * <pthread.h>, etc. directly.
 */
#ifndef COMPAT_H
#define COMPAT_H

/* Standard headers needed by the inline wrappers below */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32

/* ── Windows ─────────────────────────────────────────── */

/* Windows API must come before winsock2 to avoid winsock.h conflicts */
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include <sys/stat.h>

/* ── Types ──────────────────────────────────────────── */

typedef SOCKET socket_t;
#define INVALID_SOCKET_VAL  INVALID_SOCKET

/* ── Socket helpers ──────────────────────────────────── */

#define socket_close(fd)    closesocket(fd)
#define socket_errno()      WSAGetLastError()
#define SOCKET_EINTR        WSAEINTR
#define SOCKET_EAGAIN       WSAEWOULDBLOCK

static inline int socket_set_timeout(socket_t fd, int timeout_sec) {
  DWORD tv = (DWORD)(timeout_sec * 1000);
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
}

/* ── File helpers ────────────────────────────────────── */

#define fs_close(fd)        _close(fd)
#define fs_unlink(path)     _unlink(path)

static inline FILE *fs_fdopen(int fd, const char *mode) {
  return _fdopen(fd, mode);
}

static inline int fs_mkstemp(char *tmpl) {
  /* _mktemp_s modifies tmpl in place, then we open the file */
  errno_t e = _mktemp_s(tmpl, strlen(tmpl) + 1);
  if (e != 0) return -1;
  /* _mktemp_s only creates the name; we must open it ourselves */
  int fd;
  _sopen_s(&fd, tmpl, _O_CREAT | _O_RDWR | _O_EXCL, _SH_DENYNO, _S_IREAD | _S_IWRITE);
  return fd; /* -1 on failure */
}

/* ── Directory / path ────────────────────────────────── */

/* PATH_MAX may be missing on MSVC; MAX_PATH is 260, real limit is 32K */
#ifndef PATH_MAX
  #define PATH_MAX 4096
#endif

static inline char *fs_realpath(const char *path, char *resolved) {
  return _fullpath(resolved, path, PATH_MAX);
}

static inline char *fs_getcwd(char *buf, size_t size) {
  return _getcwd(buf, (int)size);
}

/* ── Terminal ────────────────────────────────────────── */

static inline int fs_isatty(int fd) {
  return _isatty(fd);
}

static inline int terminal_columns(void) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!h) return 80;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(h, &csbi))
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
  return 80;
}

/* ── ANSI escape support ──────────────────────────────── */

static inline void terminal_enable_ansi(void) {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h) {
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
      mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
      SetConsoleMode(h, mode);
    }
  }
}

/* ── Process (types for bash.c) ───────────────────────── */

typedef struct {
  HANDLE hProcess;
  HANDLE hThread;
  HANDLE hPipeRead;
} win32_process_t;

/* ── Networking init / cleanup ───────────────────────── */

static inline int net_init(void) {
  WSADATA wsa;
  return WSAStartup(MAKEWORD(2, 2), &wsa);
}

static inline void net_cleanup(void) {
  WSACleanup();
}

/* ── Threading abstraction (native Windows) ──────────── */

typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
typedef CONDITION_VARIABLE cond_t;

static inline int mutex_init(mutex_t *m) {
  InitializeCriticalSection(m);
  return 0;
}
static inline int mutex_destroy(mutex_t *m) {
  DeleteCriticalSection(m);
  return 0;
}
static inline int mutex_lock(mutex_t *m) {
  EnterCriticalSection(m);
  return 0;
}
static inline int mutex_unlock(mutex_t *m) {
  LeaveCriticalSection(m);
  return 0;
}
static inline int cond_init(cond_t *c) {
  InitializeConditionVariable(c);
  return 0;
}
static inline int cond_destroy(cond_t *c) {
  (void)c;
  return 0; /* CONDITION_VARIABLE needs no explicit destruction */
}
static inline int cond_signal(cond_t *c) {
  WakeConditionVariable(c);
  return 0;
}
static inline int cond_broadcast(cond_t *c) {
  WakeAllConditionVariable(c);
  return 0;
}
static inline int cond_wait(cond_t *c, mutex_t *m) {
  return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1;
}
static inline int cond_timedwait(cond_t *c, mutex_t *m,
                                  const struct timespec *abstime) {
  struct timespec now;
  timespec_get(&now, TIME_UTC);
  long long ms = (long long)(abstime->tv_sec - now.tv_sec) * 1000
               + (abstime->tv_nsec - now.tv_nsec) / 1000000;
  if (ms < 0) ms = 0;
  if (ms > (long long)INFINITE) ms = INFINITE;
  return SleepConditionVariableCS(c, m, (DWORD)ms) ? 0 : -1;
}

typedef struct {
  void *(*fn)(void *);
  void *arg;
} thread_wrapper_t;

static DWORD WINAPI thread_trampoline(LPVOID param) {
  thread_wrapper_t *tw = (thread_wrapper_t *)param;
  void *(*fn)(void *) = tw->fn;
  void *arg = tw->arg;
  free(tw);
  fn(arg);
  return 0;
}

static inline int thread_create(thread_t *t, void *(*fn)(void *), void *arg) {
  thread_wrapper_t *tw = malloc(sizeof(*tw));
  if (!tw) return -1;
  tw->fn = fn;
  tw->arg = arg;
  *t = CreateThread(NULL, 0, thread_trampoline, tw, 0, NULL);
  return (*t != NULL) ? 0 : -1;
}

static inline int thread_join(thread_t t) {
  DWORD rc = WaitForSingleObject(t, INFINITE);
  CloseHandle(t);
  return (rc == WAIT_OBJECT_0) ? 0 : -1;
}

/* ── Clocks ──────────────────────────────────────────── */

static inline int clock_monotonic(struct timespec *ts) {
  return timespec_get(ts, TIME_UTC);
}
static inline int clock_realtime(struct timespec *ts) {
  return timespec_get(ts, TIME_UTC);
}

#else /* ── Linux / POSIX ──────────────────────────────── */

#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <errno.h>
#include <limits.h>

/* ── Types ──────────────────────────────────────────── */

typedef int socket_t;
#define INVALID_SOCKET_VAL  (-1)

/* ── Socket helpers ──────────────────────────────────── */

#define socket_close(fd)    close(fd)
#define socket_errno()      errno
#define SOCKET_EINTR        EINTR
#define SOCKET_EAGAIN       EAGAIN

static inline int socket_set_timeout(socket_t fd, int timeout_sec) {
  struct timeval tv = { .tv_sec = timeout_sec, .tv_usec = 0 };
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* ── File helpers ────────────────────────────────────── */

#define fs_close(fd)        close(fd)
#define fs_unlink(path)     unlink(path)

static inline FILE *fs_fdopen(int fd, const char *mode) {
  return fdopen(fd, mode);
}

static inline int fs_mkstemp(char *tmpl) {
  return mkstemp(tmpl);
}

/* ── Directory / path ────────────────────────────────── */

static inline char *fs_realpath(const char *path, char *resolved) {
  return realpath(path, resolved);
}

static inline char *fs_getcwd(char *buf, size_t size) {
  return getcwd(buf, size);
}

/* ── Terminal ────────────────────────────────────────── */

static inline int fs_isatty(int fd) {
  return isatty(fd);
}

static inline int terminal_columns(void) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
    return ws.ws_col;
  return 80;
}

static inline void terminal_enable_ansi(void) {
  /* Linux terminals support ANSI by default — nothing to do */
  (void)0;
}

/* ── Networking init / cleanup (no-op on Linux) ───────── */

static inline int net_init(void) {
  return 0;
}

static inline void net_cleanup(void) {
  /* no-op */
}

/* ── Threading abstraction ────────────────────────────── */

#include <pthread.h>

typedef pthread_t       thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t  cond_t;

static inline int mutex_init(mutex_t *m) {
  return pthread_mutex_init(m, NULL);
}
static inline int mutex_destroy(mutex_t *m) {
  return pthread_mutex_destroy(m);
}
static inline int mutex_lock(mutex_t *m) {
  return pthread_mutex_lock(m);
}
static inline int mutex_unlock(mutex_t *m) {
  return pthread_mutex_unlock(m);
}
static inline int cond_init(cond_t *c) {
  return pthread_cond_init(c, NULL);
}
static inline int cond_destroy(cond_t *c) {
  return pthread_cond_destroy(c);
}
static inline int cond_signal(cond_t *c) {
  return pthread_cond_signal(c);
}
static inline int cond_broadcast(cond_t *c) {
  return pthread_cond_broadcast(c);
}
static inline int cond_wait(cond_t *c, mutex_t *m) {
  return pthread_cond_wait(c, m);
}
static inline int cond_timedwait(cond_t *c, mutex_t *m,
                                  const struct timespec *abstime) {
  return pthread_cond_timedwait(c, m, abstime);
}
static inline int thread_create(thread_t *t, void *(*fn)(void *), void *arg) {
  return pthread_create(t, NULL, fn, arg);
}
static inline int thread_join(thread_t t) {
  return pthread_join(t, NULL);
}

/* ── Clocks ──────────────────────────────────────────── */

static inline int clock_monotonic(struct timespec *ts) {
  return clock_gettime(CLOCK_MONOTONIC, ts);
}
static inline int clock_realtime(struct timespec *ts) {
  return clock_gettime(CLOCK_REALTIME, ts);
}

#endif /* _WIN32 */

/*
 * ── Compiler abstraction ───────────────────────────────
 *
 * __attribute__ is a GCC/Clang extension. MSVC uses __declspec for similar
 * purposes, but for the format check there is no MSVC equivalent; just
 * silence it so the code compiles.
 */
#if defined(__GNUC__) || defined(__clang__)
  #define ATTR_PRINTF(fmt_idx, arg_idx) \
    __attribute__((format(printf, fmt_idx, arg_idx)))
#else
  #define ATTR_PRINTF(fmt_idx, arg_idx)
#endif

#endif /* COMPAT_H */
