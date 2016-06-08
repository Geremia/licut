// $Id: licut_io.cpp 5 2011-01-31 03:48:23Z henry_groover $

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>

#include "licut_io.h"

uint32_t LicutIO::g_cmd_keys[8][4] = {
/*KEY0 -*/{ 0x272D6C37, 0x342A6173, 0x3663255B, 0x2B265A4D },
/*KEY1 -*/{ 0x7D316E22, 0x4A4A7133, 0x5A3C5C5F, 0x78613A61 },
/*KEY2 -*/{ 0x47302A23, 0x5D31482F, 0x3B257A61, 0x3671382F },
/*KEY3 -*/{ 0x303F6863, 0x71646D30, 0x4769457B, 0x6D342569 },
/*KEY4 -*/{ 0x45356650, 0x3A386D69, 0x575A7037, 0x335F357D },
/*KEY5 -*/{ 0x343A2148, 0x614F3925, 0x753F6953, 0x47463626 },
/*KEY6 -*/{ 0x3F62626D, 0x7E555F44, 0x7E29425A, 0x52246268 },
/*KEY7 -*/{ 0x47302A23, 0x342A6173, 0x4769457B, 0x335F357D }
};

// If nonzero, this is a fixed pseudo-noise value which increments on each fetch
int LicutIO::g_fixedNoise = 0;

LicutIO::LicutIO( int handle )
{
	m_handle = handle;
	m_expectedReply = 0;
	m_expectedReplyCmd = 0;
	m_verbose = 0;
}

int LicutIO::Send(  const unsigned char *bytes, int length )
{
	int n;
	int actual_sent = 0;
	for (n = 0; n < length; n++)
	{
		int write_res = write( m_handle, &bytes[n], 1 );
		if (write_res < 1)
		{
			printf( "%s(%p,%u) - write returned %d, errno=%d (%s)\n", __FUNCTION__, bytes, length, write_res, errno, strerror(errno) );
		}
		else
		{
			actual_sent++;
		}
		// Add intercharacter delay after each character, including the last
		usleep( 1000 );
	}
	return actual_sent;
}

int LicutIO::Drain( int verbose, int ms_timeout /* = 50 */ )
{
	unsigned char binbuf[256];
	// Wait 50 ms for data ready
	fd_set rfds;
	struct timeval tv;
	FD_ZERO( &rfds );
	FD_SET( m_handle, &rfds );
	tv.tv_sec = 0;
	tv.tv_usec = ms_timeout * 1000;
	int res = 0;
	if (select( m_handle+1, &rfds, NULL, NULL, &tv ) > 0)
	{
		res = read( m_handle, binbuf, sizeof(binbuf)-1 );
	}

	if (res == 0) return res;

	if (verbose > 0) printf( "%s() read %d characters\n", __FUNCTION__, res );

	int n;
	char *ascii_run = NULL;
	int ascii_run_length = 0;
	int in_ascii_run = 0;
	for (n = 0; n < res; n++)
	{
		if (verbose >= 0)
		{
			if (n == 0) printf( "[%02x", binbuf[n] );
			else printf( " %02x", binbuf[n] );
		}
		if (in_ascii_run)
		{
			if (binbuf[n] < ' ' || binbuf[n] > 126)
			{
				in_ascii_run = 0;
			}
			else
			{
				ascii_run_length++;
			}
		}
		else if (ascii_run_length < 4 && binbuf[n] >= ' ' && binbuf[n] <= 126)
		{
			in_ascii_run = 1;
			ascii_run_length = 1;
			ascii_run = (char *)&binbuf[n];
		}

	}
	if (ascii_run_length > 0)
	{
		ascii_run[ascii_run_length] = '\0';
		if (verbose >= 0) printf( "]   %s\n", ascii_run );
	}
	else
	{
		if (verbose >= 0) printf( "]\n" );
	}

	return res;
}

