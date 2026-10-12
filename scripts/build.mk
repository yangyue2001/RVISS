#***************************************************************************************
# 通用编译规则：主工程与 tools/ 下的构建工具共用（NEMU 风格）。
#
# 使用方式：包含本文件前先定义
#   NAME      产物名（obj 目录与二进制名都由它派生）
#   SRCS      源文件列表（相对当前目录的路径）
# 可选：INC_DIR / CFLAGS / LDFLAGS / LIBS / OBJ_DIR / BINARY
#
# 产物布局：
#   build/obj-<NAME>/...（保持源码相对路径）  —— 对象文件
#   build/<NAME>                              —— 最终产物
#***************************************************************************************

.DEFAULT_GOAL = app

WORK_DIR   = $(shell pwd)
BUILD_DIR ?= $(WORK_DIR)/build
OBJ_DIR   ?= $(BUILD_DIR)/obj-$(NAME)
BINARY    ?= $(BUILD_DIR)/$(NAME)

# 注意：Make 内建变量 CC 默认就是 "cc"（origin 为 default），
# 所以不能用"是否为空"来判断用户有没有指定编译器；要用 $(origin CC)。
# 命令行/环境传入的 CC 会被尊重，其余情况明确用 gcc。
ifeq ($(origin CC),default)
CC := gcc
endif

INCLUDES = $(addprefix -I, $(INC_DIR))
CFLAGS  := -MMD -Wall $(INCLUDES) $(CFLAGS)

COBJS = $(SRCS:%.c=$(OBJ_DIR)/%.o)
OBJS  = $(COBJS)

# 编译一条 .c：-MMD 生成依赖；fixdep 再把"该文件用到了哪些 CONFIG_"写进 .d，
# 这样改一个配置项只会重编真正受影响的对象（见 scripts/config.mk）。
$(OBJ_DIR)/%.o: %.c
	@echo + CC $<
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c -o $@ $<
	$(call call_fixdep, $(@:.o=.d), $@)

-include $(OBJS:.o=.d)

app: $(BINARY)

$(BINARY): $(OBJS)
	@echo + LD $@
	@mkdir -p $(dir $@)
	@$(CC) -o $@ $(OBJS) $(LDFLAGS) $(LIBS)

clean:
	-rm -rf $(BUILD_DIR)

.PHONY: app clean
