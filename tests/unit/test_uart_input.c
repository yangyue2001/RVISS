#include <host/host.h>
#include <unistd.h>
#include "test.h"

int main(void) {
  char path[]="/tmp/rviss-uart-input-XXXXXX";
  int temp=mkstemp(path);CHECK(temp>=0);close(temp);
  int pipefd[2];CHECK_EQ(pipe(pipefd),0);
  int saved=dup(STDIN_FILENO);CHECK(saved>=0);CHECK_EQ(dup2(pipefd[0],STDIN_FILENO),STDIN_FILENO);
  close(pipefd[0]);CHECK_EQ(write(pipefd[1],"a\0Z",3),3);close(pipefd[1]);
  CHECK(host_uart_input_init(path,NULL));uint8_t bytes[8]={0};
  CHECK_EQ(host_uart_input(128,bytes,8),3);CHECK_EQ(bytes[0],'a');CHECK_EQ(bytes[1],0);CHECK_EQ(bytes[2],'Z');
  CHECK_EQ(host_uart_input(129,bytes,8),0);host_uart_input_close();
  CHECK_EQ(dup2(saved,STDIN_FILENO),STDIN_FILENO);close(saved);
  CHECK(host_uart_input_init(NULL,path));CHECK(host_uart_replaying());
  CHECK_EQ(host_uart_input(127,bytes,8),0);CHECK_EQ(host_uart_input(128,bytes,8),3);
  CHECK_EQ(bytes[0],'a');CHECK_EQ(bytes[1],0);CHECK_EQ(bytes[2],'Z');CHECK_EQ(host_uart_input(129,bytes,8),0);
  host_uart_input_close();CHECK(!host_uart_replaying());
  CHECK(host_uart_input_init(NULL,path));CHECK_EQ(host_uart_input(129,bytes,8),-1);host_uart_input_close();
  CHECK(host_uart_input_init(NULL,path));CHECK_EQ(host_uart_input(128,bytes,2),-1);host_uart_input_close();
  FILE *f=fopen(path,"w");CHECK(f!=NULL);fputs("128 61\n127 62\n",f);fclose(f);
  CHECK(!host_uart_input_init(NULL,path));CHECK(!host_uart_replaying());
  f=fopen(path,"w");CHECK(f!=NULL);fputs("128 100\n",f);fclose(f);
  CHECK(!host_uart_input_init(NULL,path));
  f=fopen(path,"w");CHECK(f!=NULL);fputs("128 61 extra\n",f);fclose(f);
  CHECK(!host_uart_input_init(NULL,path));host_uart_input_close();unlink(path);
  TEST_MAIN_END();
}
