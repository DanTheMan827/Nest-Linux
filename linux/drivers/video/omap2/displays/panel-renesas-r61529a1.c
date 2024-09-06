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


#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/fs.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/r61529a1.h>
#include <linux/time.h>

#include <plat/display.h>

/*
 * Driver Strings
 */
#define TM025ZDZ01_DRIVER_NAME			"Renesas r61529a1 LCD Driver"
#define TM025ZDZ01_DRIVER_VERSION		"2014-4-23"


#define FRAME_DURATION 		(1000/60)
#define WAKE_RETRY_COUNT	5

/*
 *user commands
 */

#define	NOP					0x00
#define SOFT_RESET			0x01
#define READ_DDB_START		0x04
#define GET_POWER_MODE		0x0A
#define GET_ADDRESS_MODE	0x0B
#define GET_PIXEL_FORMAT	0x0C
#define GET_DISPLAY_MODE	0x0D
#define GET_SIGNAL_MODE		0x0E
#define GET_DIAG_RESULT		0x0F

#define ENTER_SLEEP_MODE	0x10
#define EXIT_SLEEP_MODE		0x11
#define ENTER_PARTIAL_MODE	0x12
#define ENTER_NORMAL_MODE	0x13
#define EXIT_DEEP_SLEEP		0xFF

#define EXIT_INVERT_MODE	0x20
#define ENTER_INVERT_MODE	0x21
#define SET_DISPLAY_OFF		0x28
#define SET_DISPLAY_ON		0x29
#define SET_COLUMN_ADDRESS	0x2A
#define SET_PAGE_ADDRESS	0x2B
#define WRITE_MEMORY_START	0x2C
#define READ_MEMORY_START	0x2E

#define SET_PARTIAL_AREA	0x30
#define SET_TEAR_OFF		0x34
#define SET_TEAR_ON			0x35
#define SET_ADDRESS_MODE	0x36
#define EXIT_IDLE_MODE		0x38
#define ENTER_IDLE_MODE		0x39
#define SET_PIXEL_FORMAT	0x3A
#define WRITE_MEM_CONTINUE	0x3C
#define READ_MEMORY_CONTIUE	0x3E

#define SET_TEAR_SCANLINE	0x44
#define GET_SCANLINE		0x45

/*
 *Manufacturer commands
 */

#define MCAP 				0xB0
#define LOW_POWER_CONTROL 	0xB1
#define FRAME_MEM_ACCESS 	0xB3
#define DISPLAY_MODE		0xB4
#define READ_CHECKSUM		0xB5
#define DSI_CONTROL			0xB6
#define MDDI_CONTROL 		0xB7
#define BACKLIGHT_CONTROL_1	0xB8
#define BACKLIGHT_CONTROL_2	0xB9
#define BACKLIGHT_CONTROL_3	0xBA
#define DEVICE_CODE_READ 	0xBF

#define DEVICE_CODE_1		0x01
#define DEVICE_CODE_2		0x22
#define DEVICE_CODE_3		0x15
#define DEVICE_CODE_4		0x29

#define PANEL_DRIVE			0xC0
#define V_TIMING			0xC1
#define TEST_MODE			0xC3
#define DRIVE_CONTROL		0xC4
#define DPI_POLARITY		0xC6
#define TEST_MODE_2			0xC7
#define GAMMA_SETTING_A		0xC8
#define GAMMA_SETTING_B		0xC9
#define GAMMA_SETTING_C		0xCA

#define CHARGE_PUMP			0xD0
#define VCOM				0xD1
#define TEST_MODE_4			0xD6
#define TEST_MODE_5			0xD7
#define TEST_MODE_6			0xD8
#define TEST_MODE_7			0xD9
#define TEST_MODE_8			0xDA

static struct regulator *reg;
const char *supply;
/*lines to delay after vsync*/
static u16 line=0;
static struct timespec last_frame;
static int32_t fps;

static ssize_t  r61529a1_test_pixel_read(struct device *unused, struct device_attribute *attr, char *buf);
static ssize_t  r61529a1_display_id_read(struct device *unused, struct device_attribute *attr, char *buf);
static ssize_t  r61529a1_get_fps(struct device *unused, struct device_attribute *attr, char *buf);

