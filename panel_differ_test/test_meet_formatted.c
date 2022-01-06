#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <i2c/smbus.h>
#include "./ini_utils.h"

#define I2C_DEVICE "/dev/i2c-1"
#define I2C_DEV_ADDR 0x38

int fd;

void usage()
{
	fprintf(stderr, "Usage:\t\t./test -d <device> -v/r/s/q/p/a\n");
	fprintf(stderr, "Parameters:\n");
	fprintf(stderr, "\t-d <device>\tDevice to be tested\n");
	fprintf(stderr, "\t-v\t\tFirmware Version Test\n");
	fprintf(stderr, "\t-r\t\tRawdata Test\n");
	fprintf(stderr, "\t-s\t\tScap CB Test\n");
	fprintf(stderr, "\t-q\t\tScap Rawdata Test\n");
	fprintf(stderr, "\t-p\t\tPanel Differ Test\n");
	fprintf(stderr, "\t-a\t\tRun all tests\n");

	exit(EXIT_FAILURE);
}

static int switch_hid_to_i2c(void)
{
	unsigned char buf[3] = {
			0};

	buf[0] = 0xeb;
	buf[1] = 0xaa;
	buf[2] = 0x09;

	if (write(fd, buf, 3) != 3)
	{
		printf("\n write command failed");
		return -1;
	}

	buf[0] = buf[1] = buf[2] = 0;

	if (read(fd, buf, 3) != 3)
	{
		printf("\n Read command failed");
		return -1;
	}

	if (buf[0] == 0xEB && buf[1] == 0xAA && buf[2] == 0x08)
	{
		printf("\n Successfully switched to i2c mode");
		return 0;
	}
	else
		return -1;
}

static int switch_i2c_to_hid(void)
{

	int ret = 0;

	ret = i2c_smbus_write_byte_data(fd, 0xfc, 0xaa);
	if (ret < 0)
	{
		printf("\n Failed to write 0xaa into 0xfc register");
		return -1;
	}

	usleep(80000);

	ret = i2c_smbus_write_byte_data(fd, 0xfc, 0x66);
	if (ret < 0)
	{
		printf("\n Failed to write 0x66 into 0xfc register");
	}

	return 0;
}
static int enter_upgrade_mode(void)
{

	unsigned char buf[3];
	buf[0] = 0x00;
	buf[1] = 0x40;

	if (write(fd, buf, 2) != 2)
		return -1;

	return 0;
}

static unsigned char *read_register(unsigned char read_buf[], int len)
{
	if (write(fd, read_buf, 1) != 1)
	{
		printf("\n Failed to write to the register");
		//return -1;
	}

	if (read(fd, read_buf, len) != len)
	{
		printf("\n Failed to read from the register");
		//return -1;
	}
	return read_buf;
}

int test_data_waterproof_mode(unsigned char rx, unsigned char tx, unsigned char reg)
{
	int num, bit6, bit5, bit2;

	bit6 = (reg >> 6) & 1;
	bit5 = (reg >> 5) & 1;
	bit2 = (reg >> 2) & 1;

	if (bit5 == 0)
	{
		if (bit6 == 1)
		{
			if (bit2 == 0)
				num = tx;
			else
				num = rx;
		}
		else
			num = rx + tx;
	}
	else
	{
		printf("\n No Waterproof detection");
		return -1;
	}

	num = num * 2;
	return num;
}

int test_data_normal_mode(unsigned char rx, unsigned char tx, unsigned char reg)
{
	int num, bit7, bit1, bit0;

	bit7 = (reg >> 7) & 1;
	bit1 = (reg >> 1) & 1;
	bit0 = (reg >> 0) & 1;

	if (bit7 == 0)
	{
		if ((bit1 + bit0) == 0)
			num = tx;
		else if (bit1 + bit0 == 1)
			num = rx;
		else if (bit1 + bit0 == 2)
			num = rx + tx;
		else
		{
			printf("\n Error in normal mode test data");
			return -1;
		}
	}
	else
	{
		printf("\n No Normal mode detection");
		return -1;
	}

	num = num * 2;
	return num;
}

static int confirm_scan_completion(void)
{
	unsigned char reg_val = 0;
	int retries = 20;

	i2c_smbus_write_byte_data(fd, 0x00, 0xc0);

	while (reg_val != 64 && retries >= 0)
	{
		sleep(2);
		reg_val = i2c_smbus_read_byte_data(fd, 0x00);
		printf("\n Value of register 0x00 is : %d", reg_val);
		retries--;
	}
	if (retries < 0)
		return -1;

	return 0;
}

