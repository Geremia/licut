// $Id: licut_io.h 5 2011-01-31 03:48:23Z henry_groover $
// Basic delayed I/O - Cricut uses 200kbps 8N2 for output but 8N1 for input,
// so we set 8N1 and add a 1ms intercharacter delay on output

/* 
Response examples:
Send: 04 14 00 00 00 00
Recv: 04 00 01 00 01

Send: 04 12 00 00 00 00
Recv: 06 00 14 00 02 00 22

Send: 04 11 00 00 00 00
Recv: 08 01 3c 00 32 13 62 12 58

Send: 04 18 00 00 00 00
Recv: 26 00 01 00 21 43 72 69 63 75 74 28 52 29 20 43 
      61 6b 65 20 42 61 73 69 63 73 00 00 00 00 00 00 
      00 00 00 00 00 00 23 
      (Cricut(R) Cake Basics)

*/
#include <stdint.h>

class LicutIO
{
public:
	LicutIO( int handle );
	~LicutIO() {};

	// Send command with variable args. Returns bytes written and sets expected reply bytes
	int SendCmd( unsigned char cmd, ... );

	// Specific commands with args specified. These should be followed by ReadCmdReply()
	int SendCmd_StartTransaction( void ); // 0x21: No reply
	int SendCmd_EndTransaction( void ); // 0x22: No reply
	int SendCmd_StatusRequest( unsigned int *cartridge_loaded, unsigned int *mat_loaded ); // 0x14: 4 byte reply, last byte is mat loaded
	int SendCmd_FirmwareVersion( unsigned int ver[3] ); // 0x12: 6 byte reply, 3 big-endian integer components, model number plus major.minor
	int SendCmd_MatBoundaries( unsigned int *x_min, unsigned int *y_min, unsigned int *x_max, unsigned int *y_max ); // 0x11: 8 byte reply, 4 big-endian int components
	int SendCmd_CartridgeName( unsigned int *cartridge_present, char cartridge_name[64], unsigned int *cartridge_version ); // 0x18: 38 byte reply
	int SendCmd_MoveCut( unsigned int subCmd, unsigned int x, unsigned int y ); // 0x40: 4 byte reply
	
	// Read command reply. If none expected returns 0, but waits for minimum intercommand delay
	int ReadCmdReply( int verbose );

	// Low level packet send. Returns bytes sent reported by write()
	int Send( const unsigned char *bytes, int length );

	// Drain whatever is in the receive buffer and display it to stdout as hex (and printable text if found)
	int Drain( int verbose, int ms_timeout = 50 );

	// Convert to and from little-endian unsigned
	static unsigned int leu_to_unsigned( unsigned char const *leu );
	static void unsigned_to_leu( unsigned int u, unsigned char *leu );
	static unsigned int leu32_to_unsigned( unsigned char const *leu );
	static void unsigned_to_leu32( unsigned int u, unsigned char *leu );

	// Convert to and from big-endian unsigned
	static unsigned int beu_to_unsigned( unsigned char const *beu );
	static void unsigned_to_beu( unsigned int u, unsigned char *beu );
	static unsigned int beu32_to_unsigned( unsigned char const *beu );
	static void unsigned_to_beu32( unsigned int u, unsigned char *beu );

	// XXTEA encrypt/decrypt from wikipedia reference implementation
	static void btea(uint32_t *v, int n, uint32_t const k[4]);

	// Get a random big-endian number in the range Cricut expects (10000 - 32767)
	static unsigned int noise();

	// Set starting value for fixed pseudo-noise (linear with no randomness). 0 to use random noise
	static void SetFixedNoiseStart( int n ) { g_fixedNoise = n; }

	// Dump values in hex to stdout
	static void dump_hex( const char *prefix, unsigned char *data, int length, const char *suffix );

	// Default verbosity
	int GetVerbose() const { return m_verbose; }
	void SetVerbose( int level ) { m_verbose = level; }
protected:
	int m_handle;
	int m_expectedReply; // Set by SendCmd - expected bytes in reply
	int m_expectedReplyCmd; // Command from which we're expecting a reply
	// Return value pointers
	unsigned int *m_pCartridgeLoaded; // Set by return from status query (0x14)
	unsigned int *m_pMatLoaded;
	unsigned int *m_pVer;
	unsigned int *m_pXMin;
	unsigned int *m_pYMin;
	unsigned int *m_pXMax;
	unsigned int *m_pYMax;
	unsigned int *m_pCartridgePresent;
	char *m_cartridgeName;
	unsigned int *m_pCartridgeVersion;

	int m_verbose; // Default verbosity
	// If nonzero, this is a fixed pseudo-noise value which increments on each fetch
	static int g_fixedNoise;

	static uint32_t g_cmd_keys[8][4];
};

