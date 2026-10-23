#ifndef RVISS_MONITOR_MONITOR_H
#define RVISS_MONITOR_MONITOR_H

/*
 * monitor：命令行入口 + 交互调试器。
 * 是唯一"从上往下"看整个系统的模块（main.c 只调它）。
 */

void monitor_init(int argc, char *argv[]); /* 解析参数、复位、建机器；镜像在引擎里装 */
int  monitor_run(void);                    /* 批处理或进交互；返回退出码 */

#endif /* RVISS_MONITOR_MONITOR_H */