static int version_check_test(void)
{
	int ret, ver = 0;

	ret = enter_upgrade_mode();
	if (ret < 0)
	{
		printf("Device didn't enter upgrade mode");
		return ret;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x50, 0x40);
	if (ret < 0)
	{
		printf("\n Failed to write 0x40 into register 0x50");
		return -1;
	}

	ver = i2c_smbus_read_byte_data(fd, 0x51);
	if (ver < 0)
	{
		printf("\nInvalid firmware version");
		return -1;
	}

	printf("\nFirmware version is : %d", ver);

	return 0;
}

static int rawdata_test(configuration config)
{
	unsigned char readraw1, readraw2, readraw3;
	int ret, j = 0;

	ret = enter_upgrade_mode();
	if (ret < 0)
	{
		printf("Device didn't enter upgrade mode");
		return ret;
	}

	readraw1 = i2c_smbus_read_byte_data(fd, 0x02);
	readraw2 = i2c_smbus_read_byte_data(fd, 0x03);
	readraw3 = i2c_smbus_read_byte_data(fd, 0x0a);

	if (readraw1 < 0 || readraw2 < 0 || readraw3 < 0)
	{
		printf("\nUnable to read raw data registers");
		return -1;
	}
	else
		printf("\n reg1, reg2: %x, %x", readraw1, readraw2);

	ret = i2c_smbus_write_byte_data(fd, 0x0a, 0x81);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	for (int i = 0; i < 3; i++)
	{
		ret = confirm_scan_completion();
		if (ret < 0)
		{
			printf("\nError in scan completion");
			return ret;
		}
	}

	ret = i2c_smbus_write_byte_data(fd, 0x01, 0xAA);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	int READRAW_DATA = (readraw1 * readraw2) * 2;

	unsigned char readraw_buf[READRAW_DATA];
	int buf[READRAW_DATA / 2];
	printf("\n READRAW_DATA %d\n\n", READRAW_DATA);
	readraw_buf[0] = 0x36;

	read_register(readraw_buf, READRAW_DATA);

	for (int i = 0; i < READRAW_DATA - 1; i += 2)
	{
		buf[j] = readraw_buf[i] << 8 | readraw_buf[i + 1];
		printf("%x\t", buf[j]);
		j++;
	}

	for (int i = 0; i < READRAW_DATA / 2; i++) {
		int actual = buf[i];
		int max = config.RawData_Max_High_Tx.array[i];
		int min = config.RawData_Min_High_Tx.array[i];
		if (actual < min || actual > max) {
			printf("\n Rawdata test failed %d %d %d",actual, min, max);
			return -1;
		}
	}

	ret = i2c_smbus_write_byte_data(fd, 0x0a, readraw3);
	if (ret < 0)
	{
		printf("\n Failed to write %d into register 0x0a", readraw3);
		return -1;
	}

	return 0;
}

static int scap_cb_test(void)
{
	unsigned char scap_cb_reg1, scap_cb_reg2, scap_cb_reg3;
	int ret, j = 0;

	ret = enter_upgrade_mode();
	if (ret < 0)
	{
		printf("\n Device didn't enter upgrade mode");
		return -1;
	}
	/* Switch to no mapping status */

	ret = i2c_smbus_write_byte_data(fd, 0x54, 0x01);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	scap_cb_reg1 = i2c_smbus_read_byte_data(fd, 0x55);
	scap_cb_reg2 = i2c_smbus_read_byte_data(fd, 0x56);
	scap_cb_reg3 = i2c_smbus_read_byte_data(fd, 0x09);

	if (scap_cb_reg1 == -1 || scap_cb_reg2 == -1 || scap_cb_reg3 == -1)
		printf("\n Unable to read scap cb data registers");

	else
		printf("\n reg1, reg2, reg3 : %d, %d, %d \n", scap_cb_reg1, scap_cb_reg2, scap_cb_reg3);

	/* Set waterproof mode */

	unsigned char reg_44 = i2c_smbus_read_byte_data(fd, 0x44);
	ret = i2c_smbus_write_byte_data(fd, 0x44, 0x01);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	unsigned char reg_45 = i2c_smbus_read_byte_data(fd, 0x45);
	ret = i2c_smbus_write_byte_data(fd, 0x45, 0x00);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	int SCAP_CB_DATA = test_data_waterproof_mode(scap_cb_reg1, scap_cb_reg2, scap_cb_reg3);
	unsigned char scap_cb_buf[SCAP_CB_DATA];
	int buf[SCAP_CB_DATA / 2];
	scap_cb_buf[0] = 0x4e;

	read_register(scap_cb_buf, SCAP_CB_DATA);
	for (int i = 0; i < SCAP_CB_DATA - 1; i += 2)
	{
		buf[j] = scap_cb_buf[i] << 8 | scap_cb_buf[i + 1];
		printf("%x\t", buf[j]);
		j++;
	}

	printf("\n\n");

	/* Set normal mode */

	ret = i2c_smbus_write_byte_data(fd, 0x44, 0x00);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x45, 0x00);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	SCAP_CB_DATA = test_data_normal_mode(scap_cb_reg1, scap_cb_reg2, scap_cb_reg3);
	unsigned char scap_cb_buf2[SCAP_CB_DATA];
	int buf2[SCAP_CB_DATA / 2];
	scap_cb_buf2[0] = 0x4e;
	j = 0;
	read_register(scap_cb_buf2, SCAP_CB_DATA);

	for (int i = 0; i < SCAP_CB_DATA - 1; i += 2)
	{
		buf2[j] = scap_cb_buf2[i] << 8 | scap_cb_buf2[i + 1];
		printf("%x\t", buf2[j]);
		j++;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x44, reg_44);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x45, reg_45);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	return 0;
}

