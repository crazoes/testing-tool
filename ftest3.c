// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * (C) 2021 Collabora ltd.
 */

#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <time.h>
#include <error.h>

#ifndef HIDIOCSFEATURE
#warning Please have your distro update the userspace kernel headers
#define HIDIOCSFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x06, len)
#define HIDIOCGFEATURE(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x07, len)
#endif
#define HIDIOCGINPUT(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x0A, len)
#define HIDIOCSOUTPUT(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x0B, len)
#define HIDIOCGOUTPUT(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x0C, len)
#define HIDIOCSINPUT(len)    _IOC(_IOC_WRITE|_IOC_READ, 'H', 0x09, len)

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define WRITE_REQ_LEN 63
#define READ_REQ_LEN 6
#define READ_LEN 66
#define TAIL_LEN 1
#define DATA_PACKET_HEADER 1
#define CMD_HEADER_LEN 0x5
#define DATA_PACKET_LEN (WRITE_REQ_LEN -		\
			 (CMD_HEADER_LEN + DATA_PACKET_HEADER + TAIL_LEN ))

#define PAYLOAD_SIZE (DATA_PACKET_HEADER+DATA_PACKET_LEN)

#define FT_CHIP_ID 0x542C

unsigned char *fw_map;
unsigned char no_mapping[2] = {0x54, 0x01};
size_t fw_size;
int fd;
int verbose;
int write_value;

#define VERBOSE_PRINT(lvl, fmt, ...)				\
	do {							\
		if (verbose >= lvl)				\
			fprintf(stderr, fmt, ##__VA_ARGS__);	\
	} while (0)

#define DEBUG(fmt, ...) VERBOSE_PRINT(2, fmt, ##__VA_ARGS__)
#define LOG(fmt, ...) VERBOSE_PRINT(1, fmt, ##__VA_ARGS__)
#define WARN(fmt, ...) VERBOSE_PRINT(1, "WARN: "fmt, ##__VA_ARGS__)
#define INFO(fmt, ...) VERBOSE_PRINT(0, fmt, ##__VA_ARGS__)

#define ERROR(fmt, ...) error(0, errno, "ERROR: " fmt, ##__VA_ARGS__)
#define DIE(fmt, ...) error(1, errno, "FATAL: " fmt, ##__VA_ARGS__)

#define MIN(x, y) ((x)<(y))? (x):(y)

static unsigned char checksum(unsigned char *data, size_t len)
{
	unsigned int i=0;
	unsigned char checksum=0;

	for(i = 0; i < len; i++)
		checksum ^= data[i];

	checksum++;
	return checksum;
}

static unsigned int checksum32(unsigned char *data, size_t len)
{
	unsigned int checksum = 0;
	size_t i;

	for(i = 0; i < len ; i += 4) {
		checksum ^= ((data[i + 3] << 24) +
			     (data[i + 2] << 16) +
			     (data[i + 1] << 8) +
			     data[i]);
	}

	checksum += 1;

	return checksum;
}

static int submit_request(unsigned char *buf, int len)
{
	int i, ret;

	DEBUG("\n[WRITE (%d)] ", len);
	for (i = 0; i < len; i++)
		DEBUG("%.2hhx ", buf[i]);
	DEBUG("\n");

	ret = write(fd, buf, len);
	if (ret < 0)
		return ret;
	return 0;
}

static int send_write_request(unsigned char cmd, unsigned char* data, int data_len)
{
	unsigned char buf[WRITE_REQ_LEN + 1];
	unsigned char *message = &buf[1];
	unsigned char len = CMD_HEADER_LEN + data_len;
	int i;

	/* First byte is not part of the message and must be zero. */
	buf[0] = 6;

	message[0] = 0xff;
	message[1] = 0xff;
	message[2] = len;
	message[3] = cmd;

	memcpy(&message[4], data, data_len);
	i = 4 + data_len;

	message[i++] = checksum(message, len - 1);

	for (; i < (WRITE_REQ_LEN + 1) ; i++)
		message[i] = 0xff;

	return submit_request(buf, WRITE_REQ_LEN + 1);
}


static int read_response(unsigned char *buf, size_t len)
{
	int ret;
	int retries = 5;

	memset(buf, '\0', len);

	do {
		usleep (20000);
		ret = read(fd, buf, len);
	} while (ret < 0 && retries--);

	DEBUG("[RESPONSE (%d)]", ret);
	if (ret > 0)
		for (int i = 0; i < ret; i++)
			DEBUG(" %hhx", buf[i]);
	else
		DEBUG(" [FAILED]");

	DEBUG("\n");

	return ret;
}

#define CMD_ENTER_UPGRADE_MODE	0x40
#define CMD_CHECK_CURRENT_STATE	0x41
#define CMD_READY_FOR_UPGRADE	0x42
#define CMD_SEND_DATA		0x43
#define CMD_UPGRADE_CHECKSUM	0x44
#define CMD_EXIT_UPGRADE_MODE	0x45
#define CMD_USB_READ_UPGRADE_ID	0x46
#define CMD_ERASE_FLASH		0x47
#define CMD_READ_REGISTER	0x50
#define CMD_WRITE_REGISTER	0x51
#define VERSION_REGISTER	0xA6

#define WRITE_DATA_REQ(cmd, data, len) do { 				\
	if (send_write_request(cmd, data, len))				\
		ERROR("Failed to send command to device.");		\
	} while(0)

