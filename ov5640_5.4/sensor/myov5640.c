/* Target: NXP linux-imx lf-5.4.y, i.MX6ULL parallel CSI. */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

//电压值,单位微伏(uV)
#define OV5640_VOLTAGE_ANALOG               2800000
#define OV5640_VOLTAGE_DIGITAL_CORE         1500000
#define OV5640_VOLTAGE_DIGITAL_IO           1800000

#define MIN_FPS 15
#define MAX_FPS 30
#define DEFAULT_FPS 30

#define OV5640_XCLK_MIN 6000000
#define OV5640_XCLK_MAX 24000000

#define OV5640_CHIP_ID_HIGH_BYTE        0x300A
#define OV5640_CHIP_ID_LOW_BYTE         0x300B


enum ov5640_mode {
	ov5640_mode_MIN = 0,
	ov5640_mode_VGA_640_480 = 0,
	ov5640_mode_QVGA_320_240 = 1,
	ov5640_mode_NTSC_720_480 = 2,
	ov5640_mode_PAL_720_576 = 3,
	ov5640_mode_720P_1280_720 = 4,
	ov5640_mode_1080P_1920_1080 = 5,
	ov5640_mode_QSXGA_2592_1944 = 6,
	ov5640_mode_QCIF_176_144 = 7,
	ov5640_mode_XGA_1024_768 = 8,
	ov5640_mode_MAX = 8
};

enum ov5640_frame_rate {
	ov5640_15_fps,
	ov5640_30_fps
};

static int ov5640_framerates[] = {
	[ov5640_15_fps] = 15,
	[ov5640_30_fps] = 30,
};

struct ov5640 {
    struct v4l2_subdev subdev;
    struct i2c_client* client;
    struct v4l2_pix_format pix;
    const struct ov5640_datafmt	*fmt;
	struct v4l2_captureparm streamcap;
	bool on;

    int brightness;
	int hue;
	int contrast;
	int saturation;
	int red;
	int green;
	int blue;
	int ae_mode;

	u32 mclk; //只是数值
	u8 mclk_source;
	struct clk *sensor_clk;//时钟对象
	int csi;

	void (*io_init)(struct ov5640*); //一个函数指针

    //gpio
    int pwn_gpio;
    int rst_gpio;

    //regulator电源
    struct regulator* io_regulator; //数字
    struct regulator* core_regulator; //数字核心
    struct regulator* analog_regulator; //模拟

};

struct reg_value {
    u16 u16RegAddr;//寄存器地址
	u8 u8Val; //值
	u8 u8Mask; //掩码
	u32 u32Delay_ms; //延时
};


static struct reg_value ov5640_global_init_setting[] = {
	{0x3008, 0x42, 0, 0},
	{0x3103, 0x03, 0, 0}, {0x3017, 0xff, 0, 0}, {0x3018, 0xff, 0, 0},
	{0x3034, 0x1a, 0, 0}, {0x3037, 0x13, 0, 0}, {0x3108, 0x01, 0, 0},
	{0x3630, 0x36, 0, 0}, {0x3631, 0x0e, 0, 0}, {0x3632, 0xe2, 0, 0},
	{0x3633, 0x12, 0, 0}, {0x3621, 0xe0, 0, 0}, {0x3704, 0xa0, 0, 0},
	{0x3703, 0x5a, 0, 0}, {0x3715, 0x78, 0, 0}, {0x3717, 0x01, 0, 0},
	{0x370b, 0x60, 0, 0}, {0x3705, 0x1a, 0, 0}, {0x3905, 0x02, 0, 0},
	{0x3906, 0x10, 0, 0}, {0x3901, 0x0a, 0, 0}, {0x3731, 0x12, 0, 0},
	{0x3600, 0x08, 0, 0}, {0x3601, 0x33, 0, 0}, {0x302d, 0x60, 0, 0},
	{0x3620, 0x52, 0, 0}, {0x371b, 0x20, 0, 0}, {0x471c, 0x50, 0, 0},
	{0x3a13, 0x43, 0, 0}, {0x3a18, 0x00, 0, 0}, {0x3a19, 0x7c, 0, 0},
	{0x3635, 0x13, 0, 0}, {0x3636, 0x03, 0, 0}, {0x3634, 0x40, 0, 0},
	{0x3622, 0x01, 0, 0}, {0x3c01, 0x34, 0, 0}, {0x3c04, 0x28, 0, 0},
	{0x3c05, 0x98, 0, 0}, {0x3c06, 0x00, 0, 0}, {0x3c07, 0x07, 0, 0},
	{0x3c08, 0x00, 0, 0}, {0x3c09, 0x1c, 0, 0}, {0x3c0a, 0x9c, 0, 0},
	{0x3c0b, 0x40, 0, 0}, {0x3810, 0x00, 0, 0}, {0x3811, 0x10, 0, 0},
	{0x3812, 0x00, 0, 0}, {0x3708, 0x64, 0, 0}, {0x4001, 0x02, 0, 0},
	{0x4005, 0x1a, 0, 0}, {0x3000, 0x00, 0, 0}, {0x3004, 0xff, 0, 0},
	{0x300e, 0x58, 0, 0}, {0x302e, 0x00, 0, 0}, {0x4300, 0x30, 0, 0},
	{0x501f, 0x00, 0, 0}, {0x440e, 0x00, 0, 0}, {0x5000, 0xa7, 0, 0},
	{0x3008, 0x02, 0, 0},
};