int LicutIO::SendCmd( unsigned char cmd, ... )
{
	// Try to force xxtea operation to take place on dword-aligned memory
	// ARM processors are picky about this, and require qword-aligned memory
	// for strd operations (used with doubles). But we cannot do 32-bit operations
	// on memory which is not dword aligned...
	unsigned char sendBuffer[64];
	int sendBuffStartOff = 2;
	int sendBuffDataOff = 4;
	unsigned char dataLength = 4;
	int packetLength = 5;

	va_list arglist;
	va_start( arglist, cmd );
	unsigned int subCmd;
	unsigned int x, y;
	unsigned int n;

	m_expectedReply = 0;
	m_expectedReplyCmd = cmd;

	memset( sendBuffer, 0, sizeof(sendBuffer) );

	switch (cmd)
	{
		// subtype, x and y int32 data sent, needs noise and XXTEA encryption
		case 0x40:
			dataLength = 13; // Includes cmd
			packetLength = dataLength + 1;
			m_expectedReply = 1;
			subCmd = va_arg( arglist, unsigned int );
			x = va_arg( arglist, unsigned int );
			y = va_arg( arglist, unsigned int );
			n = noise();
			if (m_verbose > 0) printf( "%s(%u,%u,%u,%u) using noise %u (fixed=%d)\n", __FUNCTION__, cmd, subCmd, x, y, n, g_fixedNoise );
			// Assemble datablock
			if (subCmd > 7)
			{
				printf( "Invalid subcmd %d\n", subCmd );
				break;
			}
			// Works only on x86
			/****
			*((unsigned int *)&sendBuffer[2 + 0 * sizeof(int)]) = n;
			*((unsigned int *)&sendBuffer[2 + 1 * sizeof(int)]) = x;
			*((unsigned int *)&sendBuffer[2 + 2 * sizeof(int)]) = y;
			****/
			unsigned_to_leu32( n, &sendBuffer[sendBuffDataOff + 0 * sizeof(int)] );
			unsigned_to_leu32( x, &sendBuffer[sendBuffDataOff + 1 * sizeof(int)] );
			unsigned_to_leu32( y, &sendBuffer[sendBuffDataOff + 2 * sizeof(int)] );
			if (m_verbose > 0) dump_hex( "Plaintext: ", &sendBuffer[2], 12, "\n" );
#if 0 && defined( __arm__ )
			// Put bytes to be encrypted in big-endian order
			unsigned_to_beu32( n, &sendBuffer[2 + 0 * sizeof(int)] );
			unsigned_to_beu32( x, &sendBuffer[2 + 1 * sizeof(int)] );
			unsigned_to_beu32( y, &sendBuffer[2 + 2 * sizeof(int)] );
			if (m_verbose > 0) dump_hex( "Plaintext_BE: ", &sendBuffer[2], 12, "\n" );
#endif
			// Encrypt using appropriate key
			btea( (unsigned int *)&sendBuffer[sendBuffDataOff], 3, g_cmd_keys[subCmd] );
			if (m_verbose > 0)
			{
				printf( "k[%d]; ", subCmd );
				dump_hex( "Cryptext:  ", &sendBuffer[sendBuffDataOff], 12, "\n" );
			}
			break;
		// No data sent, length-prefixed reply expected
		case 0x18:
		case 0x11:
		case 0x12:
		case 0x14:
			m_expectedReply = 1;
			break;
		// No data sent, no reply
		case 0x21:
		case 0x22:
			break;
	}

	va_end( arglist );

	sendBuffer[sendBuffStartOff+0] = dataLength;
	sendBuffer[sendBuffStartOff+1] = cmd;

	return Send( &sendBuffer[sendBuffStartOff], packetLength );
}

int LicutIO::SendCmd_StartTransaction( void ) // 0x21: No reply
{
	return SendCmd( 0x21 );
}

int LicutIO::SendCmd_EndTransaction( void ) // 0x22: No reply
{
	return SendCmd( 0x22 );
}

int LicutIO::SendCmd_StatusRequest( unsigned int *cartridge_loaded, unsigned int *mat_loaded ) // 0x14: 4 byte reply, last byte is mat loaded
{
	m_pCartridgeLoaded = cartridge_loaded;
	m_pMatLoaded = mat_loaded;
	return SendCmd( 0x14 );
}

int LicutIO::SendCmd_FirmwareVersion( unsigned int ver[3] ) // 0x12: 6 byte reply, 3 big-endian integer components, model number plus major.minor
{
	m_pVer = &ver[0];
	return SendCmd( 0x12 );
}