#define WRITE_REQ(cmd) WRITE_DATA_REQ(cmd, NULL, 0)

void usage()
{
	fprintf(stderr, "Usage: \n");
	fprintf(stderr, "\tUpdate:\n\t\t./ftupd -d <device> -f <firmware_file>\n");
	fprintf(stderr, "\tCheck version:\n\t\t./ftupd -d <device> -c\n");
	fprintf(stderr, "Parameters:\n");
	fprintf(stderr, "\t-d <device>\tDevice to be updated\n");
	fprintf(stderr, "\t-f <file>\tFirmware file to upload\n");
	fprintf(stderr, "\t-r\t\tRescue mode.  Try to recover the device after a partial updated\n");
	fprintf(stderr, "\t-y\t\tNon-interactive mode\n");
	fprintf(stderr, "\t-c\t\tcheck the current firmware version\n");
	fprintf(stderr, "\t-q\t\tQuiet mode.\n");
	fprintf(stderr, "\t-v\t\tVerbose output \n");
	fprintf(stderr, "\t-vv\t\tDebug output \n");

	exit(EXIT_FAILURE);
}

#define RESPONSE_SIZE 64

int parse_accepted_command_reply(unsigned char *buf, unsigned int len)
{
	if (len != RESPONSE_SIZE)
		return -EINVAL;

	if (buf[3] == 0x5 && buf[4] == 0xf0 && buf[5] == 0xf6)
		return 0;

	return 1;


}

int change_device_mode(int enter)
{
	int ret = -1;
	unsigned char buf[RESPONSE_SIZE];
	unsigned char cmd = enter ? CMD_ENTER_UPGRADE_MODE : CMD_EXIT_UPGRADE_MODE;

	WRITE_REQ(cmd);

	memset(buf, '\0', RESPONSE_SIZE);

	ret = read_response(buf, RESPONSE_SIZE);

	if (ret > 0 && !parse_accepted_command_reply(buf, RESPONSE_SIZE)) {
		DEBUG("Accepted\n");
		ret = 0;
	} else {
		DEBUG("Failed\n");
	}


	return ret;
}

int device_in_upgrade_mode()
{
	unsigned char buf[RESPONSE_SIZE];

	WRITE_REQ(CMD_CHECK_CURRENT_STATE);
	read_response(buf, RESPONSE_SIZE);

	if (buf[3] == 0x06 && buf[4] == 0x41 && buf[5] == 0x1 && buf[6] == 0x47) {
		DEBUG("Device is in upgrade mode\n");
		return 1;
	}

	return 0;
}

int enter_upgrade_mode()
{
	int retries = 15;
	int i;

	for (i = 0; i < retries; i++) {
		LOG("Entering upgrade mode [%d/%d]... ", (i+1), retries);

		change_device_mode(1);

		sleep (1);

		if (device_in_upgrade_mode()) {
			LOG("Success.\n");
			return 0;
		}

		LOG("failed.\n");

	}

	INFO("Couldn't enter Upgrade Mode.\n");

	return -EIO;
}

int exit_upgrade_mode()
{
	int retries = 15;
	int i;

	for (i = 0; i < retries; i++) {
		LOG("Exiting upgrade mode [%d/%d]... ", (i+1), retries);

		change_device_mode(0);

		sleep (1);

		if (!device_in_upgrade_mode()) {
			LOG("Success.\n");
			return 0;
		}

		LOG("failed.\n");
	}

	INFO("Couldn't exit Upgrade Mode.\n");

	return -EIO;
}