//读取寄存器的值,值通过val返回
static int ov5640_read_reg(u16 reg, u8* val, struct i2c_client* client)
{
    //首先把16位寄存器拆成两位
    u8 regbuf[2] = {0};
    regbuf[0] = (u8)(reg >> 8);
    regbuf[1] = (u8)reg;

    u8 readbuf = 0;

    if (i2c_master_send(client, regbuf, 2) != 2) //发送要访问的寄存器地址
    {
        pr_err("%s:write reg error:reg=%x\n",
				__func__, reg);
		return -1;
    }

    if (i2c_master_recv(client, &readbuf, 1) != 1)
    {
        pr_err("%s:read reg error:reg=%x,val=%x\n",
				__func__, reg, readbuf);
		return -1;
    }
    *val = readbuf;
    return 0;
    
}

static int ov5640_write_reg(u16 reg, u8 val, struct i2c_client* client)
{
    int ret = 0;
    u8 buf[3] = {0};
    buf[0] = (u8)(reg>>8);
    buf[1] = reg & 0xff;
    buf[2] = val;

    if (i2c_master_send(client, buf, 3) < 0)
    {
		pr_err("%s:write reg error:reg=%x,val=%x\n",
			__func__, reg, val);
		return -1;
	}


    return ret;
}


//写入寄存器的值
static int ov5640_download_firmware(struct reg_value* pModeSetting, s32 ArySize, struct i2c_client* client)
{
    u32 Delay_ms = 0;
    u16 RegAddr = 0;
    u8 Mask = 0;
    u8 Val = 0;
    u8 RegVal = 0;
    int i = 0;
    int ret = 0;

    for(i = 0; i < ArySize; i++)
    {
        Delay_ms = pModeSetting[i].u32Delay_ms;
        RegAddr = pModeSetting[i].u16RegAddr;
        Mask = pModeSetting[i].u8Mask;
        Val = pModeSetting[i].u8Val;

        if(Mask) //掩码处理
        {
            ret = ov5640_read_reg(RegAddr, &RegVal, client);
            if (ret < 0)
				goto err;
            
            RegVal &= ~(u8)Mask;//先把需要修改的位清0
            Val &= Mask; //将不需要修改的位清0；
            Val |= RegVal;
        }

        ret = ov5640_write_reg(RegAddr, Val, client);
        if (ret < 0)
			goto err;

        if (Delay_ms)
			msleep(Delay_ms);
    }
err:
    return ret;
}

static int ov5640_driver_capability(int strength, struct i2c_client* client)
{
    u8 temp = 0;
    if (strength > 4 || strength < 1) {
		pr_err("The valid driver capability of ov5640 is 1x~4x\n");
		return -EINVAL;
	}

    ov5640_read_reg(0x302c, &temp, client);
    temp &= ~0xc0;	/* clear [7:6] */
	temp |= ((strength - 1) << 6);	/* set [7:6] */

	ov5640_write_reg(0x302c, temp, client);

	return 0;
}

