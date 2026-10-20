#include <device/misc/test_finisher.h>

#define FINISHER_MAX 8

struct TestFinisher {
  void (*finish)(void* opaque, int code);
  void* finish_opaque;
};

static TestFinisher pool[FINISHER_MAX];
static bool used[FINISHER_MAX];

static TestFinisher* finisher_alloc(void) {
  for (int i = 0; i < FINISHER_MAX; i++) {
    if (!used[i]) {
      used[i] = true;
      memset(&pool[i], 0, sizeof(pool[i]));
      return &pool[i];
    }
  }
  return NULL;
}

static bool finisher_owned(const TestFinisher* finisher) {
  return finisher != NULL && finisher >= pool && finisher < pool + FINISHER_MAX;
}

static bool finisher_accepts(paddr_t offset, int length) {
  return offset == 0 && (length == 2 || length == 4);
}

static bool finisher_read(void* ctx, paddr_t offset, int length, word_t* data) {
  (void)ctx;
  if (!finisher_accepts(offset, length)) return false;
  *data = 0;
  return true;
}

static bool finisher_write(void* ctx, paddr_t offset, int length, word_t data) {
  TestFinisher* finisher = ctx;
  if (!finisher_accepts(offset, length)) return false;
  unsigned status = data & 0xffff;
  unsigned code = (data >> 16) & 0xffff;
  if (finisher->finish != NULL && (status == 0x5555 || status == 0x3333)) {
    finisher->finish(finisher->finish_opaque,
                     status == 0x5555 ? (int)code
                                      : (code != 0 ? (int)code : 1));
  }
  return true;
}

TestFinisher* test_finisher_create(void (*finish)(void* opaque, int code),
                                   void* opaque) {
  TestFinisher* finisher = finisher_alloc();
  if (finisher == NULL) return NULL;
  finisher->finish = finish;
  finisher->finish_opaque = opaque;
  return finisher;
}

void test_finisher_release(TestFinisher* finisher) {
  if (!finisher_owned(finisher)) return;
  used[finisher - pool] = false;
}

void test_finisher_mmio(TestFinisher* finisher, MemObject* mmio) {
  *mmio = (MemObject){.name = "test-finisher",
                      .ctx = finisher,
                      .read = finisher_read,
                      .write = finisher_write};
}
