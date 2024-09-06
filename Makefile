#
#    Copyright (c) 2010-2012 Nest Labs, Inc.
#    All rights reserved.
#
#    This document is the property of Nest. It is considered
#    confidential and proprietary information.
#
#    This document may not be reproduced or transmitted in any form,
#    in whole or in part, without the express written permission of
#    Nest.
#
#    Description:
#      This file is the makefile for the Linux kernel.
#

.NOTPARALLEL:

include pre.mak

PackageSourceDir		:= $(LinuxSourcesPath)

PackageBuildConfig		:= $(call GenerateBuildPaths,.config)
PackageBuildMakefile		:= $(call GenerateBuildPaths,Makefile)

CleanPaths			+= $(PackageLicenseFile)

#
# Configuration
#
# Kernel default configuration is handled from most-specific to
# least-specific, as follows:
#
#   Out-of-tree:
#
#     1) If configs/$(BuildProduct)/$(BuildConfig)_defconfig exists,
#        it is used.
#     2) If configs/$(BuildProduct)/defconfig exists, it is used.
#     3) If configs/$(BuildConfig)_defconfig exists, it is used.
#     4) If configs/defconfig exists, it is used.
#
#   In-tree:
#
#     5) If none of those exist, $(LinuxDefaultConfig) is used, which
#        refers to an architecture- and/or board-specific default
#        configuration.
#
# We go through this elaborate scheme because, once created, the
# kernel loses all association with it configuration file and the
# source it was created from. As a consequence, if the default
# configuration is edited, the kernel doesn't know about the
# change. This is especially problematic when changing branches or
# receiving updates to the default configuration and the kernel
# doesn't rebuild to reflect those changes.
#
# With this scheme, the user can edit .config as before. However, now
# if the default configuration is changed, out-of-tree only, .config
# is updated and the kernel will rebuild to reflect those changes.
#

PackageConfigDir		:= configs
PackageProjectConfig		:= $(PackageConfigDir)/defconfig
PackageConfigConfig		:= $(PackageConfigDir)/$(BuildConfig)_defconfig
PackageProductConfig		:= $(PackageConfigDir)/$(BuildProduct)/defconfig
PackageProductConfigConfig	:= $(PackageConfigDir)/$(BuildProduct)/$(BuildConfig)_defconfig

PackageMaybeProjectConfig	:= $(wildcard $(PackageProjectConfig))
PackageMaybeConfigConfig	:= $(wildcard $(PackageConfigConfig))
PackageMaybeProductConfig	:= $(wildcard $(PackageProductConfig))
PackageMaybeProductConfigConfig	:= $(wildcard $(PackageProductConfigConfig))

PackageMaybeConfig		:= $(if $(PackageMaybeProductConfigConfig),$(PackageProductConfigConfig),$(if $(PackageMaybeProductConfig),$(PackageProductConfig),$(if $(PackageMaybeConfigConfig),$(PackageConfigConfig),$(if $(PackageMaybeProjectConfig),$(PackageProjectConfig),))))

PackageSourceConfig     	:= $(if $(PackageMaybeConfig),$(PackageMaybeConfig),)

ifeq ($(NL_KERNEL_VERSION), $(subst -,,$(NL_KERNEL_VERSION)))
PackageLocalVersion             := ""
else
PackageLocalVersion             := "-$(BuildVersion)"
endif

#
# Specific products may want additional build targets beyond those
# produced by the linux build's 'all' target. Process and establish
# those.
#

LinuxAllTarget		:= all
LinuxBuildTargets	:= $(filter-out $(LinuxAllTarget),$(LinuxProductTargets)) $(LinuxAllTarget)

#
# For some architectures, the linux build requires u-boot's host
# 'mkimage' tool. Establish some paths and directories that will be
# used, if present, to help the linux build find it.
#

#
# Architecture-specific boot images that may need to be copied from
# the build to the result directory.
#

LinuxBootFolder		= arch/$(LinuxProductArch)/boot
LinuxBootDir		= $(call GenerateBuildPaths,$(LinuxBootFolder))
LinuxBootBuildPaths	= $(addprefix $(LinuxBootDir)/,$(LinuxProductBootFiles))

LinuxKernelFiles	= vmlinux
LinuxKernelFolder	= .
LinuxKernelDir		= $(call GenerateBuildPaths,$(LinuxKernelFolder))
LinuxKernelBuildPaths	= $(addprefix $(call Slashify,$(LinuxKernelDir)),$(LinuxKernelFiles))

LinuxDtbUniqueFiles	= $(sort $(LinuxDtbFiles))
LinuxDtbFolder		= arch/$(LinuxProductArch)/boot/dts
LinuxDtbDir		= $(call GenerateBuildPaths,$(LinuxDtbFolder))
LinuxDtbBuildPaths	= $(addprefix $(call Slashify,$(LinuxDtbDir)),$(LinuxDtbUniqueFiles))

LinuxBuildPaths		= $(LinuxBootBuildPaths) $(LinuxKernelBuildPaths) $(LinuxDtbBuildPaths)
LinuxResultPaths	= $(call GenerateResultPaths,,$(notdir $(LinuxBuildPaths)))

