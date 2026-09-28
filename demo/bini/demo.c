#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

char *gets_replica(char *buf) {
  // Simulate a vulnerable function that reads input into a buffer
  // read(STDIN_FILENO, buf, 128); // intentionally unsafe: can overflow buf
  __asm__("mov eax, 0x0\n"
          "mov edi, 0x0\n"
          "mov esi, %0\n"
          "mov edx, 0x80\n"
          "syscall\n"
          :
          : "r"(buf)
          : "eax", "edi", "esi", "edx");
  return buf;
}

void vuln() {
  char buf[64] = {};
  gets_replica(buf); // intentionally unsafe: vulnerable to stack overflow
  __asm__("int3");
}

int main() {
  // Disable buffering so output appears immediately
  setbuf(stdout, NULL);

  // Print a libc address to make demo easier (simulates an info leak)
  printf("system() is at: %p\n", system);

  vuln();

  return 0;
}