static int scap_rawdata_test(void)
{
	unsigned char scap_rw_reg1, scap_rw_reg2, scap_rw_reg3;
	int ret, j = 0;

	ret = enter_upgrade_mode();
	if (ret < 0)
	{
		printf("\n Device didn't enter upgrade mode");
		return -1;
	}
	/* Switch to no mapping status */

	unsigned char reg_54 = i2c_smbus_read_byte_data(fd, 0x54);
	ret = i2c_smbus_write_byte_data(fd, 0x54, 0x01);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	scap_rw_reg1 = i2c_smbus_read_byte_data(fd, 0x55);
	scap_rw_reg2 = i2c_smbus_read_byte_data(fd, 0x56);
	scap_rw_reg3 = i2c_smbus_read_byte_data(fd, 0x09);

	if (scap_rw_reg1 == -1 || scap_rw_reg2 == -1 || scap_rw_reg3 == -1)
		printf("\n Unable to read scap cb data registers");

	else
		printf("\n reg1, reg2, reg3 : %d, %d, %d \n", scap_rw_reg1, scap_rw_reg2, scap_rw_reg3);

	ret = confirm_scan_completion();
	if (ret < 0)
	{
		printf("\nError in scan completion");
		return ret;
	}

	/* Set waterproof mode */

	ret = i2c_smbus_write_byte_data(fd, 0x01, 0xAC);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	int SCAP_RW_DATA = test_data_waterproof_mode(scap_rw_reg1, scap_rw_reg2, scap_rw_reg3);

	unsigned char scap_rawdata_buf[SCAP_RW_DATA];
	int buf[SCAP_RW_DATA / 2];
	scap_rawdata_buf[0] = 0x36;

	read_register(scap_rawdata_buf, SCAP_RW_DATA);

	for (int i = 0; i < SCAP_RW_DATA - 1; i += 2)
	{
		buf[j] = scap_rawdata_buf[i] << 8 | scap_rawdata_buf[i + 1];
		printf("%x\t", buf[j]);
		j++;
	}

	printf("\n\n");

	/* Set normal mode */

	ret = i2c_smbus_write_byte_data(fd, 0x01, 0xAB);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	SCAP_RW_DATA = test_data_normal_mode(scap_rw_reg1, scap_rw_reg2, scap_rw_reg3);
	unsigned char scap_rawdata_buf2[SCAP_RW_DATA];
	int buf2[SCAP_RW_DATA / 2];
	scap_rawdata_buf2[0] = 0x36;
	j = 0;

	read_register(scap_rawdata_buf2, SCAP_RW_DATA);

	for (int i = 0; i < SCAP_RW_DATA - 1; i += 2)
	{
		buf2[j] = scap_rawdata_buf2[i] << 8 | scap_rawdata_buf2[i + 1];
		printf("%x\t", buf2[j]);
		j++;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x54, reg_54);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}
}

