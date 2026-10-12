#include <monitor/monitor.h>

/*
 * 入口只做两件事：初始化 monitor（解析参数、建机器、装镜像）、跑。
 * 退出码：0 = HIT GOOD TRAP。
 */

int main(int argc, char* argv[]) {
  monitor_init(argc, argv);
  return monitor_run();
}
