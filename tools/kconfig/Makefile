# 自定位到工程根目录（本文件在 <root>/tools/kconfig/ 下），
# 这样直接 `make -C tools/kconfig` 也能构建，不依赖外部环境变量。
RVISS_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

NAME = conf
obj := build
SRCS += confdata.c expr.c preprocess.c symbol.c util.c
SRCS += $(obj)/lexer.lex.c $(obj)/parser.tab.c
CFLAGS += -DYYDEBUG
INC_DIR += .

ifeq ($(NAME),conf)
SRCS += conf.c
else ifeq ($(NAME),mconf)
SRCS += mconf.c $(shell find lxdialog/ -name "*.c")
LIBS += -lncurses -ltinfo
else
$(error bad target=$(NAME))
endif

include $(RVISS_HOME)/scripts/build.mk

$(obj)/lexer.lex.o: $(obj)/parser.tab.h
$(obj)/lexer.lex.c: lexer.l $(obj)/parser.tab.h
	@echo + LEX $@
	@flex -o $@ $<

# bison 一次生成两个文件；分组目标避免 -j 下两次同时写同一组输出。
$(obj)/parser.tab.c $(obj)/parser.tab.h &: parser.y
	@echo + YACC $@
	@mkdir -p $(obj)
	@bison -v $< --defines=$(obj)/parser.tab.h -o $(obj)/parser.tab.c

conf:
	@$(MAKE) -s

mconf:
	@$(MAKE) -s NAME=mconf

.PHONY: conf mconf
