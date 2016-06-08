// $Id: licut_svg.cpp 1 2011-01-28 21:55:10Z henry_groover $
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <alloca.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "licut_svg.h"
#include "licut_io.h"

#define MAX_DRAWSETS	64

LicutSVG::LicutSVG( int verbose /* = 0*/)
{
	m_width = 0;
	m_height = 0;
	m_drawSetCount = 0;
	m_drawSets = (drawSet_t**)malloc( MAX_DRAWSETS * sizeof(drawSet_t*) );
	memset( m_drawSets, 0, MAX_DRAWSETS * sizeof(drawSet_t*) );
	m_verbose = verbose;
	m_intercommand = 100; // 100ms between commands (in addition to waiting for reply)
	m_intercurve = 5; // 5ms betwen elements of a Bezier curve set
}

LicutSVG::~LicutSVG()
{
	int n;
	if (m_drawSets != NULL)
	  for (n = 0; n < m_drawSetCount; n++)
	  {
		if (m_drawSets[n] == NULL) break;
		free( m_drawSets[n] );
	  }
	if (m_drawSets != NULL)
	{
		free( m_drawSets );
		m_drawSets = NULL;
	}
}

// Dump without control characters for debugging
static const char *_fmt_sample( char *s, int length )
{
	static char buff[256];
	int n;
	buff[0] = '\0';
	int buffLen = 0;
	for (n = 0; n < length; n++)
	{
		buffLen = strlen( buff );
		switch (s[n])
		{
			case '\n':
				strcpy( &buff[buffLen], "{lf}" );
				break;
			case '\r':
				strcpy( &buff[buffLen], "{cr}" );
				break;
			case '\t':
				strcpy( &buff[buffLen], "{ht}" );
				break;
			case '\0':
				strcpy( &buff[buffLen], "{NUL}" );
				break;
			default:
				if (s[n] >= ' ' && s[n] < 127)
				{
					buff[buffLen++] = s[n];
					buff[buffLen] = '\0';
				}
				else
				{
					sprintf( &buff[buffLen], "{0x%02x}", s[n] );
				}
				break;
		}
	}
	return buff;
}

// Parse file - returns 0 if successful
int LicutSVG::Parse( const char *svgPath )
{
	FILE *f = fopen( svgPath, "r" );
	if (!f)
	{
		printf( "Failed to open %s (errno=%d: %s)\n", svgPath, errno, strerror(errno) );
		return -1;
	}
	struct stat fileInfo;
	if (0 != fstat( fileno(f), &fileInfo ))
	{
		printf( "Failed to get size of %s (errno=%d: %s)\n", svgPath, errno, strerror(errno) );
		fclose( f );
		return -1;
	}
	// If size is greater than the process stack limit we'll get a stack overflow fault
	// here. Use malloc / free if over 1mb *FIXME*
	if (fileInfo.st_size < 1  || fileInfo.st_size > 0x10000)
	{
		printf( "Invalid file size %lu for %s - must be > 0 and <= 1mb\n", fileInfo.st_size, svgPath );
		fclose( f );
		return -1;
	}
	char *data = (char *)alloca( fileInfo.st_size + 1 );
	size_t bytesRead = fread( data, sizeof(char), fileInfo.st_size, f );
	if (bytesRead < fileInfo.st_size)
	{
		printf( "Failed to read contents of %s (%lu requested, %d read, errno=%d: %s)\n",
			svgPath, fileInfo.st_size, bytesRead, errno, strerror(errno) );
		fclose( f );
		return -1;
	}

	fclose( f );

	// NULL-terminate it
	data[bytesRead] = '\0';

	int success = -1;

	/****
	Simple state machine parser
	This is a simplistic, tiny xml parser which doesn't handle CDATA or escapes
	It operates destructively on data, parsing recursively from the outside in
	and setting the end of each tag (</{tagname}> or /> or --> or ?>) to '\0'
	Also null-terminates the name of each tag
	Limited to tag identification for 1024 levels
	****/
	char * tagStack[1024];
	tagStack[0] = (char *)"";
	int stackLevel = 0;
	char * tagData = data;

	if (ParseTags( tagData, stackLevel, tagStack, false ) >= 1)
	{
		success = 0;
	}
	else
	{
		printf( "Did not parse any tags!\n" );
	}

	return success;
}

