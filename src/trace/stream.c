#include <common.h>
#include <stdio.h>
#include <trace/trace.h>
#include <util/log.h>

#define TRACE_BUFFER_SIZE (1u << 20)

static FILE* trace_fp;
static uint8_t trace_buffer[TRACE_BUFFER_SIZE];
static size_t trace_used;
static bool trace_failed;

static void put_u16(uint8_t* p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static void put_u32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static void put_u64(uint8_t* p, uint64_t value) {
  for (unsigned i = 0; i < 8; i++) p[i] = (uint8_t)(value >> (8 * i));
}

static bool flush_buffer(void) {
  if (trace_fp == NULL || trace_used == 0) return true;
  const bool ok = fwrite(trace_buffer, 1, trace_used, trace_fp) == trace_used;
  trace_used = 0;
  if (!ok) trace_failed = true;
  return ok;
}

static bool append_bytes(const uint8_t* data, size_t size) {
  if (trace_fp == NULL || trace_failed) return false;
  while (size != 0) {
    const size_t available = TRACE_BUFFER_SIZE - trace_used;
    const size_t count = size < available ? size : available;
    memcpy(trace_buffer + trace_used, data, count);
    trace_used += count;
    data += count;
    size -= count;
    if (trace_used == TRACE_BUFFER_SIZE && !flush_buffer()) return false;
  }
  return true;
}

bool trace_close(void) {
  if (trace_fp == NULL) return !trace_failed;
  const bool flushed = flush_buffer();
  const bool closed = fclose(trace_fp) == 0;
  trace_fp = NULL;
  const bool ok = flushed && closed && !trace_failed;
  trace_failed = false;
  trace_used = 0;
  return ok;
}

bool trace_init(const char* path) {
#ifdef CONFIG_TRACE_STREAM
  CONFIG_TRACE_STREAM, trace_fp = fopen(path, "wb");
  if (trace_fp == NULL) {
    Loge("cannot open trace file %s", path);
    return false;
  }
#else
  if (path != NULL) {
    Logw("--trace-file requires CONFIG_TRACE_STREAM; trace disabled");
    return false;
  }
#endif

  uint8_t header[RVISS_TRACE_HEADER_SIZE] = {0};
  memcpy(header, "RVTR", 4);
  put_u16(header + 4, 1); /* format version */
  put_u16(header + 6, RVISS_TRACE_HEADER_SIZE);
  put_u32(header + 8, RVISS_TRACE_RECORD_SIZE);
  header[12] = 1;  /* little endian */
  header[13] = 64; /* RV64 */
  /* Header currently carries the stable ISA width only; profile-specific
   * capability bits can be added without changing the record layout. */
  memcpy(header + 16, "rv64", 4);
  if (!append_bytes(header, sizeof(header))) {
    trace_close();
    Loge("cannot write trace header %s", path);
    return false;
  }
  return true;
}

void trace_emit_record(const StepResult* r) {
  if (trace_fp == NULL || trace_failed) return;

  uint8_t record[RVISS_TRACE_RECORD_SIZE] = {0};
  put_u64(record + 0, r->event_seq);
  put_u64(record + 8, r->retired_total);
  put_u64(record + 16, r->pc_before);
  put_u64(record + 24, r->pc_after);
  put_u32(record + 32, r->raw_instruction);
  record[36] = r->instruction_length;
  record[37] = r->raw_bytes;
  record[38] = r->privilege_before;
  record[39] = r->privilege_after;
  record[40] = (uint8_t)r->outcome;
  record[41] = r->rd;
  record[42] = (uint8_t)r->memory.type;
  record[43] = (uint8_t)r->memory.length;

  uint16_t flags = 0;
  if (r->instruction_attempted) flags |= 1u << 0;
  if (r->retired) flags |= 1u << 1;
  if (r->wait_request) flags |= 1u << 2;
  if (r->instruction_valid) flags |= 1u << 3;
  if (r->register_write) flags |= 1u << 4;
  if (r->register_is_fp) flags |= 1u << 5;
  if (r->memory.valid) flags |= 1u << 6;
  if (r->memory.mmio) flags |= 1u << 7;
  if (r->memory.write_performed) flags |= 1u << 8;
  if (r->trap.valid) flags |= 1u << 9;
  if (r->memory.paddr_valid) flags |= 1u << 10;
  put_u16(record + 44, flags);
  put_u64(record + 48, r->register_value);
  put_u64(record + 56, r->memory.vaddr);
  put_u64(record + 64, r->memory.paddr);
  put_u64(record + 72, r->memory.data);
  put_u64(record + 80, r->trap.cause);
  put_u64(record + 88, r->trap.epc);
  put_u64(record + 96, r->trap.tval);

  (void)append_bytes(record, sizeof(record));
}