static int ov5640_get_sysclk(struct ov5640* ov5640_data)
{
    int xvclk = ov5640_data->mclk / 10000;
    int sysclk;
    int temp1, temp2;
    int Multiplier, PreDiv, VCO, SysDiv, Pll_rdiv, Bit_div2x, sclk_rdiv;
    int sclk_rdiv_map[] = {1, 2, 4, 8};
    u8 regval = 0;



}


//设置防频闪的参数
static void ov5640_set_bandingfilter(void)
{
    int prev_VTS;
    int band_step60, max_band60, band_step50, max_band50;//曝光步长与最大值
}


static int ov5640_init_mode(struct ov5640* ov5640_data)
{
    struct reg_value* pModeSetting = NULL;
    int ArySize = 0;
    int ret = 0;

    struct i2c_client* client = ov5640_data->client;
    //首先做软件复位
    ov5640_write_reg(0x3103, 0x11, client);
    ov5640_write_reg(0x3008, 0x82, client);

	/* delay at least 5ms */
	msleep(10);

    pModeSetting = ov5640_global_init_setting;
    ArySize = ARRAY_SIZE(ov5640_global_init_setting); //只能对真正的数组用不能对指针用
    ret = ov5640_download_firmware(pModeSetting, ArySize, client);
    if (ret < 0)
		goto err;
    
    ov5640_driver_capability(2, client);


err:
    return ret;
}




static int init_device(struct ov5640* ov5640_data)
{
    u32 target_xclk;
    u32 target_fps;
    int ret;
	enum ov5640_frame_rate frame_rate;

    ov5640_data->on = true;

    target_xclk = ov5640_data->mclk;

    target_fps = ov5640_data->streamcap.timeperframe.denominator / ov5640_data->streamcap.timeperframe.numerator;

    if (target_fps == 15)
		frame_rate = ov5640_15_fps;
	else if (target_fps == 30)
		frame_rate = ov5640_30_fps;
	else
		return -EINVAL; /* Only support 15fps or 30fps now. */

    ret = ov5640_init_mode(ov5640_data);

    return ret;

}


//复位函数
static inline void ov5640_reset(struct ov5640* ov5640_data)
{
    /* camera reset */
	gpio_set_value_cansleep(ov5640_data->rst_gpio, 1);

	/* camera power down */
	gpio_set_value_cansleep(ov5640_data->pwn_gpio, 1);
	msleep(5);
	gpio_set_value_cansleep(ov5640_data->pwn_gpio, 0);
	msleep(5);
	gpio_set_value_cansleep(ov5640_data->rst_gpio, 0);
	msleep(1);
	gpio_set_value_cansleep(ov5640_data->rst_gpio, 1);
	msleep(5);
	gpio_set_value_cansleep(ov5640_data->pwn_gpio, 1);
}

