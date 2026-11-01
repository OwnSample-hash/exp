/*
 * example_stub.c
 *
 * A trivial, self-contained routine used to exercise the encoder's
 * "compile source to a flat binary blob" pipeline. It is intentionally benign:
 * it just sums the integers below its argument and returns the result. The
 * purpose of this file is to drive the toolchain + objcopy(.text) extraction
 * path end to end, not to do anything on its own.
 *
 * Built freestanding so no C runtime/startup is required; only the .text of
 * entry() is extracted into the blob.
 */
int entry(int n) {
  int acc = 0;
  for (int i = 0; i < n; ++i)
    acc += i;
  return acc;
}