int validate_chip_id()
{
	unsigned char buf[RESPONSE_SIZE];
	unsigned int id;

	LOG("Reading chip ID.\n");

	WRITE_REQ(CMD_USB_READ_UPGRADE_ID);
	read_response(buf, RESPONSE_SIZE);

	id = buf[5] << 8 | buf[6];

	if (id != FT_CHIP_ID) {
		LOG("Unexpected Chip ID %hx", id);
		return -EIO;
	}
	LOG("Good chip ID.\n");
	return 0;
}

int check_ready_for_upgrade()
{
	unsigned char buf[RESPONSE_SIZE];

	LOG("Sending READY_FOR_UPGRADE.\n");

	WRITE_REQ(CMD_READY_FOR_UPGRADE);
	read_response(buf, RESPONSE_SIZE);

	if (buf[3] == 0x06 && buf[4] == 0x42
	    && buf[5] == 0x02 && buf[6] == 0x47) {
		LOG("Device is ready for upgrade.\n");
		return 0;
	}
	return -EIO;
}
#define CMD_SET_FLASH_LEN	0xB0

static unsigned int set_flash_length()
{
	int ret;
	unsigned char buf[RESPONSE_SIZE];
	unsigned char flash_len[4] = {00, 01, 00, 00};

	LOG("Sending SET_FLASH_LEN.\n");

	WRITE_DATA_REQ(CMD_SET_FLASH_LEN, flash_len, 4);

	ret = read_response(buf, RESPONSE_SIZE);

	if (ret > 0 && !parse_accepted_command_reply(buf, RESPONSE_SIZE))
	    return 0;

	return -EIO;

}

static int write_register(unsigned char write_reg[])
{
	int ret;
	unsigned char buf[RESPONSE_SIZE];

	WRITE_DATA_REQ(CMD_WRITE_REGISTER, write_reg, 2);

	ret = read_response(buf, RESPONSE_SIZE);
/*
	for(int i = 0; i < RESPONSE_SIZE; i++)
		INFO("%x\t", buf[i]);
*/

	if (ret && buf[3] == 0x6 && buf[4] == CMD_WRITE_REGISTER &&
	    buf[5] == write_reg[0])
	{
		printf("\nExecuted successfully %x and %x", buf[5], buf[6]);
		return 0;
	}
	printf("\n buf 5 and buf 6 : %d , %d", buf[5], buf[6]);
	return -1;

}

static unsigned int read_register(unsigned char reg)
{
	int ret;
	unsigned char buf[RESPONSE_SIZE];

	WRITE_DATA_REQ(CMD_READ_REGISTER, &reg, 1);

	ret = read_response(buf, RESPONSE_SIZE);
/*	
	for(int i=0; i< RESPONSE_SIZE; i++)
		INFO("%x\t", buf[i]);
*/
	if (ret && buf[3] == 0x7 && buf[4] == CMD_READ_REGISTER &&
	    buf[5] == reg)
		return buf[6];
	return -1;

}

unsigned int get_device_checksum(unsigned int *checksum)
{
	unsigned char buf[RESPONSE_SIZE];

	LOG("Sending UPGRADE_CHECKSUM.\n");

	WRITE_REQ(CMD_UPGRADE_CHECKSUM);
	read_response(buf, RESPONSE_SIZE);

	if (buf[3] == 0x09 && buf[4] == 0x44) {
		*checksum = *(unsigned int*)&buf[5];
		LOG("checksum is: %x\n", *checksum);
		return 0;
	}
	return -EIO;
}

unsigned int erase_flash()
{
	unsigned char buf[RESPONSE_SIZE];
	int ret;

	LOG("Sending CMD_ERASE_FLASH.\n");

	WRITE_REQ(CMD_ERASE_FLASH);

	sleep (1);

	ret = read_response(buf, RESPONSE_SIZE);

	if (ret > 0 && !parse_accepted_command_reply(buf, RESPONSE_SIZE))
		return 0;

	return -EIO;
}

void version_check_test(void)
{
    unsigned int current_fw_version = read_register(VERSION_REGISTER);

	if (current_fw_version == -1)
		WARN("Unable to read current firwmware version\n");
	else
		INFO("Device Firmware version: %d\n", current_fw_version);
}