// Parse node tags at the current level recursively. See notes above
// Return number of node tags parsed
int LicutSVG::ParseTags( char *& s, int stackLevel, char *tagStack[1024], bool ignore )
{
	int tagsParsed = 0;
	while (*s)
	{
		if (m_verbose) printf( "calling [%s]\n", _fmt_sample( s, 6 ) );
		int parsed = ParseTag( s, stackLevel, tagStack, ignore );
		if (m_verbose) printf( "returned %d\n", parsed );
		tagsParsed += parsed;
	}
	return tagsParsed;
}

// Parse a single node tag recursively at the current level
// Returns 1 if parsed or 0 if not a tag
int LicutSVG::ParseTag( char *& s, int stackLevel, char *tagStack[1024], bool ignore )
{
	if (!*s) return 0;
	// Skip leading whitespace
	s += strspn( s, " \t\r\n" );
	if (!*s) return 0;
	// Expected: <tag, <!-- or <?
	if (s[0] != '<')
	{
		printf( "%s() level %d - unexpected character %c\n", __FUNCTION__, stackLevel, *s );
		if (s[0]) s++;
		return 0;
	}

	int gotTag = 0;

	// Ignore comments
	if (s[1] == '!' && s[2] == '-' && s[3] == '-')
	{
		if (m_verbose) printf( "Skipping comment [%s]\n", _fmt_sample( s, 6 ) );
		char *end = strstr( s, "-->" );
		if (end)
		{
			s = &end[3];
		}
		else
		{
			s += strlen( s );
		}
	}
	// Ignore directives
	else if (s[1] == '?')
	{
		char *end = strstr( s, "?>" );
		if (end)
		{
			if (m_verbose)
			{
				printf( "Got directive [%s]...", _fmt_sample( s, 6 ) );
				printf( "%s\n", _fmt_sample( &end[-4], 5 ) );
			}
			s = &end[2];
		}
		else
		{
			printf( "Unexpected missing end for [%s]\n", _fmt_sample( s, 4 ) );
			s += strlen( s );
		}
	}
	else
	{
		gotTag = 1;
		// Get tag name
		char *tagName = &s[1];
		tagName += strspn( tagName, " \t\r\n" );
		// Name ends in whitespace or end of tag
		s = &tagName[strcspn( tagName, "/> \t\r\n" )];
		bool foundEnd = false;
		bool closedTag = false; // tag ends with />
		char *tagEnd;
		char *tagNext = NULL;
		if (*s == '/')
		{
			foundEnd = true;
			closedTag = true;
			tagEnd = &s[1];
			tagEnd += strspn( tagEnd, " \t\r\n" );
			tagNext = &tagEnd[1];
		}
		else if (*s == '>')
		{
			foundEnd = true;
			tagEnd = s;
			tagNext = &tagEnd[1];
		}
		// Terminate name
		*s = '\0';
		if (m_verbose) printf( "Tag: %s end:%c closed:%c\n", tagName, foundEnd?'Y' : 'n', closedTag ? 'Y' : 'n' );
		// Find end of tag
		if (!foundEnd)
		{
			s++;
			int attrCount = 0;
			int attrWithValueCount = 0;
			if (m_verbose) printf( "Searching for end starting %s\n", _fmt_sample( s, 4 ) );
			while (*s != '/' && *s != '>' && *s)
			{
				// Parse attribute [ = "value" ]
				s += strspn( s, " \t\r\n" );
				if (*s == '/' || *s == '>') break;
				char *attrName = s;
				int attrNameLength = strcspn( attrName, "= \t\r\n" );
				s += attrNameLength;
				s += strspn( s, " \t\r\n" );
				attrCount++;
				char *attrValue = NULL;
				// Check for value
				if (*s == '=')
				{
					attrWithValueCount++;
					s++;
					s += strspn( s, " \t\r\n" );
					// Unquoted values are not kosher but we'll parse them anyway
					if (*s == '"')
					{
						s++;
						attrValue = s;
						s += strcspn( s, "\"" );
						*s = '\0';
						s++;
					}
					else
					{
						attrValue = s;
						s += strcspn( s, " \t\r\n" );
						*s = '\0';
						s++;				
					}
				}
			
				attrName[attrNameLength] = '\0';

				if (m_verbose) printf( "  [%d] %s=%s\n", attrCount, attrName, attrValue );
				if (!strcmp( tagName, "svg" ))
				{
					if (!strcmp( attrName, "width" ))
					{
						printf( "svg width=%s\n", attrValue );
						m_width = atoi( attrValue );
					}
					else if (!strcmp( attrName, "height" ))
					{
						printf( "svg height=%s\n", attrValue );
						m_height = atoi( attrValue );
					}
				}
				if (!ignore && !strcmp( tagName, "g" ))
				{
					if (!strcmp( attrName, "id" ) && attrValue != NULL)
					{
						// FIXME if we wanted to use some layers as guide, the id or label could be 
						// configurable
						printf( "layer id=%s\n", attrValue );
					}
				}
				if (!ignore && !strcmp( tagName, "path" ))
				{
					if (!strcmp( attrName, "d" ) && attrValue != NULL)
					{
						int setsParsed = ParseDrawList( attrValue );
						printf( "path d len=%d sets=%d\n", strlen(attrValue), setsParsed );
					}
				}
			}
			if (m_verbose) printf( "Exited attr parse [%s]\n", _fmt_sample( s, 4 ) );
			// Complain if end not found
			if (*s == '>')
			{
				if (m_verbose) printf( "Got end > [%s]\n", _fmt_sample( s, 4 ) );
				foundEnd = true;
				tagNext = &s[1];
			}
			else if (*s == '/')
			{
				if (m_verbose) printf( "Got end /> [%s]\n", _fmt_sample( s, 4 ) );
				foundEnd = true;
				closedTag = true;
				tagNext = &s[1];
				tagNext += strspn( tagNext, " \t\r\n" );
				s = tagNext;
				tagNext++;
			}
			else
			{
				printf( "Error: tag %s is not properly closed, ends with [%s]\n", tagName, _fmt_sample( s, 4 ) );
				if (!*s) s++;
			}
		}
		// Recurse into container tag
		if (foundEnd && !closedTag)
		{
			// Skip > from end of tag opening
			// If we have a tag with no attributes, we may already have NUL
			if (*s == '>' || *s == '\0') s++;
			// Skip quoted values and find </tagname>
			if (m_verbose) printf( "Searching for end of tag %s in %s\n", tagName, _fmt_sample( s, 8 ) );
			char *containerStart = s;
			char *containerEnd = s;
			bool foundContainerEnd = false;
			while (!foundContainerEnd && containerEnd != NULL)
			{
				containerEnd = strpbrk( containerEnd, "\"<" );
				if (!containerEnd) break;
				if (*containerEnd == '"')
				{
					containerEnd = strchr( &containerEnd[1], '"' );
					if (containerEnd == NULL) break;
					containerEnd++;
				}
				else
				{
					char *containerEndTagStart = containerEnd;
					containerEnd++;
					if (*containerEnd == '/')
					{
						containerEnd++;
						int closeLength = strcspn( containerEnd, "> \t\r\n" );
						if (closeLength == strlen( tagName ) && !strncmp( containerEnd, tagName, closeLength ))
						{
							foundEnd = true;
							foundContainerEnd = true;
							tagNext = &containerEnd[closeLength];
							tagNext += strspn( tagNext, " \t\r\n" );
							// Skip final >
							tagNext++;
							*containerEndTagStart = '\0';
						}
					}
				}
			}
			if (foundContainerEnd)
			{
				// containerStart is null-terminated
				if (stackLevel >= 1023)
				{
					printf( "Error: cannot parse %s at level %d - stack overflow\n", tagName, stackLevel );
				}
				else
				{
					// If container starts with something other than <, treat it as an immediate value
					char *valueCandidate = containerStart;
					valueCandidate += strspn( valueCandidate, " \t\r\n" );
					if (*valueCandidate != '<')
					{
						if (m_verbose) printf( "Got tag enclosed value [%s]\n", valueCandidate );
					}
					else
					{
						stackLevel++;
						tagStack[stackLevel] = tagName;
						if (m_verbose) printf( "Entering level %d container tag %s [%s]\n", stackLevel, tagName, _fmt_sample( containerStart, 4 ) );
						ParseTags( containerStart, stackLevel, tagStack, ignore );
						stackLevel--;
						if (m_verbose) printf( "Returned to level %d tag %s\n", stackLevel, tagName );
					}
				}
			}
			else
			{
				printf( "Did not find container end\n" );
			}
		} // Found container tag
		// Skip to next tag
		if (tagNext != NULL)
		{
			s = tagNext;
			if (m_verbose) 
			{
				printf( "Ending parse with [%s] - ", _fmt_sample( &s[-4], 4 ) );
				printf( "next [%s]\n", _fmt_sample( s, 4 ) );
			}
		}
		// else parsing error occurred
		else
		{
			printf( "Parsing error occurred, no next tag\n" );
		}
	}

	// Skip trailing whitespace
	if (*s) s += strspn( s, " \t\r\n" );

	if (m_verbose) printf( "exiting %s() returning %d\n", __FUNCTION__, gotTag );

	return gotTag;
}

