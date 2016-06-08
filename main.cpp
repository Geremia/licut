// $Id: main.cpp 5 2011-01-31 03:48:23Z henry_groover $
// Main entry point for licut

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <gflags/gflags.h>

#include "licut_probe.h"
#include "licut_io.h"
#include "licut_svg.h"

const char version_str[] = "0.15";

DEFINE_int32( verbose, 0, "Verbose mode" );
DEFINE_int32( eject, 1, "Eject on exit" );
DEFINE_int32( quick, 0, "Skip wait for pressure adjustment" );
DEFINE_int32( intercurve, 10, "Set intercommand delay for bezier curves (in ms)" );
DEFINE_int32( intercmd, 50, "Set intercommand delay for command sets (in ms)" );
DEFINE_int32( noise, 0, "Use fixed noise starting with specified value" );
DEFINE_int32( xxtea_unittest, 0, "Run XXTEA unit test with specified uint32 value" );
DEFINE_string( xxtea_unittest_str, "", "Pass string to XXTEA unit test" );

int main( int argc, char *argv[] )
{
	printf( "licut v%s\n", version_str );

#if 0
	// Parse options
	int verbose = 0;
	int eject = 1;
	int quick = 0;
	int interCurve = 10;
	int interCmd = 50;
#endif
	#define interCurve	FLAGS_intercurve
	#define interCmd	FLAGS_intercmd
	#define verbose		FLAGS_verbose
	#define eject		FLAGS_eject
	#define quick		FLAGS_quick

	int n;
	const char *svgPath = NULL;
	// Rearrange args with options removed
	google::ParseCommandLineFlags( &argc, &argv, true );
	for (n = 1; n < argc; n++)
	{
#if 0
		if (argv[n][0] == '-')
		{
			switch (argv[n][1])
			{
				case 'v':
					verbose++;
					break;
				case 'n':
					eject = 0;
					break;
				case 'q':
					quick  = 1;
					break;
				default:
					printf( "Unknown option %s\n", argv[n] );
				case 'h':
					printf( "Syntax:\n\t%s [options] [svg-file]\n", argv[0] );
					printf( "where [options] is one or more of the following:\n" );
					printf( "	-v	Display verbose progress and debug - repeat for more verbosity\n" );
					printf( "	-n	No eject - do not eject on exit\n" );
					printf( "	-q	Quick mode - do not wait for pressure adjustment\n" );
					printf( "	-h	Show this message\n" );
					printf( "and [svg-file] is an Inkscape svg file. See doc/Using_Inkscape.txt for notes\n" );
					printf( "on creating .svg files for use with licut for cutting.\n" );
					exit( 0 );
			}
		}
		else
#endif
		{
			svgPath = argv[n];
		}
	}

	if (verbose) printf( "Verbose level = %d\n", verbose );
	if (FLAGS_noise) 
	{
		printf( "Setting start value for fixed pseudo (non-random) noise to %d\n", FLAGS_noise );
		//LicutIO::SetFixedNoiseStart( FLAGS_noise );
		//printf( "First value will be %u\n", LicutIO::noise() );
		LicutIO::SetFixedNoiseStart( FLAGS_noise );
	}
	if (FLAGS_xxtea_unittest)
	{
		printf( "Running unit test with input value 0x%08lx\n", FLAGS_xxtea_unittest );
		uint32_t v[3] = {FLAGS_xxtea_unittest,0,0};
		uint32_t k1[] = { 0x272D6C37, 0x342A6173, 0x3663255B, 0x2B265A4D };
		uint32_t k2[] =/*KEY1 -*/{ 0x7D316E22, 0x4A4A7133, 0x5A3C5C5F, 0x78613A61 };
		uint32_t k3[] =/*KEY2 -*/{ 0x47302A23, 0x5D31482F, 0x3B257A61, 0x3671382F };
		LicutIO::btea( v, 3, k1 );
		LicutIO::dump_hex( "Cryptext: ", (unsigned char *)&v[0], 12, "\n" );
		const char *s = FLAGS_xxtea_unittest_str.c_str();
		printf( "Plaintext string: %s\n", s );
		char *d = (char *)&v[0];
		int n;
		for (n = 0; n < 12 && s[n]; n++) d[n] = s[n];
		LicutIO::btea( v, 3, k2 );
		LicutIO::dump_hex( "Cryptext: ", (unsigned char *)&v[0], 12, "\n" );
		unsigned char t3[] = { 0x11, 0x27, 0x00, 0x00, 0xe3, 0x02, 0x00, 0x00, 0x84, 0x01, 0x00, 0x00 };
		memcpy( d, t3, 12 );
		LicutIO::dump_hex( "Plaintext: ", (unsigned char *)&v[0], 12, "\n" );
		LicutIO::btea( v, 3, k3 );
		LicutIO::dump_hex( "Cryptext: ", (unsigned char *)&v[0], 12, "\n" );
		return 0;
	}

	LicutSVG svg( verbose );
	bool hasSvg = false;
	if (svgPath)
	{
		hasSvg = (svg.Parse( svgPath ) == 0);
		printf( "Result of parsing %s = %s\n", svgPath, hasSvg ? "OK" : "failed" );
	}

	int handle = LicutProbe::Open( verbose );
	if (handle <= 0)
	{
		fprintf( stderr, "Failed to open: %s\n", LicutProbe::Errmsg() );
		return -1;
	}

	if (verbose) printf( "Opened handle %d\n", handle );

	LicutIO lio( handle );

	// Drain anything waiting in read buffer
	lio.Drain( verbose, 500 );

	// Get status
	int send_res, reply_res;
	unsigned int cartridgeLoaded = 0;
	unsigned int matLoaded = 0;
	send_res = lio.SendCmd_StatusRequest( &cartridgeLoaded, &matLoaded );
	reply_res = lio.ReadCmdReply( verbose );
	printf( "Mat is %sloaded, cartridge %spresent\n", matLoaded ? "" : "not ", cartridgeLoaded ? "" : "not " );

	// Get model and firmware version
	unsigned int version_data[3];
	send_res = lio.SendCmd_FirmwareVersion( version_data );
	reply_res = lio.ReadCmdReply( verbose );
	printf( "Model #%u, firmware ver %u.%u\n", version_data[0], version_data[1], version_data[2] );

	// Get cartridge name
	char cartridgeName[256];
	unsigned int cartridgePresent, cartridgeVersion;
	send_res = lio.SendCmd_CartridgeName( &cartridgePresent, cartridgeName, &cartridgeVersion );
	reply_res = lio.ReadCmdReply( verbose );
	printf( "Cartridge present: %u", cartridgePresent );
	if (cartridgePresent) printf( " rev:%u name:%s", cartridgeVersion, cartridgeName );
	printf( "\n" );

	// Wait until mat loaded
	bool wasLoaded = matLoaded;
	while (!matLoaded)
	{
		send_res = lio.SendCmd_StatusRequest( &cartridgeLoaded, &matLoaded );
		reply_res = lio.ReadCmdReply( verbose );
		if (matLoaded) break;
		printf( "\nMat not loaded, insert and press 'Load mat' key:" );
		sleep( 5 );
	}

	printf( "\nMat loaded, getting boundaries...\n" );

	unsigned int XMin, YMin, XMax, YMax;
	send_res = lio.SendCmd_MatBoundaries( &XMin, &YMin, &XMax, &YMax );
	reply_res = lio.ReadCmdReply( verbose );
	printf( "Mat boundaries: (%u,%u) to (%u,%u)\n", XMin, YMin, XMax, YMax );

	// If we just loaded, allow operator to set pressure
	if (!wasLoaded && !quick)
	{
		printf( "\nSet pressure via bottom wheel:" );
		fflush( stdout );
		sleep( 15 );
		printf( " ...continuing" );
	}

	if (hasSvg && svg.GetDrawSetCount() > 0)
	{
		svg.SetIntercurveDelay( interCurve );
		svg.SetIntercommandDelay( interCmd );
		printf( "\nCutting %d draw sets from svg file with inter-command delay of %dms...\n", svg.GetDrawSetCount(), svg.GetIntercommandDelay() );
		int r = svg.CutAllDrawSets( lio, XMin, YMin, XMax - XMin, YMax - YMin );
		printf( "CutAllDrawSets() returned %d\n", r );
	}

	if (eject)
	{
		printf( "Ejecting...\n" );

		// Move to 0, 0, effectively ejecting
		send_res = lio.SendCmd_MoveCut( 2, 0, 0 );
		reply_res = lio.ReadCmdReply( verbose );
	}

	printf( "Draining final responses from device...\n" );
	lio.Drain( verbose, 1000 );

	if (verbose) printf( "Closing handle %d\n", handle );
	LicutProbe::Close( handle );
	if (verbose) printf( "Handle %d closed, exiting...\n", handle );

	return 0;
}