static int confirm_scan_completion(void)
{
	unsigned char reg_val = 0;
	int retries = 10;
	//WRITE_REQ(0xc0);

	while(reg_val != 64 && retries >= 0) {
		reg_val = read_register(0x00);
		printf("\n Value of register 0x00 is : %x", reg_val);
		retries--;
	}

	if(retries < 0)
		return -1;

	return 0;
}


static int rawdata_test(void)
{
	unsigned char readraw1, readraw2, readraw3;
	int ret;
	unsigned char rawdata_write[2] = {0x00, 0x40};
	write_register(rawdata_write);

/*	ret = enter_upgrade_mode();
	if(ret < 0 ) {
		ERROR("Device didn't enter upgrade mode");
		return -1;
	}
*/
	readraw1 = read_register(0x02);
	readraw2 = read_register(0x03);

	if(readraw1 == -1 || readraw2 == -1)
		WARN("\nUnable to read raw data registers");
	else
		printf("\n reg1, reg2: %d, %d",readraw1, readraw2);

	rawdata_write[0] = 0x0a;
	rawdata_write[1] = 0x81;
	write_register(rawdata_write);

	rawdata_write[0] = 0x00;
	rawdata_write[1] = 0xc0;

	for( int i = 0; i < 3; i++)
	{
		write_register(rawdata_write);
		ret = confirm_scan_completion();
		if( ret < 0 ) {
			ERROR("\nError in scan completion");
			return ret;
		}
	}

	rawdata_write[0] = 0x01;
	rawdata_write[1] = 0xAA;
	write_register(rawdata_write);


	int READRAW_DATA = (readraw1 + readraw2) * 2;

	unsigned int buf[READRAW_DATA];
	printf("\n READRAW_DATA %d\n\n",READRAW_DATA);


	for( int i = 0; i < READRAW_DATA; i++)
	{
		buf[i] = read_register(0x36);
		printf("%x\t", buf[i]);
	}	

}

static int scap_cb_test(void)
{
	unsigned char scap_cb_reg1, scap_cb_reg2, scap_cb_reg3;
	int ret;

	ret  = enter_upgrade_mode();
	if(ret < 0) {
		ERROR("Device didn't enter upgrade mode");
		return -1;
	}
	/* Switch to no mapping status */
	ret = write_register(no_mapping);
	if(ret) {
		WARN("\n Failed to switich to no mapping status");
		return ret;
	}

	scap_cb_reg1 = read_register(0x55);
	scap_cb_reg2 = read_register(0x56);
	scap_cb_reg3 = read_register(0x09);

	if(scap_cb_reg1 == -1 || scap_cb_reg2 == -1 || scap_cb_reg3 == -1)
		WARN("Unable to read scap cb data registers");

	else
		printf("\n reg1, reg2, reg3 : %d, %d, %d \n", scap_cb_reg1, scap_cb_reg2, scap_cb_reg3);

	/* Set waterproof mode */
	unsigned char waterproof_mode[2] = {0x44, 0x01};
	write_register(waterproof_mode);

	unsigned char scap_cb_write1[2] = {0x45, 0x00};
	write_register(scap_cb_write1);

	int SCAP_CB_DATA = (scap_cb_reg1 + scap_cb_reg2) * 2;
	unsigned char buf[SCAP_CB_DATA];

	for( int i = 0; i < SCAP_CB_DATA; i++)
	{
		buf[i] = read_register(0x4e);
		printf("%d\t", buf[i]);
	}

	/* Set normal mode */
	unsigned char normal_mode[2] = {0x44, 0x00};
	write_register(normal_mode);

	write_register(scap_cb_write1);

	for( int i = 0; i < SCAP_CB_DATA; i++)
	{
		buf[i] = read_register(0x4e);
		printf("%d\t", buf[i]);
	}
}