//电源regulator上电函数,给三路电源上电
static int ov5640_regulator_enable(struct device* dev)
{
    int ret = 0;
    struct i2c_client* client = container_of(dev, struct i2c_client, dev);//只能从“结构体里面真正内嵌的成员”反推出外层结构体，不能从“一个指针成员所指向的外部对象”反推。
    struct v4l2_subdev* sd = i2c_get_clientdata(client);
    struct ov5640* ov5640_data = container_of(sd, struct ov5640, subdev);


    ov5640_data->io_regulator = devm_regulator_get(dev, "DOVDD");
    if (!IS_ERR(ov5640_data->io_regulator)) {
		regulator_set_voltage(ov5640_data->io_regulator, OV5640_VOLTAGE_DIGITAL_IO,OV5640_VOLTAGE_DIGITAL_IO);
		ret = regulator_enable(ov5640_data->io_regulator);
		if (ret) {
			dev_err(dev, "set io voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set io voltage ok\n");
		}
	} else {
		ov5640_data->io_regulator = NULL;
		dev_warn(dev, "cannot get io voltage\n");
	}

    ov5640_data->core_regulator = devm_regulator_get(dev, "DVDD");
	if (!IS_ERR(ov5640_data->core_regulator)) {
		regulator_set_voltage(ov5640_data->core_regulator,
				      OV5640_VOLTAGE_DIGITAL_CORE,
				      OV5640_VOLTAGE_DIGITAL_CORE);
		ret = regulator_enable(ov5640_data->core_regulator);
		if (ret) {
			dev_err(dev, "set core voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set core voltage ok\n");
		}
	} else {
		ov5640_data->core_regulator = NULL;
		dev_warn(dev, "cannot get core voltage\n");
	}

    ov5640_data->analog_regulator = devm_regulator_get(dev, "AVDD");
	if (!IS_ERR(ov5640_data->analog_regulator)) {
		regulator_set_voltage(ov5640_data->analog_regulator,
				      OV5640_VOLTAGE_ANALOG,
				      OV5640_VOLTAGE_ANALOG);
		ret = regulator_enable(ov5640_data->analog_regulator);
		if (ret) {
			dev_err(dev, "set analog voltage failed\n");
			return ret;
		} else {
			dev_dbg(dev, "set analog voltage ok\n");
		}
	} else {
		ov5640_data->analog_regulator = NULL;
		dev_warn(dev, "cannot get analog voltage\n");
	}

	return ret;
}


//关闭电源
static void ov5640_regulator_disable(struct ov5640* ov5640_data)
{
	if (ov5640_data->analog_regulator)
		regulator_disable(ov5640_data->analog_regulator);

	if (ov5640_data->core_regulator)
		regulator_disable(ov5640_data->core_regulator);

	if (ov5640_data->io_regulator)
		regulator_disable(ov5640_data->io_regulator);
}




static inline void ov5640_power_down(int enable, struct ov5640* ov5640_data)
{
    gpio_set_value_cansleep(ov5640_data->pwn_gpio, enable);

    msleep(2);
}




static int ov5640_set_clk_rate(struct ov5640* ov5640_data)
{
    u32 tgt_xclk;
    int ret;

    tgt_xclk = ov5640_data->mclk;
    tgt_xclk = min(tgt_xclk, (u32)OV5640_XCLK_MAX);
	tgt_xclk = max(tgt_xclk, (u32)OV5640_XCLK_MIN);
	ov5640_data->mclk = tgt_xclk;
    pr_debug("   Setting mclk to %d MHz\n", tgt_xclk / 1000000);
    ret = clk_set_rate(ov5640_data->sensor_clk, ov5640_data->mclk); //设置频率
	if (ret < 0)
		pr_debug("set rate filed, rate=%d\n", ov5640_data->mclk);
	return ret;

}


static struct v4l2_subdev_ops ov5640_subdev_ops= {

};