LinuxConfigsDir		= $(PackageSourceDir)/arch/arm/configs
#
# Common make arguments shared by all Linux target commands
#

LinuxCommonMakeArgs	= \
	-C $(PackageSourceDir) \
	O=$(CURDIR)/$(BuildDirectory) \
	ARCH=$(LinuxProductArch) \
	INSTALL="$(INSTALL) $(INSTALLFLAGS)" \
	INSTALL_HDR_PATH=$(ResultDirectory) \
	INSTALL_MOD_PATH=$(ResultDirectory) \
	DEPMOD=$(DEPMOD) \
	LOCALVERSION="$(PackageLocalVersion)"  \
	$(if $(BuildVerbose),V=$(BuildVerbose)) \
	$(NULL)

all: $(PackageDefaultGoal)

# Generate the package license contents.

$(PackageLicenseFile): $(PackageSourceDir)/COPYING
	$(copy-result)

# The Linux kernel build has no way of explicitly setting CC, LD,
# OBJCOPY, et al and instead relies on the value of
# CROSS_COMPILE. Consequently, we have to ensure that 'ToolBinDir' is
# in 'PATH' so that the kernel build infrastructure can find
# $(CROSS_COMPILE)gcc, $(CROSS_COMPILE)ld, et al.
#
# Additionally, per the above, add the path to U-Boot's 'mkimage' if
# the variable is defined.

PATH := $(PATH):$(ToolBinDir)
CROSS_COMPILE := $(CCACHE) $(CROSS_COMPILE)

# Generate the Linux build configuration and make file
#
# We are either copying and out-of-tree configuration or using an
# in-tree default (see above).

# $(PackageBuildConfig): help-defconfig

ifeq ($(PackageSourceConfig),)

DefconfigBasename=$(subst _defconfig,,$(LinuxDefaultConfig))
ConfigFragment=$(LinuxConfigsDir)/$(DefconfigBasename)_$(BuildConfig)_fragment

$(PackageBuildConfig): $(LinuxConfigsDir)/$(LinuxDefaultConfig) $(ConfigFragment) | $(BuildDirectory)
	$(Echo) "The default Linux build configuration is \"$(LinuxDefaultConfig)\"."
	$(Verbose)$(MAKE) $(LinuxCommonMakeArgs) $(LinuxDefaultConfig)
	$(Echo) "Applying config fragment \"$(notdir $(ConfigFragment))\"."
	$(Verbose)$(PackageSourceDir)/scripts/kconfig/merge_config.sh -O $(BuildDirectory) -m $(@) $(ConfigFragment)
	$(Echo) "Setting unspecified options to defaults"
	$(Verbose) yes " " | $(MAKE) $(LinuxCommonMakeArgs) oldconfig
	$(Verbose)scripts/config-check.sh $(@) $(ConfigFragment)
else
$(PackageBuildConfig): $(PackageSourceConfig) | $(PackageSourceDir) $(BuildDirectory)
	$(Echo) "The default Linux build configuration is \"$(PackageSourceConfig)\"."
	$(copy-result)
endif

# Configure the source for building.

.PHONY: configure
configure: $(PackageBuildConfig) | $(BuildDirectory)

# Build the source.

.PHONY: build
build: configure | $(BuildDirectory)
	$(Verbose)$(MAKE) $(LinuxCommonMakeArgs) $(LinuxProductMakeArgs) $(LinuxBuildTargets) $(LinuxDtbUniqueFiles)
	$(Verbose)touch $(call GenerateBuildPaths,$(@))

# Stage the build to a temporary installation area.

.PHONY: stage
stage: stage-boot stage-modules stage-headers

.PHONY: headers
headers: stage-headers

.PHONY: stage-boot
stage-boot: $(LinuxResultPaths)

.PHONY: stage-modules
stage-modules: build | $(ResultDirectory) $(BuildDirectory)
	$(Verbose)flock -w 15 $(ResultDirectory)/modules-install-lock -c '$(MAKE) $(LinuxCommonMakeArgs) modules_install'

stage-headers: configure | $(ResultDirectory) $(BuildDirectory)
	$(Verbose)$(MAKE) $(LinuxCommonMakeArgs) headers_install

linux-%:
	$(Verbose)$(MAKE) $(LinuxCommonMakeArgs) $(*)

$(LinuxBuildPaths): build

$(ResultDirectory)/%: $(LinuxBootDir)/% | $(ResultDirectory)
	$(copy-result)

$(ResultDirectory)/%: $(LinuxKernelDir)/% | $(ResultDirectory)
	$(copy-result)

$(ResultDirectory)/%: $(LinuxDtbDir)/% | $(ResultDirectory)
	$(copy-result)

clean:
	$(Verbose)$(RM) $(RMFLAGS) -r $(BuildDirectory)
	$(Verbose)$(RM) $(RMFLAGS) -r $(ResultDirectory)

.PHONY: help help-defconfig
help-defconfig:
	$(echo-defconfig)

help: help-defconfig

include post.mak