int LicutIO::SendCmd_MatBoundaries( unsigned int *x_min, unsigned int *y_min, unsigned int *x_max, unsigned int *y_max ) // 0x11: 8 byte reply, 4 big-endian int components
{
	m_pXMin = x_min;
	m_pYMin = y_min;
	m_pXMax = x_max;
	m_pYMax = y_max;
	return SendCmd( 0x11 );
}

int LicutIO::SendCmd_CartridgeName( unsigned int *cartridge_present, char cartridge_name[64], unsigned int *cartridge_version )
{
	m_pCartridgePresent = cartridge_present;
	m_cartridgeName = cartridge_name;
	m_pCartridgeVersion = cartridge_version;
	return SendCmd( 0x18 );
}

int LicutIO::SendCmd_MoveCut( unsigned int subCmd, unsigned int x, unsigned int y ) // 0x40: 4 byte reply
{
	return SendCmd( 0x40, subCmd, x, y );
}

// Read command reply. If none expected returns 0, but waits for minimum intercommand delay
int LicutIO::ReadCmdReply( int verbose )
{
	int retValue = 0;
	if (m_expectedReply > 0)
	{
		unsigned char binbuf[256];
		// Read length byte
		if (verbose > 0) printf( "%s() reading length byte...\n", __FUNCTION__ );
		int res = read( m_handle, binbuf, 1 );
		if (res < 1)
		{
			printf( "%s() expected %d length bytes from cmd %x, got %d (errno=%d - %s)\n",
				__FUNCTION__, m_expectedReply, m_expectedReplyCmd, res, errno, strerror(errno) );
			retValue = -1;
		}
		else
		{
			int bytesToRead = binbuf[0];
			if (verbose > 0) printf( "%s() got length byte %d\n", __FUNCTION__, bytesToRead );
			if (bytesToRead > sizeof(binbuf))
			{
				printf( "%s() WARNING: truncating bytes to read from %d to %d\n",
					__FUNCTION__, bytesToRead, sizeof(binbuf) );
				bytesToRead = sizeof(binbuf);
			}
			res = read( m_handle, binbuf, bytesToRead );
			if (res < bytesToRead || bytesToRead < 1)
			{
				printf( "%s() expected %d bytes from cmd %x, got %d (errno=%d - %s)\n",
					__FUNCTION__, m_expectedReply, m_expectedReplyCmd, res, errno, strerror(errno) );
				retValue = -1;
			}
			else
			{
				retValue = 1;
				unsigned offsetValue;
				if (verbose > 0)
				{
					printf( "{" );
					for (int n = 0; n < bytesToRead; n++)
					{
						printf( "%s%02x", n ? ", " : "", binbuf[n] );
					}
					printf( "}\n" );
				}
				switch (m_expectedReplyCmd)
				{
					case 0x11: // Mat boundaries
						*m_pXMin = beu_to_unsigned( &binbuf[0] );
						*m_pYMin = beu_to_unsigned( &binbuf[2] );
						*m_pXMax = beu_to_unsigned( &binbuf[4] );
						*m_pYMax = beu_to_unsigned( &binbuf[6] );
						break;
					case 0x12: // Model and firmware version
						//if (verbose > 0) printf( "got %02x %02x = %u\n", binbuf[1], binbuf[2], beu_to_unsigned( &binbuf[1] ) );
						m_pVer[0] = beu_to_unsigned( &binbuf[0] );
						m_pVer[1] = beu_to_unsigned( &binbuf[2] );
						m_pVer[2] = beu_to_unsigned( &binbuf[4] );
						break;
					case 0x14: // Status
						*m_pCartridgeLoaded = beu_to_unsigned( &binbuf[0] );
						*m_pMatLoaded = beu_to_unsigned( &binbuf[2] );
						break;
					case 0x18: // Cartridge name / status / rev
						*m_pCartridgePresent = beu_to_unsigned( &binbuf[0] );
						offsetValue = beu_to_unsigned( &binbuf[2] );
						if (offsetValue > sizeof(binbuf))
						{
							printf( "Error: got invalid offset value %u\n", offsetValue );
							*m_pCartridgeVersion = 0;
							strcpy( m_cartridgeName, "ERROR" );
						}
						else
						{
							*m_pCartridgeVersion = binbuf[4 + offsetValue];
							// Name seems to be always null-terminated
							strncpy( m_cartridgeName, (char *)&binbuf[4], offsetValue );
						}
						break;
				}
			}
		}
	}
	// Drain for minimum 250ms
	Drain( verbose - 1, 250 );
	return retValue;
}

