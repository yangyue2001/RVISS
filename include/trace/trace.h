#ifndef RVISS_TRACE_TRACE_H
#define RVISS_TRACE_TRACE_H

#include <cpu/step.h>

#define RVISS_TRACE_HEADER_SIZE 64u
#define RVISS_TRACE_RECORD_SIZE 104u

/* 打开/关闭独立的 RVTR 二进制 trace sink。path == NULL 表示不记录。 */
bool trace_init(const char *path);
bool trace_close(void);

/* 每个执行事件一条固定布局记录；operation 指针不会写入文件。 */
void trace_emit_record(const StepResult *result);

/* 未编译 trace sink 时，调用点被预处理器删除，保持默认路径零开销。 */
#ifdef CONFIG_TRACE_STREAM
#define trace_emit(result) trace_emit_record(result)
#else
#define trace_emit(result) ((void)0)
#endif

#endif /* RVISS_TRACE_TRACE_H */
