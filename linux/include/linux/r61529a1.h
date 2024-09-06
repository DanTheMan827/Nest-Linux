/*
 *    Copyright (c) 2014 Nest, Inc.
 *
 *      Author: Andrew LeCain <alecain@nestlabs.com>
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    version 2 as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public
 *    License along with this program. If not, see
 *    <http://www.gnu.org/licenses/>.
 *
 *    Description:
 *      This file is the LCD panel driver for the Tianma TM025ZDZ01
 *      320 x 320 TFT LCD display panel using the Renesas r61529a1
 *      interface driver.
 */

#include <linux/module.h>



#ifndef _INCLUDE_LINUX_R61529A1_H_
#define _INCLUDE_LINUX_R61529A1_H_

struct r61529a1_platform_data {
	struct {
		unsigned long		gpio;
		bool				inverted;
	} reset;
	struct {
		const char			*vcc;
	} regulator;
};


#endif