unsigned int LicutIO::beu_to_unsigned( unsigned char const *beu )
{
	return (beu[0] << 8) | beu[1];
}

void LicutIO::unsigned_to_beu( unsigned int u, unsigned char *beu )
{
	beu[0] = ((u & 0xff00) >> 8);
	beu[1] = (u & 0xff);
}

unsigned int LicutIO::beu32_to_unsigned( unsigned char const *beu )
{
	return (beu[0] << 24) | (beu[1] << 16) | (beu[2] << 8) | beu[3];
}

void LicutIO::unsigned_to_beu32( unsigned int u, unsigned char *beu )
{
	beu[0] = ((u & 0xff000000) >> 24);
	beu[1] = ((u & 0x00ff0000) >> 16);
	beu[2] = ((u & 0x0000ff00) >> 8);
	beu[3] = (u & 0xff);
}

unsigned int LicutIO::leu_to_unsigned( unsigned char const *leu )
{
	return (leu[1] << 8) | leu[0];
}

void LicutIO::unsigned_to_leu( unsigned int u, unsigned char *leu )
{
	leu[1] = ((u & 0xff00) >> 8);
	leu[0] = (u & 0xff);
}

unsigned int LicutIO::leu32_to_unsigned( unsigned char const *leu )
{
	return (leu[3] << 24) | (leu[2] << 16) | (leu[1] << 8) | leu[0];
}

void LicutIO::unsigned_to_leu32( unsigned int u, unsigned char *leu )
{
	leu[3] = ((u & 0xff000000) >> 24);
	leu[2] = ((u & 0x00ff0000) >> 16);
	leu[1] = ((u & 0x0000ff00) >> 8);
	leu[0] = (u & 0xff);
}

// Get a random big-endian number in the range Cricut expects (10000 - 32767)
unsigned int LicutIO::noise()
{
#define RANGE_BASE	10001
#define RANGE_TOP	32766
#define RANGE_SIZE	(RANGE_TOP - RANGE_BASE)
	unsigned short udata;
	static int serial = 0x44;
	serial += 19;
	udata = (unsigned short)serial;
	if (g_fixedNoise)
	{
		udata = (g_fixedNoise - RANGE_BASE);
		g_fixedNoise++;
	}
	else
	{
		int rh = open( "/dev/urandom", O_RDONLY );
		if (rh > 0)
		{
			read( rh, &udata, sizeof(udata) );
			close( rh );
		}
	}
	return RANGE_BASE + (udata % RANGE_SIZE);
}

// Taken from wikipedia reference implementation, made into a static member function
#define DELTA 0x9e3779b9
#define MX (((z>>5^y<<2) + (y>>3^z<<4)) ^ ((sum^y) + (k[(p&3)^e] ^ z)))
 
void LicutIO::btea(uint32_t *v, int n, uint32_t const k[4]) 
{
    uint32_t y, z, sum;
    unsigned p, rounds, e;
    if (n > 1) {          /* Coding Part */
      rounds = 6 + 52/n;
      sum = 0;
      z = v[n-1];
      do {
        sum += DELTA;
        e = (sum >> 2) & 3;
        for (p=0; p<n-1; p++) {
          y = v[p+1]; 
          z = v[p] += MX;
        }
        y = v[0];
        z = v[n-1] += MX;
      } while (--rounds);
    } else if (n < -1) {  /* Decoding Part */
      n = -n;
      rounds = 6 + 52/n;
      sum = rounds*DELTA;
      y = v[0];
      do {
        e = (sum >> 2) & 3;
        for (p=n-1; p>0; p--) {
          z = v[p-1];
          y = v[p] -= MX;
        }
        z = v[n-1];
        y = v[0] -= MX;
      } while ((sum -= DELTA) != 0);
    }
}

// Dump values in hex to stdout
void LicutIO::dump_hex( const char *prefix, unsigned char *data, int length, const char *suffix )
{
	if (prefix) printf( "%s", prefix );
	int n;
	for (n = 0; n < length; n++)
	{
		printf( "%s%02x", n ? ", " : "", data[n] );
	}
	if (suffix) printf( "%s", suffix );
}

