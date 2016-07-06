// $Id: licut_probe.cpp 1 2011-01-28 21:55:10Z henry_groover $

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>
#include <linux/serial.h>

#include "licut_probe.h"

char LicutProbe::errmsg[256] = {0};

int LicutProbe::Open( int verbose /*= 0*/ )
{
	// Determine tty
	// Get lsusb -v output
	FILE *lsusb = popen( "lsusb -v 2> /dev/null", "r" );
	if (!lsusb)
	{
		sprintf( errmsg, "Failed to open lsusb -v (%d: %s)\n", errno, strerror(errno) );
		return -1;
	}

	char buff[1024];
	bool found_ftdi = false;
	bool in_ftdi = false;
	unsigned int bus, device, endpoint;
	bool found_devname = false;
	char devpath[256];
	if (verbose) printf( "Opened lsusb -v\n" );
	while (fgets( buff, sizeof(buff), lsusb ) && !found_devname)
	{
		// Pared down search from "ID 20d3:0011 Future Technology Devices International"
		if (!in_ftdi && !found_ftdi && strstr( buff, "ID 20d3:0011" ))
		{
			in_ftdi = true;
			found_ftdi = true;
			endpoint = 0;
			sscanf( buff, "Bus %u Device %u", &bus, &device );
			if (verbose) printf( "Found FTDI entry bus %u device %u\n", bus, device );
		}
		else if (in_ftdi && !strncmp( buff, "Bus ", 4 ))
		{
			in_ftdi = false;
		}

		if (!in_ftdi) continue;

		if (strstr( buff, "bEndpointAddress" ))
		{
			unsigned int test_ep;
			if (sscanf( buff, " bEndpointAddress 0x%x", &test_ep ) == 1)
			{
				char class_dirname[256];
				sprintf( class_dirname, "/sys/class/usb_endpoint/usbdev%u.%u_ep%02x/device", bus, device, test_ep );
				if (verbose) printf( "%s() lsusb -v output line: %s\nScanned endpoint %x\nOpening %s\n",
					__FUNCTION__, buff, test_ep, class_dirname );
				DIR *class_dir = opendir( class_dirname );
				if (!class_dir) printf( "%s() - failed to open dir %s\n", __FUNCTION__, class_dirname );
				else
				{
					while (struct dirent *d = readdir( class_dir ))
					{
						if (d->d_name[0] == '.') continue;
						if (verbose) printf( "%s/%s\n", class_dirname, d->d_name );
						if (!strncmp( d->d_name, "ttyUSB", 6 ))
						{
							found_devname = true;
							endpoint = test_ep;
							sprintf( devpath, "/dev/%s", d->d_name );
							break;
						}
					}
					closedir( class_dir );
				}
			}
			else
			{
				printf( "%s() - failed to scan address from %s", __FUNCTION__, buff );
			}
		}
	}

	pclose( lsusb );

	if (!found_devname)
	{
		if (found_ftdi)
		{
			printf( "Found FTDI USB serial port but no endpoint - assuming /dev/ttyUSB0\n" );
			sprintf( devpath, "/dev/ttyUSB0" );
		}
		else
		{
			sprintf( errmsg, "Could not find FTDI USB serial device - is the device turned on and connected?" );
			return 0;
		}
	}

	int handle = open( devpath, O_RDWR | O_NOCTTY );
	if (handle <= 0)
	{
		sprintf( errmsg, "Failed to open %s - %d (%s)\n", devpath, errno, strerror(errno) );
		return -1;
	}

	if (verbose) printf( "Opened %s handle %d\n", devpath, handle );

        struct termios oldtio,newtio;
        
        tcgetattr( handle, &oldtio ); /* save current port settings */
    
	if (verbose) printf( "setting parameters\n" );
        bzero( &newtio, sizeof(newtio) );
	// Set custom rate to 200kbps 8N1 - we're actually sending 8N2 but get 8N1 back
        newtio.c_cflag = B38400 | /*CRTSCTS |*/ CS8 | CLOCAL | CREAD;
        newtio.c_iflag = IGNPAR;
        newtio.c_oflag = 0;
        
        /* set input mode (non-canonical, no echo,...) */
        newtio.c_lflag = 0;
         
        newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
        newtio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */
        
        tcflush( handle, TCIFLUSH );
        tcsetattr( handle, TCSANOW, &newtio );

	// Now use TIOCSSERIAL ioctl to set custom divisor
	// FTDI uses base_baud 24000000 so in theory a divisor
	// of 120 should give us 200000 baud...
	struct serial_struct sio; // From /usr/include/linux/serial.h
	int ioctl_res = ioctl( handle, TIOCGSERIAL, &sio );
	if (ioctl_res < 0)
	{
		sprintf( errmsg, "Failed TIOCGSERIAL ioctl: error %d (%s)\n", errno, strerror(errno) );
		close( handle );
		return -1;
	}

	if (verbose) printf( "ioctl(TIOCGSERIAL) returned %d, flags was %04x, baud_base %u\n", ioctl_res, sio.flags, sio.baud_base );
	sio.flags = ((sio.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST);
	sio.custom_divisor = sio.baud_base / 200000;
	ioctl_res = ioctl( handle, TIOCSSERIAL, &sio );
	if (ioctl_res < 0)
	{
		sprintf( errmsg, "Failed TIOCSSERIAL ioctl: error %d (%s)\n", errno, strerror(errno) );
		close( handle );
		return -1;
	}
	if (verbose) printf( "ioctl(TIOCSSERIAL) returned %d, new flags %04x, new custom_divisor %u\n", ioctl_res, sio.flags, sio.custom_divisor );

	return handle;
}

void LicutProbe::Close( int handle )
{
	close( handle );
}

