#include <common.h>
#include <errno.h>
#include <host/host.h>
#include <poll.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <util/log.h>

/*
 * 宿主环境实现。客户机执行本身不依赖这里的时间（用指令计数保证确定性），
 * 这里只服务于"宿主侧"的观测/统计。
 */

uint64_t host_time_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000ull + (uint64_t)tv.tv_usec;
}

void* host_mmap_anon(size_t size) {
  void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? NULL : p;
}

int host_munmap(void* addr, size_t size) { return munmap(addr, size); }

bool host_read_file(const char* path, void** buf, size_t* size) {
  FILE* fp = fopen(path, "rb");
  if (fp == NULL) {
    Loge("无法打开文件 %s", path);
    return false;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return false;
  }
  const long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return false;
  }
  rewind(fp);

  void* p = malloc((size_t)sz); /* 0 字节文件也返回可释放指针 */
  Assert(p != NULL, "分配 %ld 字节失败", sz);

  if (sz > 0 && fread(p, 1, (size_t)sz, fp) != (size_t)sz) {
    fclose(fp);
    free(p);
    return false;
  }
  fclose(fp);

  *buf = p;
  *size = (size_t)sz;
  return true;
}

int host_stdin_read(uint8_t* bytes, unsigned capacity) {
  if (!capacity) return 0;
  struct pollfd fd = {.fd = STDIN_FILENO, .events = POLLIN};
  int ready = poll(&fd, 1, 0);
  if (ready < 0) return errno == EINTR ? 0 : -1;
  if (!ready || !(fd.revents & (POLLIN | POLLHUP))) return 0;
  ssize_t n = read(STDIN_FILENO, bytes, capacity);
  return n < 0 ? ((errno == EINTR || errno == EAGAIN) ? 0 : -1) : (int)n;
}
