#include <host/host.h>
#include <inttypes.h>
#include <util/log.h>

static FILE* record_file;
static struct {
  uint64_t event;
  uint8_t byte;
}* replay_data;
static size_t replay_count, replay_next;
static bool replay_enabled;

void host_uart_input_close(void) {
  if (record_file) fclose(record_file);
  record_file = NULL;
  free(replay_data);
  replay_data = NULL;
  replay_count = replay_next = 0;
  replay_enabled = false;
}
bool host_uart_replaying(void) { return replay_enabled; }

bool host_uart_input_init(const char* record, const char* replay) {
  host_uart_input_close();
  if (record) {
    record_file = fopen(record, "w");
    if (!record_file) return false;
  }
  if (!replay) return true;
  FILE* f = fopen(replay, "r");
  if (!f) return false;
  char line[128];
  uint64_t previous = 0;
  while (fgets(line, sizeof(line), f)) {
    uint64_t event;
    unsigned byte;
    char extra;
    if (sscanf(line, "%" SCNu64 " %x %c", &event, &byte, &extra) != 2 ||
        byte > 255 || event < previous || replay_count >= 1000000) {
      fclose(f);
      host_uart_input_close();
      return false;
    }
    void* data =
        realloc(replay_data, (replay_count + 1) * sizeof(*replay_data));
    if (!data) {
      fclose(f);
      host_uart_input_close();
      return false;
    }
    replay_data = data;
    replay_data[replay_count].event = event;
    replay_data[replay_count++].byte = byte;
    previous = event;
  }
  bool ok = !ferror(f);
  fclose(f);
  if (!ok) {
    host_uart_input_close();
    return false;
  }
  replay_enabled = true;
  return true;
}
int host_uart_input(uint64_t event, uint8_t* bytes, unsigned capacity) {
  if (replay_enabled) {
    unsigned n = 0;
    while (replay_next < replay_count &&
           replay_data[replay_next].event <= event) {
      if (replay_data[replay_next].event != event || n == capacity) {
        Loge("UART replay diverged at event=%lu input=%zu", event, replay_next);
        return -1;
      }
      bytes[n++] = replay_data[replay_next++].byte;
    }
    return (int)n;
  }
  int n = host_stdin_read(bytes, capacity);
  if (record_file && n > 0) {
    for (int i = 0; i < n; i++)
      if (fprintf(record_file, "%" PRIu64 " %02x\n", event, bytes[i]) < 0)
        return -1;
    if (fflush(record_file)) return -1;
  }
  return n;
}
