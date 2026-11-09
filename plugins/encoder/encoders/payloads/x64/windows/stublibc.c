typedef unsigned long long size_t;

#define NULL ((void *)0)

void *memset(void *dst, int ch, size_t n) {
  for (size_t i = 0; i < n; i++) {
    ((char *)dst)[i] = (char)ch;
  }
  return dst;
}

void *memcpy(void *__restrict__ dst, const void *__restrict__ src, size_t n) {
  for (size_t i = 0; i < n; i++) {
    ((char *)dst)[i] = ((const char *)src)[i];
  }
  return dst;
}

int strncmp(const char *lsh, const char *rsh, size_t n) {
  if (n == 0) {
    return 0;
  }
  for (unsigned long long i = 0; i < n; i++) {
    if (lsh[i] != rsh[i]) {
      return (unsigned char)lsh[i] - (unsigned char)rsh[i];
    }
    if (lsh[i] == '\0') {
      return 0;
    }
  }
  return 0;
}

size_t strlen(const char *str) {
  size_t len = 0;
  while (str[len] != '\0') {
    len++;
  }
  return len;
}

char *strncpy(char *__restrict__ dest, const char *__restrict__ src, size_t count) {
  size_t i;
  for (i = 0; i < count && src[i] != '\0'; i++) {
    dest[i] = src[i];
  }
  for (; i < count; i++) {
    dest[i] = '\0';
  }
  return dest;
}

char *strncat(char *__restrict__ dest, const char *__restrict__ src, size_t count) {
  size_t dest_len = strlen(dest);
  size_t i;
  for (i = 0; i < count && src[i] != '\0'; i++) {
    dest[dest_len + i] = src[i];
  }
  dest[dest_len + i] = '\0';
  return dest;
}

void *malloc(size_t size) { return HeapAlloc(GetProcessHeap(), 0, size); }

void free(void *ptr) { HeapFree(GetProcessHeap(), 0, ptr); }

void *calloc(size_t num, size_t size) {
  size_t total_size = num * size;
  void *ptr = malloc(total_size);
  if (ptr) {
    memset(ptr, 0, total_size);
  }
  return ptr;
}

void *realloc(void *ptr, size_t new_size) {
  if (ptr == NULL) {
    return malloc(new_size);
  }
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  void *new_ptr = malloc(new_size);
  if (new_ptr) {
    memcpy(new_ptr, ptr, new_size);
    free(ptr);
  }
  return new_ptr;
}

// Vim: set expandtab tabstop=2 shiftwidth=2:
