#include <stdio.h>
#include <trace/trace.h>
#include <unistd.h>

#include "test.h"

static uint16_t get_u16(const uint8_t *p) {
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t get_u32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; i++) value |= (uint32_t)p[i] << (8 * i);
  return value;
}

static uint64_t get_u64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; i++) value |= (uint64_t)p[i] << (8 * i);
  return value;
}

int main(void) {
  char path[] = "/tmp/rviss-trace-XXXXXX";
  const int fd = mkstemp(path);
  CHECK(fd >= 0);
  close(fd);

  StepResult result = {.event_seq = 17,
                       .retired_total = 12,
                       .outcome = STEP_RETIRED,
                       .instruction_attempted = true,
                       .retired = true,
                       .pc_before = UINT64_C(0x80000000),
                       .pc_after = UINT64_C(0x80000004),
                       .privilege_before = 3,
                       .privilege_after = 3,
                       .raw_instruction = UINT32_C(0x00100093),
                       .instruction_length = 4,
                       .raw_bytes = 4,
                       .instruction_valid = true,
                       .register_write = true,
                       .rd = 1,
                       .register_value = 1};

  CHECK(trace_init(path));
  trace_emit_record(&result);
  CHECK(trace_close());

  FILE *file = fopen(path, "rb");
  CHECK(file != NULL);
  CHECK_EQ(fseek(file, 0, SEEK_END), 0);
  CHECK_EQ((long)ftell(file),
           (long)(RVISS_TRACE_HEADER_SIZE + RVISS_TRACE_RECORD_SIZE));
  CHECK_EQ(fseek(file, 0, SEEK_SET), 0);
  uint8_t bytes[RVISS_TRACE_HEADER_SIZE + RVISS_TRACE_RECORD_SIZE];
  CHECK_EQ(fread(bytes, 1, sizeof(bytes), file), sizeof(bytes));
  fclose(file);

  CHECK(memcmp(bytes, "RVTR", 4) == 0);
  CHECK_EQ(get_u16(bytes + 4), 1);
  CHECK_EQ(get_u16(bytes + 6), RVISS_TRACE_HEADER_SIZE);
  CHECK_EQ(get_u32(bytes + 8), RVISS_TRACE_RECORD_SIZE);
  CHECK_EQ(bytes[12], 1);
  CHECK_EQ(bytes[13], 64);
  CHECK_EQ(get_u64(bytes + RVISS_TRACE_HEADER_SIZE), result.event_seq);
  CHECK_EQ(get_u64(bytes + RVISS_TRACE_HEADER_SIZE + 8), result.retired_total);
  CHECK_EQ(get_u64(bytes + RVISS_TRACE_HEADER_SIZE + 16), result.pc_before);
  CHECK_EQ(get_u32(bytes + RVISS_TRACE_HEADER_SIZE + 32), result.raw_instruction);
  CHECK(bytes[RVISS_TRACE_HEADER_SIZE + 44] & (1u << 0));
  CHECK(bytes[RVISS_TRACE_HEADER_SIZE + 44] & (1u << 1));
  CHECK_EQ(get_u64(bytes + RVISS_TRACE_HEADER_SIZE + 48), result.register_value);

  unlink(path);
  TEST_MAIN_END();
}