static int scap_rawdata_test(void)
{
	int ret;
	unsigned char scap_rd_reg1, scap_rd_reg2, scap_rd_reg3, scap_rd_reg4;

	ret = enter_upgrade_mode();
	if(ret < 0) {
		ERROR("\n Device didn't enter upgrade mode");
		return -1;
	}

	/* Switch to no mapping status */
	ret = write_register(no_mapping);
	if(ret) {
		WARN("\n Failed to switich to no mapping status");
		return ret;
	}

	scap_rd_reg1 = read_register(0x55);
	scap_rd_reg2 = read_register(0x56);
	scap_rd_reg3 = read_register(0x09);

	if(scap_rd_reg1 == -1 || scap_rd_reg2 == -1 || scap_rd_reg3 == -1)
		WARN("\n Unable to read scap cb data registers");
	else
		printf("\n reg1, reg2, reg3 %d, %d, %d",scap_rd_reg1, scap_rd_reg2, scap_rd_reg3);	


	/* Scanning rawdata */
	WRITE_REQ(0xc0);
	ret = confirm_scan_completion();
	if(ret < 0) {
		ERROR("\n Error in scan completion");
		return ret;
	}

	unsigned char scap_rd_write1[2] = {0x01, 0xAC};
	write_register(scap_rd_write1);

	int SCAP_RD_DATA = (scap_rd_reg1 + scap_rd_reg2) * 2;
	printf("SCAP_RD_DATA %d \n", SCAP_RD_DATA);
	unsigned int buf[SCAP_RD_DATA];

	for( int i = 0; i < SCAP_RD_DATA; i++)
	{
		buf[i] = read_register(0x36);
		printf("%x\t", buf[i]);
	}

	printf("\n\n");

	unsigned char scap_rd_write2[2] = {0x01, 0xAC};
	write_register(scap_rd_write2);

	for(int i = 0; i < SCAP_RD_DATA; i++)
	{
		buf[i] = read_register(0x36);
		printf("%x\t", buf[i]);
	}

}

static int panel_differ_test(void)
{
	unsigned char pd_reg1, pd_reg2;
	int ret;
	ret = enter_upgrade_mode();
	if(ret < 0 ) {
		ERROR("Device didn't enter upgrade mode");
		return -1;
	}

	pd_reg1 = read_register(0x02);
	pd_reg2 = read_register(0x03);

	if(pd_reg1 == -1 || pd_reg2 == -1)
		WARN("Unable to read raw data registers");
	else
		printf("\n reg1, reg2: %d, %d",pd_reg1, pd_reg2);

	unsigned char pd_write1[2] = {0x0a, 0x81};
	write_register(pd_write1);

	unsigned char pd_write2[2] = {0x16, 0x00};
	write_register(pd_write2);

	unsigned char pd_write3[2] = {0xfb, 0x00};
	write_register(pd_write3);

	unsigned char pd_write4[2] = {0x00, 0xc0};
	for( int i = 0; i < 3; i++)
	{
		write_register(pd_write4);
		ret = confirm_scan_completion();
		if( ret < 0 ) {
			ERROR("\nError in scan completion");
			return ret;
		}
	}

	unsigned char pd_write5[2] = {0x01, 0xAA};
	write_register(pd_write2);

	int PD_DATA = (pd_reg1 + pd_reg2) * 2;
	unsigned int buf[PD_DATA];
	printf("\n PD_DATA %d\n\n",PD_DATA);


	for( int i = 0; i < PD_DATA; i++)
	{
		buf[i] = read_register(0x36);
		printf("%x\t", buf[i]);
	}	
	//exit_upgrade_mode();
}



int main(int argc, char **argv)
{
	char *device, *fw_path;
	int opt;
	int fw_fd;
	int version = 0, rawdata = 0, scap_cb = 0, scap_rawdata = 0, panel_differ = 0;
	int interactive_mode = 1;

	unsigned int fw_checksum;
	unsigned int new_device_checksum;

	while ((opt = getopt(argc, argv, "f:d:vreyqcp")) != -1) {
		switch (opt) {
		case 'd':
			device = strdup(optarg);
			if (!device)
				DIE("Can't allocate memory.");
			break;
		case 'f':
			fw_path = strdup(optarg);
			if (!fw_path)
				DIE("Can't allocate memory.");
			break;
		case 'v':
			verbose++;
			break;
		case 'r':
			rawdata = 1;
			break;
		case 's':
			scap_cb = 1;
			break;
		case 'y':
			scap_rawdata = 1;
			break;
		case 'q':
			verbose = -1;
			break;
		case 'c':
			version = 1;
			break;
		case 'p':
			panel_differ = 1;
			break;
		default:
			usage();
		}
	}

	if (!device)
		usage();

	fd = open(device, O_RDWR|O_NONBLOCK);
	if (fd < 0) {
		DIE("Unable to open %s", device);
	}
	if(version)
		version_check_test();

	if(rawdata) {
		rawdata_test();
	}

	if(scap_cb)
		scap_cb_test();

	if(scap_rawdata)
		scap_rawdata_test();

	if(panel_differ)
		panel_differ_test();

    return 0;
}

