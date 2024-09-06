#
#    Copyright (c) 2011 Nest Labs, Inc.
#    All rights reserved.
#
#    This document is the property of Nest Labs. It is considered
#    confidential and proprietary information.
#
#    This document may not be reproduced or transmitted in any form,
#    in whole or in part, without the express written permission of
#    Nest Labs.
#
#    Description:
#      This file is the make file for generating a Linux-compatible
#      boot logo. This follows the general recipe described at:
#
#          http://www.gentoo-wiki.info/HOWTO_Linux_Logo_Hack
#
#      and adds support for handling alpha in the source image.
#

include pre/macros.mak
include host/tools.mak

PNGTOPNM		= pngtopnm
PNGTOPNMFLAGS		= -alpha -mix -background "\#000"

PPMQUANT		= ppmquant
PPMQUANTFLAGS		= -fs 223

PNMTOPLAIN		= pnmtoplainpnm

LogoSourceExtension	= .png
LogoSourceDir		= .
LogoSourceName		= nest-logo-320x320$(LogoSourceExtension)
LogoSourcePath		= $(LogoSourceDir)/$(LogoSourceName)

LogoResultExtension	= .ppm
LogoResultDir		= .
LogoResultName		= logo_diamond_clut224$(LogoResultExtension)
LogoResultPath		= $(LogoResultDir)/$(LogoResultName)

define png-to-logo
	$(Echo) "Converting \"$(<)\" to \"$(@)\"..."
	$(Verbose)$(PNGTOPNM) $(PNGTOPNMFLAGS) "$(<)"	| \
		  $(PPMQUANT) $(PPMQUANTFLAGS)		| \
		  $(PNMTOPLAIN) > "$(@)"
endef

%$(LogoResultExtension): %$(LogoSourceExtension) Makefile
	$(png-to-logo)

all: $(LogoResultPath)

$(LogoResultPath): $(LogoSourcePath) Makefile
	$(png-to-logo)

clean:
	$(Echo) "Cleaning..."
	$(Verbose)$(RM) $(RMFLAGS) $(LogoResultPath)