static int ov5640_probe(struct i2c_client *client)
{
    
    struct pinctrl* pinctrl;
    struct device* dev = &client->dev;
    int ret;
    u32 buf;
    u8 chip_id_high;
    u8 chip_id_low;

    struct ov5640* ov5640_data = devm_kzalloc(dev, sizeof(struct ov5640), GFP_KERNEL);
    if (!ov5640_data)
    {
        return -ENOMEM;
    }

    v4l2_i2c_subdev_init(&ov5640_data->subdev, client, &ov5640_subdev_ops);

    pinctrl = devm_pinctrl_get_select_default(dev);
    if (IS_ERR(pinctrl))
    {
        dev_err(dev, "setup pinctrl failed\n");
		return PTR_ERR(pinctrl);
    }

    //设置pwn-gpio
    ov5640_data->pwn_gpio = of_get_named_gpio(dev->of_node, "pwn_gpio", 0);//index表示是这个属性(pwn_gpio)里面的第几个gpio,这个函数的作用是得到GPIO编号
    if (!gpio_is_valid(ov5640_data->pwn_gpio)) {
		dev_err(dev, "no sensor pwdn pin available\n");
		return -ENODEV;
	}

    ret = devm_gpio_request_one(dev, ov5640_data->pwn_gpio, GPIOF_OUT_INIT_HIGH, "ov5640_pwdn");
    if (ret < 0)
	return ret;

    //设置rst-gpio
    ov5640_data->rst_gpio = of_get_named_gpio(dev->of_node, "rst_gpio", 0);
    if (!gpio_is_valid(ov5640_data->rst_gpio)) {
		dev_err(dev, "no sensor pwdn pin available\n");
		return -ENODEV;
	}

    ret = devm_gpio_request_one(dev, ov5640_data->rst_gpio, GPIOF_OUT_INIT_HIGH, "ov5640_reset");
    if (ret < 0)
	return ret;

    //设置sensor的初始状态
    
    

    ov5640_data->sensor_clk = devm_clk_get(dev, "csi_mclk"); //获取一个时钟对象
    if (IS_ERR(ov5640_data->sensor_clk)) {
		dev_err(dev, "get mclk failed\n");
		return PTR_ERR(ov5640_data->sensor_clk);
	}

    ret = of_property_read_u32(dev->of_node, "mclk", &ov5640_data->mclk);
	if (ret) {
		dev_err(dev, "mclk frequency is invalid\n");
		return ret;
	}

    ret = of_property_read_u32(dev->of_node, "mclk_source", buf);
    if (ret) {
		dev_err(dev, "mclk_source invalid\n");
		return ret;
	}
    ov5640_data->mclk_source = (u8)buf;

    ret = of_property_read_u32(dev->of_node, "csi_id", &(ov5640_data->csi));
	if (ret) {
		dev_err(dev, "csi_id invalid\n");
		return ret;
	}
    ov5640_set_clk_rate(ov5640_data);
    clk_prepare_enable(ov5640_data->sensor_clk); //开启时钟

    ov5640_data->io_init = ov5640_reset;
    ov5640_data->client = client;
    ov5640_data->pix.pixelformat = V4L2_PIX_FMT_YUYV;
    ov5640_data->pix.width = 640;
	ov5640_data->pix.height = 480;
	ov5640_data->streamcap.capability = V4L2_MODE_HIGHQUALITY |
					   V4L2_CAP_TIMEPERFRAME;
	ov5640_data->streamcap.capturemode = 0;
	ov5640_data->streamcap.timeperframe.denominator = DEFAULT_FPS;
	ov5640_data->streamcap.timeperframe.numerator = 1;

    ov5640_regulator_enable(&client->dev);
    ov5640_reset(ov5640_data);

    ov5640_power_down(0, ov5640_data);

    ret = ov5640_read_reg(OV5640_CHIP_ID_HIGH_BYTE, &chip_id_high, client);
    if (ret < 0 || chip_id_high != 0x56) {
		ov5640_regulator_disable(ov5640_data);
		clk_disable_unprepare(ov5640_data->sensor_clk);
		pr_warning("camera ov5640 is not found\n");
		return -ENODEV;
	}

    ret = ov5640_read_reg(OV5640_CHIP_ID_LOW_BYTE, &chip_id_low, client);
	if (ret < 0 || chip_id_low != 0x40) {
		ov5640_regulator_disable(ov5640_data);
		clk_disable_unprepare(ov5640_data->sensor_clk);
		pr_warning("camera ov5640 is not found\n");
		return -ENODEV;
	}

    
}

static int ov5640_remove(struct i2c_client *client)
{
    struct v4l2_subdev* sd = i2c_get_clientdata(client);



    return 0;
}


static const struct of_device_id ov5640_of_match[] = {
    {.compatible = "megumi,ov5640"},
    {}
};

MODULE_DEVICE_TABLE(of, ov5640_of_match);

static const struct i2c_device_id ov5640_id[] = {
    { "myov5640", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, ov5640_id);

static struct i2c_driver ov5640_driver = {
    .probe_new = ov5640_probe,
    .remove = ov5640_remove,
    .id_table = ov5640_id,
    .driver = {
        .of_match_table = ov5640_of_match,
        .owner = THIS_MODULE,
		.name = "ov5640",
    }
};

module_i2c_driver(ov5640_driver);

MODULE_AUTHOR("megumi");
MODULE_DESCRIPTION("OV5640 driver skeleton for NXP Linux 5.4");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
