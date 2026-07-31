#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static uint64_t next_u64(uint64_t *state) {
  uint64_t x = *state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  *state = x;
  return x;
}

static int sparse_mapping_probe(void) {
  const uint64_t logical_size = 15762598656ULL; /* about 14.68 GiB */
  const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  const uint64_t page_count = logical_size / page_size;
  const uint64_t touches_per_pass = 32768; /* 128 MiB of pages per pass */
  int fd = open("ppm.temp", O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) { perror("open ppm.temp"); return 1; }
  if (ftruncate(fd, (off_t)logical_size) != 0) {
    perror("ftruncate ppm.temp"); close(fd); return 1;
  }
  unsigned char *map = mmap(NULL, logical_size, PROT_READ | PROT_WRITE,
                            MAP_SHARED, fd, 0);
  if (map == MAP_FAILED) {
    perror("mmap ppm.temp"); close(fd); return 1;
  }

  uint64_t state = 0x6a09e667f3bcc909ULL;
  uint64_t checksum = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (uint64_t i = 0; i < touches_per_pass; ++i) {
      uint64_t page = next_u64(&state) % page_count;
      size_t offset = (size_t)(page * page_size);
      unsigned char value = (unsigned char)(i + 31 * pass);
      map[offset] ^= value;
      checksum += map[offset];
    }
    if (msync(map, logical_size, MS_SYNC) != 0) perror("msync");
    if (madvise(map, logical_size, MADV_DONTNEED) != 0) perror("madvise");
  }

  printf("sparse_mapping_ok logical_bytes=%" PRIu64
         " page_size=%zu touches=%" PRIu64 " checksum=%" PRIu64 "\n",
         logical_size, page_size, touches_per_pass * 2, checksum);
  munmap(map, logical_size);
  close(fd);
  return 0;
}

static int resident_memory_probe(void) {
  const uint64_t bytes = 9ULL * 1024ULL * 1024ULL * 1024ULL;
  const size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  unsigned char *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (map == MAP_FAILED) { perror("mmap 9GiB"); return 1; }
  uint64_t checksum = 0;
  for (uint64_t offset = 0; offset < bytes; offset += page_size) {
    map[offset] = (unsigned char)(offset / page_size);
    checksum += map[offset];
  }
  printf("resident_memory_ok bytes=%" PRIu64 " checksum=%" PRIu64 "\n",
         bytes, checksum);
  sleep(3);
  munmap(map, bytes);
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s sparse|rss\n", argv[0]);
    return 2;
  }
  if (strcmp(argv[1], "sparse") == 0) return sparse_mapping_probe();
  if (strcmp(argv[1], "rss") == 0) return resident_memory_probe();
  fprintf(stderr, "unknown probe: %s\n", argv[1]);
  return 2;
}
