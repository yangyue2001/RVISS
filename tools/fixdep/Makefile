# 自定位到工程根目录（本文件在 <root>/tools/fixdep/ 下）
RVISS_HOME := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

NAME = fixdep
SRCS = fixdep.c
include $(RVISS_HOME)/scripts/build.mk
