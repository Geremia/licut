// $Id: licut_probe.h 1 2011-01-28 21:55:10Z henry_groover $
// Class to handle port probe and init

class LicutProbe
{
public:
	static int Open( int verbose = 0 );
	static void Close( int handle );
	static const char *Errmsg() { return errmsg; }

protected:
	static char errmsg[256];
};