// Get draw set or NULL if undefined
drawSet_t const *LicutSVG::GetDrawSet( int index ) const
{
	if (index < m_drawSetCount && m_drawSets != NULL)
	{
		return m_drawSets[index];
	}
	return NULL;
}

// Parse draw list set values from d attribute
// Return number of sets parsed
int LicutSVG::ParseDrawList( char *s )
{
	if (m_drawSetCount >= MAX_DRAWSETS)
	{
		printf( "Maximum draw sets (%d) exceeded - discarding draw set\n", MAX_DRAWSETS );
		return 0;
	}
	int index = m_drawSetCount;
	int dataLength = strlen( s );

	// Estimate draw commands in this set assuming there are at least 10 characters used per command:
	// M 0.0,0.0
	// Take 150% of that number and add 32 in case we have a low length
	int estimatedCommands = (dataLength + dataLength / 2) / 10 + 32;
	int addedCommands = 0;
	drawSet_t *t = (drawSet_t*)alloca( estimatedCommands * sizeof( drawSet_t ) );

	// Parse as
	// type x,y
	// If type=C, also parse two additional x,y pairs
	// Type examples:
	// M 724.743,169.50181 
	// L 582.13986,169.50181 
	// C 597.15048,245.39021 605.13245,240.38659 606.08575,239.90988 
	// Inkscape doesn't seem to use relative values
	// Oh ya, and sometimes the pairs are space-separated rather than , - go figure
	int offset = 0;
	const char *pair1Fmt = "%c %lf,%lf%n";
	const char *pair2Fmt = " %lf,%lf%n";
	if (!strchr( s, ',' ))
	{
		printf( "Warning: Pairs are space-delimited, not comma-delimited\n" );
		pair1Fmt = "%c %lf %lf%n";
		pair2Fmt = " %lf %lf%n";
	}
	while (addedCommands < estimatedCommands && offset < dataLength)
	{
		int endPos;
		if (sscanf( &s[offset], pair1Fmt, &t[addedCommands].type, &t[addedCommands].pt[0][0], &t[addedCommands].pt[0][1], &endPos ) < 3)
		{
			printf( "%s() error - got fewer elements than expected at offset %d (command #%d)\n",
				__FUNCTION__, offset, addedCommands );
			break;
		}
		t[addedCommands].numPoints = 1;
		offset += endPos;

		// Scan two additional sets of points for C
		if (t[addedCommands].type == 'c' || t[addedCommands].type == 'C')
		{
			if (sscanf( &s[offset], pair2Fmt, &t[addedCommands].pt[1][0], &t[addedCommands].pt[1][1], &endPos ) < 2)
			{
				printf( "%s() error - unexpected short read of C control2 offset %d cmd #%d\n",
					__FUNCTION__, offset, addedCommands );
				break;
			}
			offset += endPos;

			if (sscanf( &s[offset], pair2Fmt, &t[addedCommands].pt[2][0], &t[addedCommands].pt[2][1], &endPos ) < 2)
			{
				printf( "%s() error - unexpected short read of C point offset %d cmd #%d\n",
					__FUNCTION__, offset, addedCommands );
				break;
			}
			offset += endPos;

			t[addedCommands].numPoints += 2;
		}

		addedCommands++;

		// Skip trailing whitespace
		offset += strspn( &s[offset], " \t\r\n" );

		// Skip closepath
		if (s[offset] == 'z')
		{
			offset++;
			offset += strspn( &s[offset], " \t\r\n" );
		}
	}

	if (addedCommands >= estimatedCommands)
	{
		printf( "Exceeded estimate %d, truncating...\n", estimatedCommands );
		addedCommands = estimatedCommands - 1;
	}

	if (addedCommands == 0)
	{
		printf( "Empty chain\n" );
		return 0;
	}

	// Allocate actual array
	m_drawSets[m_drawSetCount] = (drawSet_t*)malloc( (addedCommands + 1) * sizeof( drawSet_t ) );

	// Copy
	for (int n = 0; n < addedCommands; n++)
	{
		m_drawSets[m_drawSetCount][n] = t[n];
		if (m_verbose)
		{
			printf( "draw[%d]={%c, %d, %.5f,%.5f", n, t[n].type, t[n].numPoints, t[n].pt[0][0], t[n].pt[0][1] );
			for (int i = 1; i < t[n].numPoints; i++)
			{
				printf( " %.5f,%.5f", t[n].pt[i][0], t[n].pt[i][1] );
			}
			printf( "}\n" );
		}
	}

	// Null-terminate
	m_drawSets[m_drawSetCount][addedCommands].type = 0;
	m_drawSets[m_drawSetCount][addedCommands].numPoints = 0;

	// Update draw set count
	m_drawSetCount++;

	// Return number added (not including terminator)
	return addedCommands;
}

