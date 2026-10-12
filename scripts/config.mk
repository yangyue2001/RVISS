#***************************************************************************************
# Kconfig 前端与配置目标（Linux/NEMU 风格）
#
#   make menuconfig        交互式配置（mconf）
#   make xxx_defconfig     套用 configs/xxx_defconfig（conf --defconfig）
#   make savedefconfig     把当前 .config 精简后存为 configs/defconfig
#   make distclean         连配置一起清掉
#
# 产物：
#   .config                     完整配置
#   include/config/auto.conf    Makefile 读的配置（CONFIG_X=y）
#   include/generated/autoconf.h C 代码读的配置（#define CONFIG_X 1）
#   include/config/<symbol>.h   空标记文件，供 fixdep 做增量依赖
#***************************************************************************************

Q := @

KCONFIG_PATH := $(RVISS_HOME)/tools/kconfig
FIXDEP_PATH  := $(RVISS_HOME)/tools/fixdep
Kconfig      := $(RVISS_HOME)/Kconfig

CONF   := $(KCONFIG_PATH)/build/conf
MCONF  := $(KCONFIG_PATH)/build/mconf
FIXDEP := $(FIXDEP_PATH)/build/fixdep

rm-distclean += .config .config.old include/config include/generated

$(CONF):
	$(Q)$(MAKE) -s -C $(KCONFIG_PATH) BUILD_DIR=$(KCONFIG_PATH)/build conf

$(MCONF):
	$(Q)$(MAKE) -s -C $(KCONFIG_PATH) BUILD_DIR=$(KCONFIG_PATH)/build mconf

$(FIXDEP):
	$(Q)$(MAKE) -s -C $(FIXDEP_PATH) BUILD_DIR=$(FIXDEP_PATH)/build

menuconfig: $(MCONF) $(CONF) $(FIXDEP)
	$(Q)$(MCONF) $(Kconfig)
	$(Q)$(CONF) -s --syncconfig $(Kconfig)

savedefconfig: $(CONF)
	$(Q)$< -s --$@=configs/defconfig $(Kconfig)

# xxx_defconfig → 读 configs/xxx_defconfig，再同步生成 auto.conf/autoconf.h
%defconfig: $(CONF) $(FIXDEP)
	$(Q)$< -s --defconfig=configs/$@ $(Kconfig)
	$(Q)$< -s --syncconfig $(Kconfig)

# 每个对象编译完后调用：把 .d 里出现的 include/config/*.h 转成对配置项的依赖
define call_fixdep
	@$(FIXDEP) $(1) $(2) unused > $(1).tmp
	@mv $(1).tmp $(1)
endef

distclean: clean
	-@rm -rf $(rm-distclean)

.PHONY: menuconfig savedefconfig defconfig distclean