static DEVICE_ATTR(test_pixel, S_IRUGO, r61529a1_test_pixel_read, NULL);
static DEVICE_ATTR(display_id, S_IRUGO, r61529a1_display_id_read, NULL);
static DEVICE_ATTR(fps, S_IRUGO, r61529a1_get_fps, NULL);

static int renesas_r61529a1_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h);

static struct attribute *r61529a1_attributes[] = {
	&dev_attr_test_pixel.attr,
	&dev_attr_display_id.attr,
	&dev_attr_fps.attr,
	NULL
};

static const struct attribute_group r61529a1_attr_group = {
	.attrs = r61529a1_attributes,
};

void WriteCommand(uint16_t cmd){
	omapdss_rfbi_write_command(&cmd, 2);
}
void WriteData(uint16_t dat){
	omapdss_rfbi_write_data(&dat, 2);
}

int init_display(struct omap_dss_device *dssdev)  //for  void R61529A_initial_code(void)
{
	char temp[5];
	struct r61529a1_platform_data *pdata = (struct r61529a1_platform_data *)dssdev->data;

	gpio_set_value(pdata->reset.gpio, pdata->reset.inverted);
	mdelay(10);
	gpio_set_value(pdata->reset.gpio, !pdata->reset.inverted);
	mdelay(10);
	//************************Start initial sequence****************************

	WriteCommand(MCAP); //send enable manufacture commands
	WriteData(0x0004);
	//dev_printk(KERN_INFO, &dssdev->dev, "wrote mfg cmd\n");


	//confirm display is awake
	omapdss_rfbi_read_data(DEVICE_CODE_READ, temp, 5);
	dev_printk(KERN_INFO, &dssdev->dev, "display ID: %02x %02x %02x %2x\n",
				temp[1], temp[2], temp[3], temp[4]);

	if ((temp[1] != DEVICE_CODE_1) || (temp[2] !=DEVICE_CODE_2 )
			|| (temp[3] != DEVICE_CODE_3) || (temp[4] != DEVICE_CODE_4))
	{
		return -1;
	}

	WriteCommand(FRAME_MEM_ACCESS); //frame meory
	WriteData(0x0002); //WEMODE=1 continuous write
	WriteData(0x0000); //TE used, Every frame
	WriteData(0x0000); //unused
	WriteData(0x0000);////DFM disabled (2 pixels / 3 cycles)

	WriteCommand(DISPLAY_MODE); // display mode
	WriteData(0x0000); // internal oscillation clock

	WriteCommand(SET_TEAR_ON); //enable TE
	WriteData(0x0001); //hsync | vsync

	WriteCommand(SET_TEAR_SCANLINE); //set tearing line.
	WriteData(0x0000);
	WriteData(0x0000); //assert TE at line 0

	WriteCommand(SET_ADDRESS_MODE); //address mode
	WriteData(0 << 7 | 0 << 6 | 0 << 5); //no mirroring

	WriteCommand(PANEL_DRIVE); //panel driving setting
	WriteData(0x0013); // REV | BGR| SS
	WriteData(0x00DF); //NL[7..1] <<1 | 1 NL = 0xEF means 480 lines. (wat?)
	WriteData(0x0040); //NL[8] << 6 | SCN[5..0] SCN = 0
	WriteData(0x0010); // 0x10 (NOP)
	WriteData(0x0000); //
	WriteData(0x0001); //isc = 0x01 every 3 frames
	WriteData(0x0000); //BLS=0 retrace voltage V255
	WriteData(0x0055); //PCDIV = 0x55 5 clock div on high and low

	WriteCommand(V_TIMING);//Frequency
	WriteData(0x0007); //DIV = 0x7 fosc/12 (default)
	WriteData(0x0029); //RTN = 0x29 41clk per line
	WriteData(dssdev->panel.timings.vbp); //BP = 0x8 8 lines Back porch
	WriteData(dssdev->panel.timings.vfp); //FP = 0x8 8 lines front porch
	WriteData(0x0000); //LINEIN=0 PSNSET = 0
	//mdelay(50);

	WriteCommand(DRIVE_CONTROL);//panel driver
	WriteData(0x0057); //NOWB = x5 NOW=x7 falling 4 clocks NOW = 8 clocks
	WriteData(0x0000); //unused
	WriteData(0x0005); //SEQGND = x5 gnd precharge = 5 clkcs
	WriteData(0x0003); //SEQVCIL = x3 vcil precharge = 3 clks
	//mdelay(50);

	WriteCommand(DPI_POLARITY);//DPI signal polarity
	WriteData(0x0004);

	/*
	 *These values are correlated to the tianma panel we are driving.
	 */

	WriteCommand(GAMMA_SETTING_A);//gamma
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);

	WriteCommand(GAMMA_SETTING_B);//gamma
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);

	WriteCommand(GAMMA_SETTING_C);//gamma
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);
	WriteData(0x0005);
	WriteData(0x000d);
	WriteData(0x001a);
	WriteData(0x0028);
	WriteData(0x003b);
	WriteData(0x0054);//53
	WriteData(0x003A);//39
	WriteData(0x002F);//2E
	WriteData(0x0029);//28
	WriteData(0x0024);//23
	WriteData(0x0020);
	WriteData(0x000A);

	WriteCommand(CHARGE_PUMP);//power setting
	WriteData(0x0099);
	WriteData(0x000A);//06
	WriteData(0x0008);
	WriteData(0x0020);
	WriteData(0x000a);//29
	WriteData(0x0004);
	WriteData(0x0001);
	WriteData(0x0000);
	WriteData(0x0008);
	WriteData(0x0001);
	WriteData(0x0000);
	WriteData(0x0006);
	WriteData(0x0001);
	WriteData(0x0000);
	WriteData(0x0000);
	WriteData(0x0020);
	//mdelay(50);

	WriteCommand(VCOM);//power setting
	WriteData(0x0002);//00
	WriteData(0x001C);//20
	WriteData(0x001C);//20
	WriteData(0x0030);//15VCOMDC=-1.0
	//mdelay(50);

	WriteCommand(SET_PIXEL_FORMAT);//pixel format seting
	WriteData(0x0077); //24 bpp

	WriteCommand(SET_COLUMN_ADDRESS);//resolution setting (col)
	WriteData(0x0000);
	WriteData(0x0000); //Start Column = 0
	WriteData(((dssdev->panel.timings.y_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.y_res - 1) & 0xFF);  //end column = y_res-1

	WriteCommand(SET_PAGE_ADDRESS); //(row)
	WriteData(0x0000);
	WriteData(0); //Start row = 0
	WriteData(((dssdev->panel.timings.x_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.x_res - 1) & 0xFF); //end row = x_res-1

	WriteCommand(EXIT_SLEEP_MODE); //Exit Sleep
	mdelay(6*FRAME_DURATION);

	WriteCommand(SET_DISPLAY_ON); //Display On
	mdelay(FRAME_DURATION);

	WriteCommand(WRITE_MEMORY_START);

	return 0;
}

static ssize_t  r61529a1_test_pixel_read(struct device *dss, struct device_attribute *attr, char *buf)
{
	char temp[4];
	struct omap_dss_device *dssdev = (struct omap_dss_device*) dss;

	/*set start address to where the FB starts writing
	actually the bottom right of the image because it is rotated in sw */

	//set_page_address
	WriteCommand(SET_PAGE_ADDRESS);
	WriteData(((dssdev->panel.timings.x_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.x_res - 1) & 0xFF);  //start row = x_res-1
	WriteData(((dssdev->panel.timings.x_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.x_res - 1) & 0xFF);  //end row = x_res-1

	//set_column_address
	WriteCommand(SET_COLUMN_ADDRESS);
	WriteData(((dssdev->panel.timings.y_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.y_res - 1) & 0xFF);  //start column = y_res-1
	WriteData(((dssdev->panel.timings.y_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.y_res - 1) & 0xFF);  //end column = y_res-1

	//send pixel read -- read back 3 bytes+ one dummy
	omapdss_rfbi_read_data(READ_MEMORY_START, temp, 4);

	//fix display addressing
	WriteCommand(SET_COLUMN_ADDRESS);//resolution setting (col)
	WriteData(0);
	WriteData(0); //SC = 0
	WriteData(((dssdev->panel.timings.y_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.y_res - 1) & 0xFF);  //end column = y_res-1

	WriteCommand(SET_PAGE_ADDRESS); //(row)
	WriteData(0);
	WriteData(0); //SR
	WriteData(((dssdev->panel.timings.x_res - 1) & 0x100 ) >> 8);
	WriteData((dssdev->panel.timings.x_res - 1) & 0xFF); //end row = x_res-1

	return sprintf(buf, "%06x\n",(*((u32*)temp) & 0xFFFFFF00) >> 8);
}
static ssize_t  r61529a1_display_id_read(struct device *unused, struct device_attribute *attr, char *buf)
{
	char temp[5];
	omapdss_rfbi_read_data(DEVICE_CODE_READ, temp, 5);
	return sprintf(buf, "%08x\n",*((u32*)(temp+1)));
}

static ssize_t  r61529a1_get_fps(struct device *unused, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", fps);
}
static int renesas_r61529a1_power_on(struct omap_dss_device *dssdev)
{
	int r = 0;

	dev_printk(KERN_INFO, &dssdev->dev, "Power on\n");
	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	if (reg){
		r = regulator_enable(reg);
	}
	if (r)
		goto err0;

	if (dssdev->platform_enable) {
		r = dssdev->platform_enable(dssdev);
		if (r)
			goto err0;
	}

	r = omapdss_rfbi_display_enable(dssdev);

	if (r)
		goto err1;

	/* Setup external VSYNC triggering */
	omap_rfbi_setup_te( OMAP_DSS_RFBI_TE_MODE_1,
				20000000,     /* HSYNC pulse 30uS */
				300000000,  /* VSYNC pulse 500uS */
				0, 0,       /* TE active high*/
				0);


	getrawmonotonic(&last_frame);
	fps = -1;

	r = init_display(dssdev);

	return r;
err1:
	omapdss_rfbi_display_disable(dssdev);
err0:
	return r;
}

static void renesas_r61529a1_power_off(struct omap_dss_device *dssdev)
{

	dev_printk(KERN_INFO, &dssdev->dev, "power off \n");

	omapdss_rfbi_display_disable(dssdev);

	WriteCommand(SET_DISPLAY_OFF); //Display off
	mdelay(50);

	WriteCommand(ENTER_SLEEP_MODE); //enter Sleep
	mdelay(100);

	WriteCommand(MCAP); //send enable manufacture commands
	WriteData(0x0000);

	WriteCommand(LOW_POWER_CONTROL); //enter deep standby
	WriteData(0x0001);
	mdelay(100);


	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return;

	if (reg){
		regulator_disable(reg);
	}

	if (dssdev->platform_disable)
		dssdev->platform_disable(dssdev);
}

static int renesas_r61529a1_probe(struct omap_dss_device *dssdev)
{
	int status;

	struct r61529a1_platform_data *pdata = (struct r61529a1_platform_data *)dssdev->data;
	supply = pdata->regulator.vcc;
	if (supply)
	{
		reg = regulator_get(&dssdev->dev, supply);
	}

	dssdev->panel.config = (OMAP_DSS_LCD_TFT |
							OMAP_DSS_LCD_IPC |
							OMAP_DSS_LCD_IEO |
							OMAP_DSS_LCD_IVS |
							OMAP_DSS_LCD_IHS);

    status = sysfs_create_group(&dssdev->dev.kobj, &r61529a1_attr_group);

	if (status) goto err0;

	status = gpio_request(pdata->reset.gpio, "r61529a1 reset");

	if (status) goto err1;

	status = gpio_direction_output(pdata->reset.gpio, !pdata->reset.inverted);

	if (status) goto err2;

	return 0;

err2:
	gpio_free(pdata->reset.gpio);

err1:
	sysfs_remove_group(&dssdev->dev.kobj, &r61529a1_attr_group);

err0:
	return status;
}

static void renesas_r61529a1_remove(struct omap_dss_device *dssdev)
{

	struct r61529a1_platform_data *pdata = (struct r61529a1_platform_data *)dssdev->data;
	if (reg){
		regulator_put(reg);
	}

	sysfs_remove_group(&dssdev->dev.kobj, &r61529a1_attr_group);

	gpio_free(pdata->reset.gpio);

}

static int renesas_r61529a1_enable(struct omap_dss_device *dssdev)
{
	int r = 0;

	//dev_printk(KERN_INFO, &dssdev->dev, "r61529a1 enable\n");
	r = renesas_r61529a1_power_on(dssdev);
	if (r)
		return r;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	return 0;
}

static void renesas_r61529a1_disable(struct omap_dss_device *dssdev)
{
	dev_printk(KERN_INFO, &dssdev->dev, "r61529a1 disable\n");
	renesas_r61529a1_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
}
static enum omap_dss_update_mode generic_get_update_mode(
		struct omap_dss_device *dssdev)
{
	return OMAP_DSS_UPDATE_AUTO;
}

static void update_done(void* data){

	u16 w,h;
	struct omap_dss_device *dssdev = (struct omap_dss_device *) data;

	//queue up another frame
	if(dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
	{
		dssdev->driver->get_resolution(dssdev, &w, &h);
		renesas_r61529a1_update(dssdev, 0 , 0 , w, h);
	}
	//printk("r61529a1 update_done\n");

}

static int renesas_r61529a1_enable_te(struct omap_dss_device * dssdev, bool en)
{

	/* Setup external VSYNC triggering */
	omap_rfbi_setup_te( OMAP_DSS_RFBI_TE_MODE_1,
				20000000,     /* HSYNC pulse 30uS */
				300000000,  /* VSYNC pulse 500uS */
				0, 0,       /* TE active high*/
				0);
	omap_rfbi_enable_te(en, line); //480/2

	return 0;
}

static int renesas_r61529a1_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h)
{

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return -1;

#ifdef DISPLAY_PERFORMANCE
	{
		struct timespec t;
		uint32_t deltaNs;

		getrawmonotonic(&t);
		if (t.tv_nsec < last_frame.tv_nsec)
		{
			deltaNs = 1000000000 + t.tv_nsec - last_frame.tv_nsec;
		}
		else //no overflow
		{
			deltaNs = t.tv_nsec - last_frame.tv_nsec;
		}

		fps = 1000 / (deltaNs / 1000000);

		last_frame.tv_sec = t.tv_sec;
		last_frame.tv_nsec = t.tv_nsec;

	}
	
#endif

	//dev_printk(KERN_INFO, &dssdev->dev, "update\n");
	WriteCommand(WRITE_MEMORY_START);
	renesas_r61529a1_enable_te(dssdev, 1);
	omap_rfbi_update(dssdev, x, y, w, h, update_done, dssdev);

	return 0;
}

static int renesas_r61529a1_suspend(struct omap_dss_device *dssdev)
{
	dev_printk(KERN_INFO, &dssdev->dev, "r61529a1 suspend\n");
	renesas_r61529a1_power_off(dssdev);
	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	return 0;
}

static int renesas_r61529a1_resume(struct omap_dss_device *dssdev)
{
	int i,r = -1;
	u16 w, h;

	dev_printk(KERN_INFO, &dssdev->dev, "r61529a1 resume\n");

	for(i = 0; i < WAKE_RETRY_COUNT && r != 0; i++)
	{
		r = renesas_r61529a1_power_on(dssdev);
	}

	if (--i > 0)
	{
		dev_printk(KERN_WARNING, &dssdev->dev,"resume_retries=%d\n",i);
	}

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	//update the panel
	dssdev->driver->get_resolution(dssdev, &w, &h);
	renesas_r61529a1_update(dssdev, 0 , 0 , w, h);

	return 0;
}

static struct omap_dss_driver generic_driver = {
	.probe		= renesas_r61529a1_probe,
	.remove		= renesas_r61529a1_remove,

	.enable		= renesas_r61529a1_enable,
	.disable	= renesas_r61529a1_disable,
	.suspend	= renesas_r61529a1_suspend,
	.resume		= renesas_r61529a1_resume,

	.enable_te 	= renesas_r61529a1_enable_te,

    .get_update_mode = generic_get_update_mode,
	.update         = renesas_r61529a1_update,

	.driver         = {
		.name   = "renesas_r61529a1",
		.owner  = THIS_MODULE,
	},
};

static int __init renesas_r61529a1_drv_init(void)
{
	return omap_dss_register_driver(&generic_driver);
}

static void __exit renesas_r61529a1_drv_exit(void)
{
	omap_dss_unregister_driver(&generic_driver);
}

module_init(renesas_r61529a1_drv_init);
module_exit(renesas_r61529a1_drv_exit);

MODULE_AUTHOR("Nest, Inc.");
MODULE_DESCRIPTION(R61529A1_DRIVER_NAME);
MODULE_LICENSE("GPLv2");
