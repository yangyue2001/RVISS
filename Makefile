#***************************************************************************************
# RVISS 构建入口（Makefile + Kconfig，NEMU / Linux 风格）
#
#   make menuconfig          交互式配置
#   make xxx_defconfig       套用预设（configs/xxx_defconfig）
#   make -j                  编译
#   make check               编译并运行测试（单元测试 + 冒烟测试）
#   make run  IMG=xxx.elf    运行镜像
#   make gdb  IMG=xxx.elf    用 gdb 跑
#   make clean / distclean   清理
#
# 注意：本 Makefile 自定位（RVISS_HOME），不依赖也不使用外部环境变量，
#       避免与其它 NEMU 工程的 NEMU_HOME 混淆。
#***************************************************************************************

RVISS_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
export RVISS_HOME

# Kconfig 产出的配置（首次构建前不存在，靠 defconfig/menuconfig 生成）
-include $(RVISS_HOME)/include/config/auto.conf
-include $(RVISS_HOME)/include/config/auto.conf.cmd

ifeq ($(wildcard $(RVISS_HOME)/.config),)
$(warning 未找到 .config —— 请先运行 `make menuconfig` 或 `make minimal_defconfig`)
endif

remove_quote = $(patsubst "%",%,$(1))

# ---------------------------------------------------------------- 目标与引擎
ISA    ?= riscv64
ENGINE ?= $(call remove_quote,$(CONFIG_ENGINE))
CFLAGS += -D__ISA__=$(ISA)

# ---------------------------------------------------------------- 头文件路径
INC_DIR  = $(RVISS_HOME)/include
INC_DIR += $(RVISS_HOME)/src/target/$(ISA)
INC_DIR += $(RVISS_HOME)/src/target/$(ISA)/local_include

# ---------------------------------------------------------------- 源文件收集
# 目录整体收集（NEMU 风格：新增文件不用改 Makefile）
DIRS-y  = src/util src/host src/memory src/machine src/monitor
DIRS-y += src/checkpoint src/difftest
DIRS-y += src/engine/$(ENGINE)
DIRS-y += src/target/$(ISA)

# 逐文件收集：这里体现 Kconfig 的作用 —— 改配置会改变编进去的设备
# 设备按类别分子目录（QEMU 风格：char/timer/intc），bus.c 是设备框架本身
SRCS-y  = src/main.c
SRCS-y += src/device/bus.c
SRCS-$(CONFIG_HAS_CLINT)     += src/device/timer/clint.c
SRCS-$(CONFIG_HAS_UART16550) += src/device/char/uart16550.c
SRCS-$(CONFIG_HAS_PLIC)      += src/device/intc/plic.c

SRCS-y += $(shell find $(DIRS-y) -name "*.c")
SRCS = $(SRCS-y)

# ---------------------------------------------------------------- 编译选项
CFLAGS += $(call remove_quote,$(CONFIG_CC_OPT))
CFLAGS += $(if $(CONFIG_CC_DEBUG),-ggdb3,)
CFLAGS += $(if $(CONFIG_CC_LTO),-flto=auto,)
CFLAGS += $(if $(CONFIG_CC_STRICT),-Werror,)
CFLAGS += $(if $(CONFIG_CC_NATIVE),-march=native,)

NAME = rviss

# ---------------------------------------------------------------- 参考库模式
# 必须在 include build.mk 之前定义：build.mk 里的 `app: $(BINARY)` 会在
# 定义规则时就展开 BINARY（Make 的 prerequisites 是立即展开的）。
# CONFIG_SHARE=y：产出 librviss_ref.so 骨架；ABI 可用性需独立契约测试
ifdef CONFIG_SHARE
CFLAGS  += -fPIC -D_SHARE=1
LDFLAGS += -shared -fPIC
OBJ_DIR  = $(BUILD_DIR)/obj-rviss-ref
BINARY   = $(BUILD_DIR)/librviss_ref.so
endif

include $(RVISS_HOME)/scripts/config.mk
include $(RVISS_HOME)/scripts/build.mk

# fixdep 在每个对象编译后重写 .d；check 也会直接构建对象。
# 依赖必须挂在对象上，避免首次并行构建早于 fixdep 完成。
$(OBJS): | $(FIXDEP)

# ---------------------------------------------------------------- 测试
# 单元测试：tests/unit/*.c 各自是一个 main()，链接除 main.c 外的所有对象
TEST_SRCS = $(wildcard tests/unit/*.c)
TEST_DIR = $(if $(CONFIG_SHARE),$(BUILD_DIR)/tests-ref,$(BUILD_DIR))
TEST_BINS = $(patsubst tests/unit/%.c,$(TEST_DIR)/%.test,$(TEST_SRCS))
CORE_OBJS = $(filter-out $(OBJ_DIR)/src/main.o,$(OBJS))

# ref 对象可链接进测试可执行文件，但测试本身不能用 -shared 链接。
$(TEST_DIR)/%.test: tests/unit/%.c $(CORE_OBJS) | $(FIXDEP)
	@echo + LD $@
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -o $@ $< $(CORE_OBJS) $(filter-out -shared,$(LDFLAGS)) $(LIBS)
	$(call call_fixdep, $(@:.test=.d), $@)

-include $(TEST_BINS:.test=.d)

check: $(TEST_BINS) $(BINARY)
	@for t in $(TEST_BINS); do echo "=== $$t"; $$t || exit 1; done
	@echo "=== 单元测试全部通过"
ifndef CONFIG_SHARE
	@$(RVISS_HOME)/tests/smoke/run.sh $(BINARY)
	@python3 $(RVISS_HOME)/tests/cli/test_step.py $(BINARY)
else
	@echo "=== ref 配置：共享库已构建；CLI smoke 需在 minimal 配置运行"
endif

# ---------------------------------------------------------------- 运行
IMG  ?=
ARGS ?= -b

run: $(BINARY)
	$(BINARY) $(ARGS) $(IMG)

gdb: $(BINARY)
	gdb -q --args $(BINARY) $(ARGS) $(IMG)

# ---------------------------------------------------------------- 开发辅助
# 生成 clangd 用的编译数据库。
# 参数直接取自本 Makefile 的真实编译配置（CC/CFLAGS/-I），所以永不与构建脱节。
# 收录全部 src/ 与 tests/ 下的 .c —— 包括被当前 Kconfig 排除的文件，
# 这样 clangd 对"当前没编进去"的源文件也能正确索引（否则只能靠 inferred flags）。
ALL_SRCS = $(shell find $(RVISS_HOME)/src $(RVISS_HOME)/tests -name "*.c")

compiledb:
	@python3 $(RVISS_HOME)/scripts/gen_compiledb.py \
	  --dir $(RVISS_HOME) \
	  --cc "$(CC)" \
	  --flags "$(CFLAGS)" \
	  --files "$(ALL_SRCS)" \
	  --out $(RVISS_HOME)/compile_commands.json

# ---------------------------------------------------------------- 清理
clean-tools = $(dir $(shell find $(RVISS_HOME)/tools -name "Makefile"))
$(clean-tools):
	-@$(MAKE) -s -C $@ clean

clean-all: clean distclean
clean-all: $(clean-tools)

.PHONY: run gdb check clean-all compiledb $(clean-tools)