static int panel_differ_test(configuration config)
{
	unsigned char pd_reg1, pd_reg2;
	int ret, j = 0;

	ret = enter_upgrade_mode();
	if (ret < 0)
	{
		printf("Device didn't enter upgrade mode");
		return ret;
	}

	pd_reg1 = i2c_smbus_read_byte_data(fd, 0x02);
	pd_reg2 = i2c_smbus_read_byte_data(fd, 0x03);

	if (pd_reg1 < 0 || pd_reg2 < 0)
	{
		printf("\nUnable to read raw data registers");
		return -1;
	}
	else
		printf("\n reg1, reg2: %x, %x", pd_reg1, pd_reg2);

	unsigned char reg_0a = i2c_smbus_read_byte_data(fd, 0x0a);
	ret = i2c_smbus_write_byte_data(fd, 0x0a, 0x81);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	unsigned char reg_16 = i2c_smbus_read_byte_data(fd, 0x16);
	ret = i2c_smbus_write_byte_data(fd, 0x16, 0x00);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	unsigned char reg_fb = i2c_smbus_read_byte_data(fd, 0xfb);
	ret = i2c_smbus_write_byte_data(fd, 0xfb, 0x00);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	for (int i = 0; i < 3; i++)
	{
		ret = confirm_scan_completion();
		if (ret < 0)
		{
			printf("\nError in scan completion");
			return ret;
		}
	}

	ret = i2c_smbus_write_byte_data(fd, 0x01, 0xAA);
	if (ret < 0)
	{
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}

	int PD_DATA = (pd_reg1 * pd_reg2) * 2;

	unsigned char pd_buf[PD_DATA];
	int buf[PD_DATA / 2];
	printf("\n PD %d\n\n", PD_DATA);
	pd_buf[0] = 0x36;

	read_register(pd_buf, PD_DATA);
	for (int i = 0; i < PD_DATA - 1; i += 2)
	{
		buf[j] = pd_buf[i] << 8 | pd_buf[i + 1];
		buf[j] = buf[j] / 10;
		printf("%x\t", buf[j]);
		j++;
	}

	for (int i = 0; i < PD_DATA / 2; i++) {
		int min = config.Panel_Differ_Min_Tx.array[i];
		int max = config.Panel_Differ_Max_Tx.array[i];
		int actual = buf[i];
		if (actual < min || actual > max) {
			printf("Panel differ test failed %d %d %d",actual, min, max);
			return -1;
		}
	}

	ret = i2c_smbus_write_byte_data(fd, 0x0a, reg_0a);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(fd, 0x16, reg_16);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	ret = i2c_smbus_write_byte_data(fd, 0xfb, reg_fb);
	if (ret < 0)
	{
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{

	int ret = 0, opt;

	int rawdata = 0, scap_cb = 0, scap_rawdata = 0, panel_differ = 0, version = 0;

	while ((opt = getopt(argc, argv, "vrsqpa")) != -1)
	{
		switch (opt)
		{
		case 'v':
			version = 1;
			break;
		case 'r':
			rawdata = 1;
			break;
		case 's':
			scap_cb = 1;
			break;
		case 'q':
			scap_rawdata = 1;
			break;
		case 'p':
			panel_differ = 1;
			break;
		case 'a':
			version = rawdata = scap_cb = scap_rawdata = panel_differ = 1;
			break;
		default:
			usage();
		}
	}

	fd = open(I2C_DEVICE, O_RDWR);
	if (fd < 0)
	{
		printf("\n Error in opening the device");
		exit(1);
	}

	ret = ioctl(fd, I2C_SLAVE_FORCE, I2C_DEV_ADDR);
	if (ret < 0)
	{
		printf("\n Error in communicating with the device %d", errno);
		exit(1);
	}

	if (switch_hid_to_i2c() < 0)
	{
		printf("\n Failed to switch to i2c mode");
		return -1;
	}

	configuration config;
	read_config(&config);

	if (version)
	{
		if (version_check_test() < 0)
		{
			printf("\n Error occurred in Firmware version test");
			return -1;
		}
	}

	if (rawdata)
	{
		if (rawdata_test(config) < 0)
		{
			printf("\n Error occured in Rawdata test");
			return -1;
		}
	}

	printf("\n\n");

	if (scap_cb)
	{
		if (scap_cb_test() < 0)
		{
			printf("\n Error occured in Scap CB test");
			return -1;
		}
	}

	if (scap_rawdata)
	{
		if (scap_rawdata_test() < 0)
		{
			printf("\n Error occured in Scap Rawdata test");
			return -1;
		}
	}

	printf("\n\n");

	if (panel_differ)
	{
		if (panel_differ_test(config) < 0)
		{
			printf("\n Error occurred in Panel Differ test");
			return -1;
		}
	}

	if (switch_i2c_to_hid() < 0)
	{
		printf("\n Failed to switch to HID mode");
		return -1;
	}

	return 0;
}
