#include <util.h>

// Utilities used inside source code
// Copyright (C) 2024 Panagiotis

#define LONG_MASK (sizeof(unsigned long) - 1)
void memset(void *_dst, int val, size_t len) {
  asm volatile("rep stosb" : : "D"(_dst), "a"(val), "c"(len) : "memory");
}

void *memcpy(void *restrict dstptr, const void *restrict srcptr, size_t size) {
  asm volatile("rep movsb" : : "S"(srcptr), "D"(dstptr), "c"(size) : "memory");
  return dstptr;
}

void *memmove(void *dstptr, const void *srcptr, size_t size) {
  unsigned char       *dst = (unsigned char *)dstptr;
  const unsigned char *src = (const unsigned char *)srcptr;
  if (dst < src) {
    for (size_t i = 0; i < size; i++)
      dst[i] = src[i];
  } else {
    for (size_t i = size; i != 0; i--)
      dst[i - 1] = src[i - 1];
  }
  return dstptr;
}

int memcmp(const void *aptr, const void *bptr, size_t size) {
  const unsigned char *a = (const unsigned char *)aptr;
  const unsigned char *b = (const unsigned char *)bptr;
  for (size_t i = 0; i < size; i++) {
    if (a[i] < b[i])
      return -1;
    else if (b[i] < a[i])
      return 1;
  }
  return 0;
}

static unsigned long int next = 1;

int rand(void) {
  next = next * 1103515245 + 12345;
  return (unsigned int)(next / 65536) % 32768;
}

void srand(unsigned int seed) { next = seed; }

// https://stackoverflow.com/questions/7775991/how-to-get-hexdump-of-a-structure-data
// cool function
void hexDump(const char *desc, const void *addr, const int len, int perLine) {
  int                  i;
  unsigned char        buff[perLine + 1];
  const unsigned char *pc = (const unsigned char *)addr;

  if (desc != NULL)
    printf("%s:\n", desc);

  if (len == 0) {
    printf("  ZERO LENGTH\n");
    return;
  }
  if (len < 0) {
    printf("  NEGATIVE LENGTH: %d\n", len);
    return;
  }

  for (i = 0; i < len; i++) {
    if ((i % perLine) == 0) {
      if (i != 0)
        printf("  %s\n", buff);

      printf("  %04x ", i);
    }

    printf(" %02x", pc[i]);

    if ((pc[i] < 0x20) || (pc[i] > 0x7e))
      buff[i % perLine] = '.';
    else
      buff[i % perLine] = pc[i];
    buff[(i % perLine) + 1] = '\0';
  }

  while ((i % perLine) != 0) {
    printf("   ");
    i++;
  }

  printf("  %s\n", buff);
}
