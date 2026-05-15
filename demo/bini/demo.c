#include <stdio.h>
#include <unistd.h>

void gets_replica(char *buf) {
  // Simulate a vulnerable function that reads input into a buffer
  read(STDIN_FILENO, buf, 128); // intentionally unsafe: can overflow buf
}

void vuln() {
  char buf[64];

  puts("Enter some text:");
  gets_replica(buf); // intentionally unsafe: vulnerable to stack overflow
  puts("Done.");
  __asm__("int3");
}

int main() {
  // Disable buffering so output appears immediately
  setbuf(stdout, NULL);

  // Print a libc address to make demo easier (simulates an info leak)
  printf("puts() is at: %p\n", puts);

  vuln();

  return 0;
}
