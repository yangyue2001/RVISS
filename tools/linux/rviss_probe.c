#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* 真实用户态路径的 smoke；完整 ISA 正确性仍由独立的架构/差分测试证明。 */
int main(void) {
  double da = 1.5, db = 2.25, dc;
  float fa = 1.5f, fb = 2.25f, fc;
  __asm__ volatile("fadd.d %0, %1, %2" : "=f"(dc) : "f"(da), "f"(db));
  __asm__ volatile("fadd.s %0, %1, %2" : "=f"(fc) : "f"(fa), "f"(fb));
  if (dc != 3.75 || fc != 3.75f) return 10;

  uint64_t atom = 7;
  if (__atomic_fetch_add(&atom, 5, __ATOMIC_SEQ_CST) != 7 || atom != 12)
    return 11;
  uint64_t old, failed;
  __asm__ volatile("1: lr.d %0, (%2)\n"
                   "addi %0, %0, 1\n"
                   "sc.d %1, %0, (%2)\n"
                   "bnez %1, 1b"
                   : "=&r"(old), "=&r"(failed) : "r"(&atom) : "memory");
  if (atom != 13) return 12;

  const size_t size = 256 * 1024;
  volatile uint8_t *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED) return 13;
  for (size_t i = 0; i < size; i += 4096) mem[i] = (uint8_t)(i / 4096);
  pid_t pid = fork();
  if (pid < 0) return 14;
  if (pid == 0) {
    for (size_t i = 0; i < size; i += 4096)
      if (mem[i] != (uint8_t)(i / 4096)) _exit(20);
    mem[0] = 99; /* 触发 COW，父进程数据必须保持不变。 */
    _exit(0);
  }
  int status;
  if (waitpid(pid, &status, 0) != pid || !WIFEXITED(status) ||
      WEXITSTATUS(status) != 0 || mem[0] != 0) return 15;
  if (munmap((void *)mem, size) != 0) return 16;

  struct timespec begin, end, delay = {.tv_sec = 0, .tv_nsec = 10000000};
  if (clock_gettime(CLOCK_MONOTONIC, &begin) != 0) return 17;
  while (nanosleep(&delay, &delay) != 0) if (errno != EINTR) return 18;
  if (clock_gettime(CLOCK_MONOTONIC, &end) != 0) return 19;
  int64_t elapsed = (end.tv_sec - begin.tv_sec) * INT64_C(1000000000) +
                    end.tv_nsec - begin.tv_nsec;
  if (elapsed < 10000000) return 21;
  puts("RVISS_PROBE_OK fp32 fp64 amo lrsc mmap cow fork timer");
  return 0;
}
