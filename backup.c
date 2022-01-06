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

#define I2C_DEVICE "/dev/i2c-1"
#define I2C_DEV_ADDR 0x38 

int fd;

static int switch_hid_to_i2c(void) {
	unsigned char buf[4] = {0};
	
	buf[0] = 0xeb;
	buf[1] = 0xaa;
	buf[2] = 0x09;

	  if(write(fd, buf, 3) != 3) {
		  printf("\n write command failed");
		  return -1;
	  }

	  buf[0] = buf[1] = buf[2] = 0;

	  if(read(fd, buf, 3) != 3)
	  {
		  printf("\n Read command failed");
		  return -1;
	  }

	  if(buf[0] == 0xEB && buf[1] == 0xAA && buf[2] == 0x08)
	  {
		  printf("\n Successfully switched to i2c mode");
		  return 0;
	  } else
		  return -1;
}


static int enter_upgrade_mode(void) {

	unsigned char buf[3];
	buf[0] = 0x00;
	buf[1] = 0x40;

	if(write(fd, buf, 2) != 2)
		return -1;

	return 0;
}

static unsigned char* read_register(unsigned char read_buf[], int len)
{
	if(write(fd, read_buf, 1) != 1) {
		printf("\n Failed to write to the register");
		//return -1;
	}
	
	if(read(fd, read_buf, len) != len) {
		printf("\n Failed to read from the register");
		//return -1;
	}
	return read_buf;
}

static int confirm_scan_completion(void)
{
	unsigned char reg_val = 0, write_buf[3] = {0}, read_buf[2] = {0};
	int retries = 20;
        
	write_buf[0] = 0x00;
	write_buf[1] = 0xc0;
	i2c_smbus_write_byte_data(fd, 0x00, 0xc0);

	/*
	if(write(fd, write_buf, 2) != 2) {
		printf("\n Failed to write %x in register 0x00");
		return -1;
	}
*/
	read_buf[0] = 0x00;
        while(reg_val != 64 && retries >= 0) {
		sleep(2);
		reg_val = i2c_smbus_read_byte_data(fd,0x00);
		/*
		read_buf[0] = 0;
                if(read(fd, read_buf, 1) != 1) {
			printf("\n Failed to read from the register");
			break;
		}
		*/
                printf("\n Value of register 0x00 is : %d", reg_val);
                retries--;
        }

	printf("\n Outside while loop");
        if(retries < 0)
                return -1;

        return 0;

}

static int rawdata_test(void)
{
        unsigned char readraw1, readraw2, read_buf[2] = {0}, read_buf2[2] = {0}, write_buf[3] = {0}, write_buf2[3] = {0};
        int ret;

	ret = enter_upgrade_mode();
        if(ret < 0 ) {
                printf("Device didn't enter upgrade mode");
                return ret;
        }

	readraw1 = i2c_smbus_read_byte_data(fd, 0x02);
	readraw2 = i2c_smbus_read_byte_data(fd, 0x03);

        if(readraw1 < 0 || readraw2 < 0) {
                printf("\nUnable to read raw data registers");
		return -1;
	}
        else
		printf("\n reg1, reg2: %x, %x",readraw1, readraw2);

	ret = i2c_smbus_write_byte_data(fd, 0x0a, 0x81);
	if(ret < 0) {
		printf("\n Failed to write 0x81 into register 0x0a");
		return -1;
	}
	/*
	write_buf[0] = 0x0a;
	write_buf[1] = 0x81;
	if(write(fd, write_buf, 2) != 2) {
		printf("\n Failed to write %x in register %x",write_buf[1], write_buf[0]);
		return -1;
	}
	*/

        for( int i = 0; i < 3; i++)
        {
                ret = confirm_scan_completion();
                if( ret < 0 ) {
                        printf("\nError in scan completion");
                        return ret;
                }
        }

	ret = i2c_smbus_write_byte_data(fd, 0x01, 0xAA);
	if(ret < 0) {
		printf("\n Failed to write 0xAA into register 0x01");
		return -1;
	}
/*
	write_buf[0] = write_buf[1] = 0;
	write_buf2[0] = 0x01;
	write_buf2[1] = 0xAA;
	if(write(fd, write_buf2, 2) != 2) {
		printf("\n Failed to write %x in register %x",write_buf[1], write_buf[0]);
		return -1;
	}
*/
        int READRAW_DATA = (readraw1 + readraw2) * 2;

        unsigned char readraw_buf[READRAW_DATA];
        printf("\n READRAW_DATA %d\n\n",READRAW_DATA);
	readraw_buf[0] = 0x36;

	read_register(readraw_buf, READRAW_DATA);
        for( int i = 0; i < READRAW_DATA; i++)
		printf("%x\t",readraw_buf[i]);

}

int main() {

	int ret = 0;

	fd = open(I2C_DEVICE, O_RDWR);
	if(fd < 0) {
		printf("\n Error in opening the device");
		exit(1);
	}

	ret = ioctl(fd, I2C_SLAVE_FORCE, I2C_DEV_ADDR);
	if(ret < 0) {
		printf("\n Error in communicating with the device %d", errno);
		exit(1);
	}

	ret = switch_hid_to_i2c();
	if(ret < 0) {
		printf("\n Failed to switch to i2c mode");
		return ret;
	}

	ret = rawdata_test();
	if(ret < 0) {
		printf("\n Error returned from readraw test");
		return ret;
	}
	return 0;
}

