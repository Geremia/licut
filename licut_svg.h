// $Id: licut_svg.h 1 2011-01-28 21:55:10Z henry_groover $

// ARM processor needs qword alignment for double access via strd
#pragma pack(8)
typedef struct _drawSet
{
	char type; // Type or '\0' if end of draw set list
	int numPoints;
	double pt[3][2];
} drawSet_t;
#pragma pack()

class LicutIO;

class LicutSVG
{
public:
	LicutSVG( int verbose );
	~LicutSVG();

	// Parse file - returns 0 if successful
	int Parse( const char *svgPath );

	// Get svg attributes
	unsigned int GetWidth() const { return m_width; }
	unsigned int GetHeight() const { return m_height; }

	// Get number of draw sets
	int GetDrawSetCount() const { return m_drawSetCount; }

	// Get draw set or NULL if undefined
	drawSet_t const *GetDrawSet( int index ) const;

	// Cut a single draw set
	int CutDrawSet( LicutIO& lio, int set, int x, int y, int width, int height );

	// Cut all draw sets
	int CutAllDrawSets( LicutIO& lio, int x, int y, int width, int height );

	// Set scaling and origin
	void SetScaling( int x, int y, int width, int height ) { m_outputX = x; m_outputY = y; m_outputWidth = width; m_outputHeight = height; }

	// Scale and translate SVG x,y pair to output absolute coordinates
	void ScalePoint( double xy[2], unsigned int &x, unsigned int &y );

	// Intercommand delay in ms
	int GetIntercommandDelay() const { return m_intercommand; }
	void SetIntercommandDelay( int ms ) { m_intercommand = ms; }
	
	// Intercurve delay in ms - between elements of a Bezier curve set
	int GetIntercurveDelay() const { return m_intercurve; }
	void SetIntercurveDelay( int ms ) { m_intercurve = ms; }
protected:
	// Parse node tags at the current level recursively. See notes above
	// Return number of node tags parsed
	int ParseTags( char *& s, int stackLevel, char *tagStack[1024], bool ignore );

	// Parse a single node tag recursively at the current level
	// Returns 1 if parsed or 0 if not a tag
	int ParseTag( char *& s, int stackLevel, char *tagStack[1024], bool ignore );

	// Parse draw list set values from d attribute
	// Return number of sets parsed
	int ParseDrawList( char *s );

protected:
	int m_verbose;
	unsigned int m_width;
	unsigned int m_height;
	int m_drawSetCount;
	drawSet_t **m_drawSets;
	int m_outputX;
	int m_outputY;
	int m_outputWidth;
	int m_outputHeight;
	int m_intercommand;
	int m_intercurve;
};