// Cut a single draw set
int LicutSVG::CutDrawSet( LicutIO& lio, int set, int x, int y, int width, int height )
{
	if (set < 0 || set >= m_drawSetCount) return -1;
	SetScaling( x, y, width, height );
	int oldVerbose = lio.GetVerbose();
	lio.SetVerbose( m_verbose );
	int n;
	int send_res, reply_res;
	unsigned int lastX, lastY, curX, curY, ctl1X, ctl1Y, ctl2X, ctl2Y;
	lastX = x;
	lastY = y;
	lio.Drain( m_intercommand * 6, m_verbose );
	for (n = 0; m_drawSets[set][n].type != 0; n++)
	{
		switch (m_drawSets[set][n].type)
		{
			case 'M':	// Move
				ScalePoint( m_drawSets[set][n].pt[0], lastX, lastY );
				send_res = lio.SendCmd_MoveCut( 2, lastX, lastY );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercommand );
				break;
			case 'L':	// Straight line from previous point
				ScalePoint( m_drawSets[set][n].pt[0], lastX, lastY );
				send_res = lio.SendCmd_MoveCut( 0, lastX, lastY );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercommand );
				break;
			case 'C':	// Bezier curve from previous point
				ScalePoint( m_drawSets[set][n].pt[0], ctl1X, ctl1Y );
				ScalePoint( m_drawSets[set][n].pt[1], ctl2X, ctl2Y );
				ScalePoint( m_drawSets[set][n].pt[2], curX, curY );
				// Bezier curve data are sent in sets of 4
				send_res = lio.SendCmd_MoveCut( 1, lastX, lastY );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercurve ); // Very short wait to drain since no physical movement required
				send_res = lio.SendCmd_MoveCut( 1, ctl1X, ctl1Y );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercurve ); // Very short wait to drain since no physical movement required
				send_res = lio.SendCmd_MoveCut( 1, ctl2X, ctl2Y );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercurve ); // Very short wait to drain since no physical movement required
				send_res = lio.SendCmd_MoveCut( 1, curX, curY );
				reply_res = lio.ReadCmdReply( m_verbose );
				lio.Drain( m_verbose, m_intercommand );
				lastX = curX;
				lastY = curY;
				break;
			default:
				printf( "%s(..., %d...) warning: unhandled cut type %c at index %d\n",
					__FUNCTION__, set, m_drawSets[set][n].type, n );
				break;
		}
	}
	lio.SetVerbose( oldVerbose );
	// Return number of sets executed
	return n;
}

// Cut all draw sets
int LicutSVG::CutAllDrawSets( LicutIO& lio, int x, int y, int width, int height )
{
	int n;
	int returnValue = 0;
	for (n = 0; n < m_drawSetCount; n++)
	{
		if (m_verbose) printf( "%s() cutting draw set %d\n", __FUNCTION__, n );
		int r = CutDrawSet( lio, n, x, y, width, height );
		if (m_verbose) printf( "%s() draw set %d returned %d\n", __FUNCTION__, n, r );
		if (r < 0) return r;
		returnValue++;
	}

	return returnValue;
}

// Scale and translate SVG x,y pair to output absolute coordinates
void LicutSVG::ScalePoint( double xy[2], unsigned int &x, unsigned int &y )
{
	if (!m_width || !m_height)
	{
		printf( "Fatal error - cannot scale, no svg width and height\n" );
		exit( -1 );
	}
	x = m_outputX + m_outputWidth * xy[0] / m_width;
	y = m_outputY + m_outputHeight * xy[1] / m_height;
}

