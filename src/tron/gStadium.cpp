/*
SPDX-License-Identifier: GPL-2.0-or-later

This file is part of the browser and platform additions to Armagetron Advanced.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

/* A stadium around the arena, drawn after the rim walls through
   eGrid::SetSceneryRenderer. Everything is procedural: the bowl follows the
   rim walls' bounding box at any arena size (a rounded rectangle offset
   outwards), and its textures (crowd, crowd lights, LED dashes, floodlight
   panels, the scoreboard's dot matrix, a glow) are generated on first use, so
   nothing is downloaded. Plain GL 1.x immediate mode: the same code runs on the
   desktop client and on the browser's GLES2 layer. Lights are reflected in the
   floor by drawing them mirrored below it, additively, before the stands
   (the floor writes no depth, so walls and stands drawn later occlude the
   reflection correctly). */

#include "gStadium.h"

#include "defs.h"

#ifndef DEDICATED

#include "eAdvWall.h"
#include "eCamera.h"
#include "eCoord.h"
#include "eGrid.h"
#include "eRectangle.h"
#include "rGL.h"
#include "rRender.h"
#include "rScreen.h"
#include "rTexture.h"
#include "tConfiguration.h"
#include "tString.h"
#include "tSysTime.h"
#include "tConsole.h"
#include "tDirectories.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE GL_CLAMP
#endif

// *******************************************************************************************
// settings
// *******************************************************************************************

static int sg_stadium = 0;                   // 0 off, 1 on
static int sg_stadiumOverride = -1;          // the attract scene's choice, -1 none
static REAL sg_stadiumIntensity = 1;         // crowd lights and floodlights
static REAL sg_stadiumReflect = .5;          // the lights' reflection in the floor, 0 off
static REAL sg_stadiumSize = 1;              // the bowl's scale
static tString sg_stadiumScoreboard( "RETROCYCLES" );

static tSettingItem< int > sg_stadiumConf( "STADIUM", sg_stadium );
static tSettingItem< REAL > sg_stadiumIntensityConf( "STADIUM_INTENSITY", sg_stadiumIntensity );
static tSettingItem< REAL > sg_stadiumReflectConf( "STADIUM_REFLECT", sg_stadiumReflect );
static tSettingItem< REAL > sg_stadiumSizeConf( "STADIUM_SIZE", sg_stadiumSize );
static tSettingItem< tString > sg_stadiumScoreboardConf( "STADIUM_SCOREBOARD", sg_stadiumScoreboard );

void gStadiumSetOverride( int mode )
{
    sg_stadiumOverride = mode;
}

static bool sg_stadiumFitSet = false;
static eRectangle sg_stadiumFit;

void gStadiumSetFit( eRectangle const * floor )
{
    sg_stadiumFitSet = floor != NULL;
    if ( floor )
        sg_stadiumFit = *floor;
}

namespace
{

//! the floor the stands stand around: the rim walls' bounds, or the caller's
//! fitted floor (gStadiumSetFit) where it lies within them
eRectangle StadiumFloor()
{
    eRectangle const & rim = eWallRim::GetBounds();
    if ( !sg_stadiumFitSet )
        return rim;
    eCoord lo = sg_stadiumFit.GetLow(), hi = sg_stadiumFit.GetHigh();
    lo.x = std::max( lo.x, rim.GetLow().x );
    lo.y = std::max( lo.y, rim.GetLow().y );
    hi.x = std::min( hi.x, rim.GetHigh().x );
    hi.y = std::min( hi.y, rim.GetHigh().y );
    if ( hi.x - lo.x < 10 || hi.y - lo.y < 10 )
        return rim;
    return eRectangle( lo, hi );
}

// *******************************************************************************************
// generated textures
// *******************************************************************************************

typedef std::vector< unsigned char > Image;
typedef void ImageGenerator( Image & rgba, int & width, int & height );

class gStadiumTexture: public rISurfaceTexture
{
public:
    gStadiumTexture( ImageGenerator * generator, bool repeat )
    : rISurfaceTexture( rTextureGroups::TEX_OBJ, repeat, repeat, true ), generator_( generator ), repeat_( repeat )
    {
    }

protected:
    virtual void OnSelect()
    {
        Image rgba;
        int width = 0, height = 0;
        generator_( rgba, width, height );
        GLint wrap = repeat_ ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap );
        gluBuild2DMipmaps( GL_TEXTURE_2D, GL_RGBA, width, height, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0] );
    }

private:
    ImageGenerator * generator_;
    bool repeat_;
};

//! deterministic noise, so every client draws the same crowd
struct Random
{
    unsigned int state;
    explicit Random( unsigned int seed ): state( seed ) {}
    REAL Next()
    {
        state = state * 1664525u + 1013904223u;
        return ( state >> 8 ) * ( 1.0f / 16777216.0f );
    }
};

void Put( Image & rgba, int width, int x, int y, REAL r, REAL g, REAL b, REAL a )
{
    unsigned char * p = &rgba[ ( y * width + x ) * 4 ];
    p[0] = (unsigned char)( fmin( fmax( r, 0.0f ), 1.0f ) * 255 );
    p[1] = (unsigned char)( fmin( fmax( g, 0.0f ), 1.0f ) * 255 );
    p[2] = (unsigned char)( fmin( fmax( b, 0.0f ), 1.0f ) * 255 );
    p[3] = (unsigned char)( fmin( fmax( a, 0.0f ), 1.0f ) * 255 );
}

// the crowd: CROWD_ROWS rows of seats per texture height, people as dim
// silhouettes, low contrast; tiles horizontally
int const CROWD_W = 256, CROWD_H = 128, CROWD_ROWS = 8;
int const CROWD_ROW_PX = CROWD_H / CROWD_ROWS;
int const AISLE_PX = 10;

void CrowdImage( Image & rgba, int & width, int & height )
{
    width = CROWD_W;
    height = CROWD_H;
    rgba.assign( width * height * 4, 0 );
    // seats and steps
    for ( int y = 0; y < height; ++y )
    {
        int const inRow = y % CROWD_ROW_PX;
        REAL v = inRow < 2 ? .060f : ( inRow > CROWD_ROW_PX - 4 ? .016f : .032f );
        for ( int x = 0; x < width; ++x )
        {
            // a stairway between sections, once per tile
            REAL const w = x < AISLE_PX ? ( inRow < 3 ? .085f : .055f ) : v;
            Put( rgba, width, x, y, w * 1.25f, w * 1.4f, w * 1.7f, 1 );
        }
    }
    // people: head and shoulders, a muted shirt colour each
    static REAL const shirts[][3] = {
        { .11, .12, .14 }, { .07, .08, .13 }, { .14, .08, .08 }, { .08, .11, .10 },
        { .15, .15, .17 }, { .05, .06, .08 }, { .12, .10, .07 }, { .08, .08, .14 } };
    Random random( 12345 );
    for ( int row = 0; row < CROWD_ROWS; ++row )
    {
        int const top = row * CROWD_ROW_PX;
        for ( int slot = 0; slot < width / 8; ++slot )
        {
            if ( slot * 8 < AISLE_PX + 2 || random.Next() < .22f )
                continue; // the stairway, or an empty seat
            REAL const cx = slot * 8 + 4 + ( random.Next() - .5f ) * 3;
            REAL const * shirt = shirts[ (int)( random.Next() * 8 ) % 8 ];
            REAL const lift = ( random.Next() - .5f ) * 2.5f;
            for ( int y = top + 1; y < top + CROWD_ROW_PX; ++y )
            {
                for ( int dx = -5; dx <= 5; ++dx )
                {
                    int const x = ( (int)cx + dx + width ) % width;
                    REAL const fx = x + .5f - cx;
                    REAL const fx2 = fx > width / 2 ? fx - width : ( fx < -width / 2 ? fx + width : fx );
                    REAL const hy = y + .5f - ( top + 4.5f + lift );
                    REAL const sy = y + .5f - ( top + 11.5f + lift );
                    bool const head = fx2 * fx2 + hy * hy < 4.8f;
                    bool const body = ( fx2 * fx2 ) / 13.f + ( sy * sy ) / 9.f < 1 && sy > -3.f;
                    if ( head )
                        Put( rgba, width, x, y, .13f, .115f, .115f, 1 );
                    else if ( body )
                        Put( rgba, width, x, y, shirt[0] * 1.4f, shirt[1] * 1.4f, shirt[2] * 1.4f, 1 );
                }
            }
        }
    }
}

// the crowd's lights (phones, wristbands): sparse dots, drawn additively over
// the crowd with the same coordinates
void CrowdLightsImage( Image & rgba, int & width, int & height )
{
    width = CROWD_W;
    height = CROWD_H;
    rgba.assign( width * height * 4, 0 );
    static REAL const colors[][3] = {
        { 1, .93, .80 }, { 1, .93, .80 }, { 1, .93, .80 }, { 1, .72, .35 },
        { 1, .72, .35 }, { .35, .85, 1 }, { .35, .85, 1 }, { .95, .40, .95 } };
    // the stairway's step lights
    for ( int row = 0; row < CROWD_ROWS; ++row )
    {
        int const y = row * CROWD_ROW_PX + 1;
        for ( int dx = -1; dx <= 1; ++dx )
            Put( rgba, width, AISLE_PX / 2 + dx, y, 1, .62f, .28f, dx == 0 ? .8f : .3f );
    }
    Random random( 777 );
    for ( int i = 0; i < 110; ++i )
    {
        int const x = AISLE_PX + 1 + (int)( random.Next() * ( width - AISLE_PX - 2 ) ) % ( width - AISLE_PX - 2 );
        int const row = (int)( random.Next() * CROWD_ROWS ) % CROWD_ROWS;
        int const y = row * CROWD_ROW_PX + 6 + (int)( random.Next() * 4 );
        REAL const * c = colors[ (int)( random.Next() * 8 ) % 8 ];
        REAL const strength = .55f + .45f * random.Next();
        for ( int dy = -1; dy <= 1; ++dy )
            for ( int dx = -1; dx <= 1; ++dx )
            {
                REAL const a = ( dx == 0 && dy == 0 ) ? 1 : ( dx == 0 || dy == 0 ? .35f : .12f );
                Put( rgba, width, ( x + dx + width ) % width, ( y + dy + height ) % height,
                     c[0], c[1], c[2], a * strength );
            }
    }
}

// one LED dash per texture width, soft ends
void DashImage( Image & rgba, int & width, int & height )
{
    width = 64;
    height = 8;
    rgba.assign( width * height * 4, 0 );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            REAL ax = fmin( fmax( fmin( x - 8.f, 40.f - x ) / 2.f + .5f, 0.f ), 1.f );
            REAL ay = ( y == 0 || y == height - 1 ) ? .45f : 1;
            Put( rgba, width, x, y, 1, 1, 1, ax * ay );
        }
}

// a soft round glow
void GlowImage( Image & rgba, int & width, int & height )
{
    width = height = 64;
    rgba.assign( width * height * 4, 0 );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            REAL const fx = ( x + .5f ) / 32.f - 1, fy = ( y + .5f ) / 32.f - 1;
            REAL const r2 = fx * fx + fy * fy;
            REAL const a = r2 >= 1 ? 0 : ( expf( -r2 * 5.f ) - expf( -5.f ) ) * ( 1 - r2 );
            Put( rgba, width, x, y, 1, 1, 1, a );
        }
}

// a floodlight panel: 8x4 LEDs in a faintly lit housing
void PanelImage( Image & rgba, int & width, int & height )
{
    width = 64;
    height = 32;
    rgba.assign( width * height * 4, 0 );
    for ( int y = 0; y < height; ++y )
        for ( int x = 0; x < width; ++x )
        {
            REAL const fx = ( x % 8 ) + .5f - 4, fy = ( y % 8 ) + .5f - 4;
            REAL const r = sqrtf( fx * fx + fy * fy );
            REAL const led = fmin( fmax( 3.2f - r, 0.f ), 1.f );
            REAL const a = .10f + .90f * led;
            Put( rgba, width, x, y, .85f + .15f * led, .92f + .08f * led, 1, a );
        }
}

// the scoreboard: a dot matrix with a 5x7 font
struct Glyph
{
    char c;
    char const * rows[7];
};
Glyph const sg_glyphs[] = {
    { 'A', { " ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" } },
    { 'B', { "#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### " } },
    { 'C', { " ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### " } },
    { 'D', { "#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### " } },
    { 'E', { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####" } },
    { 'F', { "#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    " } },
    { 'G', { " ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####" } },
    { 'H', { "#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #" } },
    { 'I', { " ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### " } },
    { 'J', { "  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  " } },
    { 'K', { "#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #" } },
    { 'L', { "#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####" } },
    { 'M', { "#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #" } },
    { 'N', { "#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #" } },
    { 'O', { " ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " } },
    { 'P', { "#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    " } },
    { 'Q', { " ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #" } },
    { 'R', { "#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #" } },
    { 'S', { " ####", "#    ", "#    ", " ### ", "    #", "    #", "#### " } },
    { 'T', { "#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  " } },
    { 'U', { "#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### " } },
    { 'V', { "#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  " } },
    { 'W', { "#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # " } },
    { 'X', { "#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #" } },
    { 'Y', { "#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  " } },
    { 'Z', { "#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####" } },
    { '0', { " ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### " } },
    { '1', { "  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### " } },
    { '2', { " ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####" } },
    { '3', { "#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### " } },
    { '4', { "   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # " } },
    { '5', { "#####", "#    ", "#### ", "    #", "    #", "#   #", " ### " } },
    { '6', { "  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### " } },
    { '7', { "#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   " } },
    { '8', { " ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### " } },
    { '9', { " ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  " } },
    { '-', { "     ", "     ", "     ", "#####", "     ", "     ", "     " } },
    { '.', { "     ", "     ", "     ", "     ", "     ", " ##  ", " ##  " } },
    { ':', { "     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     " } },
    { '!', { "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  " } },
    { '/', { "    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    " } },
};

Glyph const * FindGlyph( char c )
{
    if ( c >= 'a' && c <= 'z' )
        c = c - 'a' + 'A';
    for ( size_t i = 0; i < sizeof( sg_glyphs ) / sizeof( sg_glyphs[0] ); ++i )
        if ( sg_glyphs[i].c == c )
            return &sg_glyphs[i];
    return NULL;
}

int const BOARD_W = 512, BOARD_H = 128, BOARD_MARGIN = 2, BOARD_MAX_CHARS = 40;
std::string sg_boardText;

//! the dot matrix layout of the current text: LED columns/rows and the cell size in pixels
void BoardLayout( int & columns, int & rows, int & cell )
{
    int const chars = (int)sg_boardText.size() > 0 ? (int)sg_boardText.size() : 1;
    columns = chars * 6 - 1 + 2 * BOARD_MARGIN;
    rows = 7 + 2 * BOARD_MARGIN;
    cell = std::min( BOARD_W / columns, BOARD_H / rows );
    if ( cell < 1 )
        cell = 1;
}

void BoardImage( Image & rgba, int & width, int & height )
{
    width = BOARD_W;
    height = BOARD_H;
    rgba.assign( width * height * 4, 0 );
    int columns, rows, cell;
    BoardLayout( columns, rows, cell );
    for ( int row = 0; row < rows; ++row )
        for ( int column = 0; column < columns; ++column )
        {
            bool lit = false;
            int const gr = row - BOARD_MARGIN, gc = column - BOARD_MARGIN;
            if ( gr >= 0 && gr < 7 && gc >= 0 && gc % 6 != 5 && gc / 6 < (int)sg_boardText.size() )
            {
                Glyph const * glyph = FindGlyph( sg_boardText[ gc / 6 ] );
                lit = glyph && glyph->rows[gr][gc % 6] == '#';
            }
            for ( int y = 0; y < cell; ++y )
                for ( int x = 0; x < cell; ++x )
                {
                    REAL const fx = ( x + .5f ) / cell - .5f, fy = ( y + .5f ) / cell - .5f;
                    REAL const r = sqrtf( fx * fx + fy * fy );
                    REAL const dot = fmin( fmax( ( .42f - r ) * cell + .5f, 0.f ), 1.f );
                    if ( lit )
                        Put( rgba, width, column * cell + x, row * cell + y, .78f, .96f, 1, dot );
                    else
                        Put( rgba, width, column * cell + x, row * cell + y, .35f, .45f, .55f, dot * .10f );
                }
        }
}

gStadiumTexture sg_crowdTexture( CrowdImage, true );
gStadiumTexture sg_crowdLightsTexture( CrowdLightsImage, true );
gStadiumTexture sg_dashTexture( DashImage, true );
gStadiumTexture sg_glowTexture( GlowImage, false );
gStadiumTexture sg_panelTexture( PanelImage, false );
gStadiumTexture sg_boardTexture( BoardImage, false );

// *******************************************************************************************
// the bowl's outline: a rounded rectangle at any distance outside the rim's bounds
// *******************************************************************************************

//! a point on the bounds with its outward normal; a distance d outwards it is
//! p + n * d, and the perimeter length up to it there is s + a * d
struct Station
{
    eCoord p, n;
    REAL s, a;
    bool corner;
};

void BuildStations( eRectangle const & bounds, REAL segment, std::vector< Station > & out, int cornerSteps = 8 )
{
    out.clear();
    eCoord const lo = bounds.GetLow(), hi = bounds.GetHigh();
    eCoord const corners[4] = { eCoord( lo.x, lo.y ), eCoord( hi.x, lo.y ), eCoord( hi.x, hi.y ), eCoord( lo.x, hi.y ) };
    eCoord const normals[4] = { eCoord( 0, -1 ), eCoord( 1, 0 ), eCoord( 0, 1 ), eCoord( -1, 0 ) };

    REAL s = 0, a = 0;
    for ( int side = 0; side < 4; ++side )
    {
        eCoord const from = corners[side], to = corners[( side + 1 ) % 4];
        REAL const length = sqrt( ( to - from ).NormSquared() );
        int const steps = std::max( 1, (int)ceil( length / segment ) );
        for ( int i = 0; i < steps; ++i )
        {
            Station station = { from + ( to - from ) * ( REAL( i ) / steps ), normals[side], s + length * i / steps, a, false };
            out.push_back( station );
        }
        s += length;
        // the corner at `to`: the normal turns a quarter
        for ( int k = 0; k < cornerSteps; ++k )
        {
            REAL const angle = ( M_PI * .5 ) * k / cornerSteps;
            eCoord const n = normals[side] * cos( angle ) + normals[( side + 1 ) % 4] * sin( angle );
            Station station = { to, n, s, a + angle, true };
            out.push_back( station );
        }
        a += M_PI * .5;
    }
    Station close = out.front();
    close.s = s;
    close.a = a;
    out.push_back( close );
}

// *******************************************************************************************
// drawing helpers
// *******************************************************************************************

struct Vec
{
    REAL x, y, z;
    Vec( REAL x_ = 0, REAL y_ = 0, REAL z_ = 0 ): x( x_ ), y( y_ ), z( z_ ) {}
    Vec operator+( Vec const & o ) const { return Vec( x + o.x, y + o.y, z + o.z ); }
    Vec operator-( Vec const & o ) const { return Vec( x - o.x, y - o.y, z - o.z ); }
    Vec operator*( REAL f ) const { return Vec( x * f, y * f, z * f ); }
    REAL Length() const { return sqrtf( x * x + y * y + z * z ); }
    Vec Normalized() const { REAL l = Length(); return l > 0 ? *this * ( 1 / l ) : *this; }
};
Vec Cross( Vec const & a, Vec const & b )
{
    return Vec( a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x );
}

Vec At( Station const & st, REAL d, REAL z )
{
    return Vec( st.p.x + st.n.x * d, st.p.y + st.n.y * d, z );
}

void V( Vec const & p )
{
    glVertex3f( p.x, p.y, p.z );
}

void TV( REAL u, REAL v, Vec const & p )
{
    glTexCoord2f( u, v );
    glVertex3f( p.x, p.y, p.z );
}

void Quad( Vec const & a, Vec const & b, Vec const & c, Vec const & d )
{
    TV( 0, 1, a );
    TV( 1, 1, b );
    TV( 1, 0, c );
    TV( 0, 0, d );
}

//! a glow (GlowImage) facing the camera, centred on p, size its radius: a disc of its own
//! triangle fan, not the texture's whole square. GlowImage is below 1/255 of its light past .7
//! of the radius, so this looks the same and shades half the pixels
void Sprite( Vec const & p, Vec const & camera, REAL size, REAL r, REAL g, REAL b, REAL a )
{
    Vec const view = ( p - camera ).Normalized();
    Vec right = Cross( view, Vec( 0, 0, 1 ) ).Normalized();
    if ( right.Length() < .5f )
        right = Vec( 1, 0, 0 );
    Vec const up = Cross( right, view ).Normalized();
    // ten sides round .82 of the radius: the polygon holds the disc of .78
    int const sides = 10;
    REAL const reach = .82f;
    glColor4f( r, g, b, a );
    BeginTriangleFan();
    TV( .5f, .5f, p );
    for ( int k = 0; k <= sides; ++k )
    {
        REAL const t = 2 * M_PI * k / sides, c = cosf( t ) * reach, s = sinf( t ) * reach;
        TV( .5f + c * .5f, .5f + s * .5f, p + right * ( c * size ) + up * ( s * size ) );
    }
    RenderEnd();
}

//! a light's reflection on a wet floor: a vertical streak below z = 0 under p
void Streak( Vec const & p, Vec const & camera, REAL width, REAL length, REAL r, REAL g, REAL b, REAL a )
{
    Vec right( -( p.y - camera.y ), p.x - camera.x, 0 );
    right = right.Normalized();
    Vec const c( p.x, p.y, -p.z );
    glColor4f( r, g, b, a );
    Quad( c - right * width + Vec( 0, 0, -length ), c + right * width + Vec( 0, 0, -length ),
          c + right * width + Vec( 0, 0, length ), c - right * width + Vec( 0, 0, length ) );
}

/* how much of a floodlight's glint the floor shows the eye. The floor mirrors the lamp where the
   line from the eye to the lamp's mirror image (below the floor) crosses it; a floodlight is
   aimed, though, and only lights its pool (DrawLightCones: .55 of the way from the lamp to the
   floor's centre, radius .32 of the beam's length), so the glint shows only where that point
   lies in the pool, fading out past its edge; elsewhere a glint would be a streak with no
   visible source. */
REAL GlintWeight( Vec const & lamp, Vec const & eye, Vec const & center )
{
    if ( eye.z <= 0 || lamp.z <= 0 )
        return 0;
    REAL const t = eye.z / ( eye.z + lamp.z );
    Vec const seen = eye + ( Vec( lamp.x, lamp.y, -lamp.z ) - eye ) * t;
    Vec const pool( lamp.x + ( center.x - lamp.x ) * .55f, lamp.y + ( center.y - lamp.y ) * .55f, 0 );
    REAL const radius = ( pool - lamp ).Length() * .32f;
    if ( radius <= 0 )
        return 0;
    REAL const d = Vec( seen.x - pool.x, seen.y - pool.y, 0 ).Length() / radius;
    if ( d >= 1.4f )
        return 0;
    if ( d <= .7f )
        return 1;
    REAL const k = ( 1.4f - d ) / .7f;
    return k * k * ( 3 - 2 * k );
}

void Additive()
{
    RenderEnd();
    glBlendFunc( GL_SRC_ALPHA, GL_ONE );
    glDepthMask( GL_FALSE );
}

void Normal()
{
    RenderEnd();
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glDepthMask( GL_TRUE );
}

void Untextured()
{
    RenderEnd();
    glDisable( GL_TEXTURE_2D );
}

// *******************************************************************************************
// the stadium
// *******************************************************************************************

//! the cross section, outwards from the rim (d) and up (z), in units of the
//! bowl's scale; each point starts the surface to the next one
enum Surface { FACE, WALK, CROWD, END };
struct ProfilePoint
{
    REAL d, z;
    Surface surface;
};
ProfilePoint const sg_profile[] = {
    { 5, 0, FACE },       // the podium wall around the floor
    { 5, 3, WALK },
    { 8, 3, CROWD },      // the lower tier
    { 34, 19, FACE },     // the concourse
    { 34, 23, WALK },
    { 37, 23, CROWD },    // the upper tier
    { 66, 48, FACE },     // the fascia
    { 66, 52, WALK },     // the roof's edge
    { 72, 52, END },
};
int const sg_profileCount = sizeof( sg_profile ) / sizeof( sg_profile[0] );

//! an emissive band along the bowl: distance, bottom, top (bowl units), dash
//! length in bowl units, colour, and how much of it the floor reflects
struct Band
{
    REAL d, z0, z1, dash;
    REAL r, g, b;
    REAL reflect;
};
Band const sg_bands[] = {
    { 4.9, 1.5, 2.2, 1.7, .24, .68, .80, 1 },     // the podium's LED strip
    { 33.9, 20.6, 21.3, 3.0, .70, .50, .28, .4 }, // the concourse's lamps
    { 65.9, 49.6, 50.3, 2.4, .60, .63, .70, .3 }, // the fascia's LED dashes
};
int const sg_bandCount = sizeof( sg_bands ) / sizeof( sg_bands[0] );

REAL const RIG_D = 52, RIG_Z = 62, BOARD_D = 61, BOARD_Z = 59, HAZE_D = 115, TOP_Z = 52;

struct Light
{
    Vec p, facing;
};

struct Layout
{
    REAL unit;                      //!< one bowl unit in grid units
    REAL bollardUnit;               //!< the bollards' size scale (they stand by the cycles)
    std::vector< Station > stations;
    std::vector< Light > panels;    //!< floodlight panels
    std::vector< Vec > bollards;    //!< foot points
    std::vector< Station > boards;  //!< scoreboard anchors (side midpoints)
    eCoord center;

    //! the crowd's lights as one pixel points (they stay visible at any
    //! distance, where a texture's dots would be filtered away)
    struct Spark
    {
        REAL r, g, b, strength, phase, speed;
        bool flash;
    };
    std::vector< Spark > sparks, steadySparks;
    std::vector< GLfloat > sparkPositions, steadyPositions;
    std::vector< GLubyte > sparkColors;
};

void BuildSparks( Layout & layout )
{
    static REAL const colors[][3] = {
        { 1, .93, .80 }, { 1, .93, .80 }, { 1, .93, .80 }, { 1, .72, .35 },
        { 1, .72, .35 }, { .35, .85, 1 }, { .35, .85, 1 }, { .95, .40, .95 } };
    layout.sparks.clear();
    layout.sparkPositions.clear();
    layout.steadySparks.clear();
    layout.steadyPositions.clear();
    Random random( 4242 );
    REAL const unit = layout.unit;
    REAL const perArea = 1 / ( 30 * unit * unit );
    std::vector< Station > const & st = layout.stations;
    for ( int k = 0; k + 1 < sg_profileCount; ++k )
    {
        ProfilePoint const & a = sg_profile[k], & b = sg_profile[k + 1];
        if ( a.surface != CROWD )
            continue;
        REAL const da = a.d * unit, db = b.d * unit, za = a.z * unit, zb = b.z * unit;
        REAL const slope = sqrtf( ( db - da ) * ( db - da ) + ( zb - za ) * ( zb - za ) );
        for ( size_t i = 0; i + 1 < st.size(); ++i )
        {
            REAL const dm = ( da + db ) * .5f;
            REAL const length = ( st[i + 1].s - st[i].s ) + ( st[i + 1].a - st[i].a ) * dm;
            REAL const expected = length * slope * perArea;
            int count = (int)expected;
            if ( random.Next() < expected - count )
                ++count;
            for ( int j = 0; j < count; ++j )
            {
                REAL const along = random.Next(), up = random.Next();
                Vec const p0 = At( st[i], da + ( db - da ) * up, za + ( zb - za ) * up );
                Vec const p1 = At( st[i + 1], da + ( db - da ) * up, za + ( zb - za ) * up );
                Vec const p = p0 + ( p1 - p0 ) * along;
                REAL const * c = colors[ (int)( random.Next() * 8 ) % 8 ];
                Layout::Spark spark = { c[0], c[1], c[2], .35f + .65f * random.Next() * random.Next(),
                                        random.Next() * 6.283f, .3f + random.Next() * 1.2f, random.Next() < .04f };
                // most lights hold still and go into the static list; a
                // fifth twinkles (and the odd camera flash), drawn each frame
                bool const steady = !spark.flash && random.Next() < .8f;
                std::vector< Layout::Spark > & sparks = steady ? layout.steadySparks : layout.sparks;
                std::vector< GLfloat > & positions = steady ? layout.steadyPositions : layout.sparkPositions;
                positions.push_back( p.x );
                positions.push_back( p.y );
                positions.push_back( p.z + .5f * unit );
                sparks.push_back( spark );
            }
        }
    }
    layout.sparkColors.assign( std::max( layout.sparks.size(), layout.steadySparks.size() ) * 4, 0 );
}

void DrawPoints( std::vector< GLfloat > const & positions, std::vector< GLubyte > const & colors, size_t n )
{
    if ( n == 0 )
        return;
    RenderEnd();
    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_COLOR_ARRAY );
    glVertexPointer( 3, GL_FLOAT, 0, &positions[0] );
    glColorPointer( 4, GL_UNSIGNED_BYTE, 0, &colors[0] );
    glDrawArrays( GL_POINTS, 0, (GLsizei)n );
    glDisableClientState( GL_COLOR_ARRAY );
    glDisableClientState( GL_VERTEX_ARRAY );
}

void DrawSteadySparks( Layout & layout, REAL intensity )
{
    size_t const n = layout.steadySparks.size();
    for ( size_t i = 0; i < n; ++i )
    {
        Layout::Spark const & s = layout.steadySparks[i];
        GLubyte * c = &layout.sparkColors[ i * 4 ];
        c[0] = (GLubyte)( s.r * 255 );
        c[1] = (GLubyte)( s.g * 255 );
        c[2] = (GLubyte)( s.b * 255 );
        c[3] = (GLubyte)( fmin( fmax( s.strength * .8f * intensity, 0.f ), 1.f ) * 255 );
    }
    DrawPoints( layout.steadyPositions, layout.sparkColors, n );
}

void DrawSparks( Layout & layout, REAL intensity, REAL time )
{
    size_t const n = layout.sparks.size();
    if ( n == 0 )
        return;
    for ( size_t i = 0; i < n; ++i )
    {
        Layout::Spark const & s = layout.sparks[i];
        REAL level = s.strength * ( .65f + .35f * sinf( time * s.speed + s.phase ) ) * intensity;
        REAL r = s.r, g = s.g, b = s.b;
        if ( s.flash && fmodf( time * .21f + s.phase, 7.f ) < .07f )
        {
            // a camera flash
            level = 1;
            r = g = b = 1;
        }
        GLubyte * c = &layout.sparkColors[ i * 4 ];
        c[0] = (GLubyte)( r * 255 );
        c[1] = (GLubyte)( g * 255 );
        c[2] = (GLubyte)( b * 255 );
        c[3] = (GLubyte)( fmin( fmax( level, 0.f ), 1.f ) * 255 );
    }
    DrawPoints( layout.sparkPositions, layout.sparkColors, n );
}

void BuildLayout( eRectangle const & bounds, eRectangle const & rim, Layout & layout )
{
    eCoord const lo = bounds.GetLow(), hi = bounds.GetHigh();
    REAL const half = std::max( hi.x - lo.x, hi.y - lo.y ) * .5f;
    // the bowl's scale follows the arena (the rim), also when a fitted floor
    // (gStadiumSetFit) brings the stands in closer
    eCoord const rimLo = rim.GetLow(), rimHi = rim.GetHigh();
    REAL const rimHalf = std::max( std::max( rimHi.x - rimLo.x, rimHi.y - rimLo.y ) * .5f, half );
    REAL const unit = std::max( rimHalf, (REAL)60 ) / 125 * ( sg_stadiumSize > .1f ? sg_stadiumSize : .1f );
    layout.unit = unit;
    layout.bollardUnit = std::min( unit, (REAL)1.6 );
    layout.center = ( lo + hi ) * .5f;
    BuildStations( bounds, half * .25f + 1, layout.stations );

    layout.panels.clear();
    layout.bollards.clear();
    layout.boards.clear();
    Vec const center( layout.center.x, layout.center.y, 0 );
    eCoord const corners[4] = { eCoord( lo.x, lo.y ), eCoord( hi.x, lo.y ), eCoord( hi.x, hi.y ), eCoord( lo.x, hi.y ) };
    eCoord const normals[4] = { eCoord( 0, -1 ), eCoord( 1, 0 ), eCoord( 0, 1 ), eCoord( -1, 0 ) };
    for ( int side = 0; side < 4; ++side )
    {
        eCoord const from = corners[side], to = corners[( side + 1 ) % 4];
        eCoord const n = normals[side];
        REAL const length = sqrt( ( to - from ).NormSquared() );

        // floodlight panels along the rig, aimed down at the arena's centre
        int const count = std::max( 2, (int)floor( length / ( 42 * unit ) + .5f ) );
        bool const boardSide = side == 1 || side == 3;
        for ( int i = 0; i < count; ++i )
        {
            REAL const t = ( i + .5f ) / count;
            if ( boardSide && fabs( t - .5f ) * length < std::min( length * .25f, 28 * unit ) + 4 * unit )
                continue; // the scoreboard hangs there
            eCoord const base = from + ( to - from ) * t + n * ( RIG_D * unit );
            Light light;
            light.p = Vec( base.x, base.y, RIG_Z * unit );
            light.facing = ( center - light.p ).Normalized();
            layout.panels.push_back( light );
        }
        // and one over every corner
        {
            eCoord const diagonal = n + normals[( side + 1 ) % 4];
            eCoord const base = to + diagonal * ( RIG_D * unit * .62f );
            Light light;
            light.p = Vec( base.x, base.y, RIG_Z * unit * 1.04f );
            light.facing = ( center - light.p ).Normalized();
            layout.panels.push_back( light );
        }

        // bollards along the floor's edge
        int const bollards = std::max( 2, (int)floor( length / ( 7.5f * unit ) + .5f ) );
        for ( int i = 0; i < bollards; ++i )
        {
            eCoord const base = from + ( to - from ) * ( ( i + .5f ) / bollards ) + n * ( 2.2f * unit );
            layout.bollards.push_back( Vec( base.x, base.y, 0 ) );
        }

        // scoreboards on the two ends across x
        if ( side == 1 || side == 3 )
        {
            Station anchor = { ( from + to ) * .5f, n, 0, 0, false };
            layout.boards.push_back( anchor );
        }
    }

    BuildSparks( layout );
}

void DrawProfile( Layout const & layout, Surface surface, bool textured )
{
    std::vector< Station > const & st = layout.stations;
    REAL const unit = layout.unit;
    REAL const tile = 24 * unit;           // crowd texture width in grid units
    REAL const rowHeight = 1.5f * unit;    // one seat row along the slope
    BeginQuads();
    for ( int k = 0; k + 1 < sg_profileCount; ++k )
    {
        ProfilePoint const & a = sg_profile[k], & b = sg_profile[k + 1];
        if ( a.surface != surface )
            continue;
        REAL const da = a.d * unit, db = b.d * unit, za = a.z * unit, zb = b.z * unit;
        REAL const slope = sqrtf( ( db - da ) * ( db - da ) + ( zb - za ) * ( zb - za ) );
        REAL const vb = slope / ( rowHeight * CROWD_ROWS );
        // lit from above: the upper rows a little brighter
        REAL const ca = .8f + .2f * a.z / TOP_Z, cb = .8f + .2f * b.z / TOP_Z;
        for ( size_t i = 0; i + 1 < st.size(); ++i )
        {
            Station const & s0 = st[i], & s1 = st[i + 1];
            if ( textured )
            {
                glColor4f( ca, ca, ca, 1 );
                TV( ( s0.s + s0.a * da ) / tile, vb, At( s0, da, za ) );
                TV( ( s1.s + s1.a * da ) / tile, vb, At( s1, da, za ) );
                glColor4f( cb, cb, cb, 1 );
                TV( ( s1.s + s1.a * db ) / tile, 0, At( s1, db, zb ) );
                TV( ( s0.s + s0.a * db ) / tile, 0, At( s0, db, zb ) );
            }
            else
            {
                REAL const base = surface == FACE ? .055f : .032f;
                glColor4f( base * ca * .85f, base * ca * .95f, base * ca * 1.2f, 1 );
                V( At( s0, da, za ) );
                V( At( s1, da, za ) );
                glColor4f( base * cb * .85f, base * cb * .95f, base * cb * 1.2f, 1 );
                V( At( s1, db, zb ) );
                V( At( s0, db, zb ) );
            }
        }
    }
    RenderEnd();
}

//! the crowd's lights, twinkling slowly by section
//! the crowd's lights from the texture: the close-up detail (the sparks twinkle)
void DrawCrowdLights( Layout const & layout, REAL intensity )
{
    std::vector< Station > const & st = layout.stations;
    REAL const unit = layout.unit;
    REAL const tile = 24 * unit;
    REAL const rowHeight = 1.5f * unit;
    BeginQuads();
    for ( int k = 0; k + 1 < sg_profileCount; ++k )
    {
        ProfilePoint const & a = sg_profile[k], & b = sg_profile[k + 1];
        if ( a.surface != CROWD )
            continue;
        REAL const da = a.d * unit, db = b.d * unit, za = a.z * unit, zb = b.z * unit;
        REAL const slope = sqrtf( ( db - da ) * ( db - da ) + ( zb - za ) * ( zb - za ) );
        REAL const vb = slope / ( rowHeight * CROWD_ROWS );
        for ( size_t i = 0; i + 1 < st.size(); ++i )
        {
            Station const & s0 = st[i], & s1 = st[i + 1];
            glColor4f( 1, 1, 1, intensity );
            TV( ( s0.s + s0.a * da ) / tile, vb, At( s0, da, za ) );
            TV( ( s1.s + s1.a * da ) / tile, vb, At( s1, da, za ) );
            TV( ( s1.s + s1.a * db ) / tile, 0, At( s1, db, zb ) );
            TV( ( s0.s + s0.a * db ) / tile, 0, At( s0, db, zb ) );
        }
    }
    RenderEnd();
}

void DrawBands( Layout const & layout, REAL brightness, bool reflection )
{
    std::vector< Station > const & st = layout.stations;
    REAL const unit = layout.unit;
    BeginQuads();
    for ( int k = 0; k < sg_bandCount; ++k )
    {
        Band const & band = sg_bands[k];
        REAL const f = brightness * ( reflection ? band.reflect : 1 );
        if ( f <= 0 )
            continue;
        REAL const d = band.d * unit, z0 = band.z0 * unit, z1 = band.z1 * unit, dash = band.dash * unit;
        glColor4f( band.r, band.g, band.b, f );
        for ( size_t i = 0; i + 1 < st.size(); ++i )
        {
            Station const & s0 = st[i], & s1 = st[i + 1];
            REAL const u0 = ( s0.s + s0.a * d ) / dash, u1 = ( s1.s + s1.a * d ) / dash;
            TV( u0, 1, At( s0, d, z0 ) );
            TV( u1, 1, At( s1, d, z0 ) );
            TV( u1, 0, At( s1, d, z1 ) );
            TV( u0, 0, At( s0, d, z1 ) );
        }
    }
    RenderEnd();
}

//! a light post: two crossed glowing quads, so it reads from any side
void BollardQuads( Layout const & layout, Vec const & foot, REAL bottom, REAL top )
{
    REAL const w = .16f * layout.bollardUnit;
    for ( int axis = 0; axis < 2; ++axis )
    {
        Vec const right = axis ? Vec( w, 0, 0 ) : Vec( 0, w, 0 );
        TV( 0, 1, Vec( foot.x, foot.y, bottom ) - right );
        TV( 1, 1, Vec( foot.x, foot.y, bottom ) + right );
        TV( 1, 0, Vec( foot.x, foot.y, top ) + right );
        TV( 0, 0, Vec( foot.x, foot.y, top ) - right );
    }
}

void DrawPanels( Layout const & layout, REAL brightness, REAL mirror )
{
    REAL const w = 4.2f * layout.unit, h = 2.2f * layout.unit;
    BeginQuads();
    glColor4f( .92f, .96f, 1, brightness );
    for ( size_t i = 0; i < layout.panels.size(); ++i )
    {
        Light const & light = layout.panels[i];
        Vec right = Cross( light.facing, Vec( 0, 0, 1 ) ).Normalized();
        Vec const up = Cross( right, light.facing ).Normalized();
        Vec const c( light.p.x, light.p.y, light.p.z * mirror );
        Vec const u = up * ( h * mirror );
        Quad( c - right * w - u, c + right * w - u, c + right * w + u, c - right * w + u );
    }
    RenderEnd();
}

void BoardFrame( Layout const & layout, Station const & anchor, REAL & width, REAL & height, Vec & center, Vec & right )
{
    int columns, rows, cell;
    BoardLayout( columns, rows, cell );
    eRectangle const area = StadiumFloor();
    eCoord const lo = area.GetLow(), hi = area.GetHigh();
    REAL const side = fabs( anchor.n.x ) > .5f ? hi.y - lo.y : hi.x - lo.x;
    width = std::min( side * .5f, 56 * layout.unit ) * .5f;
    height = width * rows / columns;
    eCoord const base = anchor.p + anchor.n * ( BOARD_D * layout.unit );
    center = Vec( base.x, base.y, BOARD_Z * layout.unit );
    right = Vec( anchor.n.y, -anchor.n.x, 0 ); // looking inwards, +right is to the viewer's right
}

void DrawBoardText( Layout const & layout, REAL brightness, REAL mirror )
{
    int columns, rows, cell;
    BoardLayout( columns, rows, cell );
    REAL const u1 = REAL( columns * cell ) / BOARD_W, v1 = REAL( rows * cell ) / BOARD_H;
    BeginQuads();
    glColor4f( 1, 1, 1, brightness );
    for ( size_t i = 0; i < layout.boards.size(); ++i )
    {
        REAL w, h;
        Vec c, right;
        BoardFrame( layout, layout.boards[i], w, h, c, right );
        Vec const in( -layout.boards[i].n.x * .05f, -layout.boards[i].n.y * .05f, 0 );
        c = c + in;
        c.z *= mirror;
        REAL const hz = h * mirror;
        TV( 0, v1, c - right * w + Vec( 0, 0, -hz ) );
        TV( u1, v1, c + right * w + Vec( 0, 0, -hz ) );
        TV( u1, 0, c + right * w + Vec( 0, 0, hz ) );
        TV( 0, 0, c - right * w + Vec( 0, 0, hz ) );
    }
    RenderEnd();
}

void DrawStructure( Layout const & layout )
{
    REAL const unit = layout.unit;

    // scoreboard housings and their hangers
    BeginQuads();
    for ( size_t i = 0; i < layout.boards.size(); ++i )
    {
        REAL w, h;
        Vec c, right;
        BoardFrame( layout, layout.boards[i], w, h, c, right );
        Vec const back( layout.boards[i].n.x * .6f * unit, layout.boards[i].n.y * .6f * unit, 0 );
        REAL const fw = w * 1.06f, fh = h * 1.25f;
        glColor4f( .012f, .014f, .018f, 1 );
        V( c - right * fw + Vec( 0, 0, -fh ) + back * .2f );
        V( c + right * fw + Vec( 0, 0, -fh ) + back * .2f );
        V( c + right * fw + Vec( 0, 0, fh ) + back * .2f );
        V( c - right * fw + Vec( 0, 0, fh ) + back * .2f );
        glColor4f( .03f, .035f, .045f, 1 );
        V( c - right * fw + Vec( 0, 0, -fh ) + back );
        V( c + right * fw + Vec( 0, 0, -fh ) + back );
        V( c + right * fw + Vec( 0, 0, fh ) + back );
        V( c - right * fw + Vec( 0, 0, fh ) + back );
    }
    RenderEnd();

    // the rigs: two chords and a zigzag between the panels, struts to the roof
    BeginLines();
    glColor4f( .10f, .115f, .14f, 1 );
    std::vector< Station > const & st = layout.stations;
    REAL const ztop = ( RIG_Z + 2.4f ) * unit, zbottom = ( RIG_Z - .6f ) * unit;
    for ( size_t i = 0; i + 1 < st.size(); ++i )
    {
        Vec const a0 = At( st[i], RIG_D * unit, ztop ), a1 = At( st[i + 1], RIG_D * unit, ztop );
        Vec const b0 = At( st[i], RIG_D * unit, zbottom ), b1 = At( st[i + 1], RIG_D * unit, zbottom );
        V( a0 ); V( a1 );
        V( b0 ); V( b1 );
        V( a0 ); V( b1 );
        if ( !st[i].corner )
        {
            // a strut down to the roof's edge behind
            V( a0 );
            V( At( st[i], 67 * unit, TOP_Z * unit ) );
            V( b0 );
            V( At( st[i], 67 * unit, TOP_Z * unit ) );
        }
    }
    for ( size_t i = 0; i < layout.boards.size(); ++i )
    {
        REAL w, h;
        Vec c, right;
        BoardFrame( layout, layout.boards[i], w, h, c, right );
        for ( int s = -1; s <= 1; s += 2 )
        {
            Vec const top = c + right * ( w * .8f * s ) + Vec( 0, 0, h * 1.25f );
            V( top );
            V( Vec( top.x, top.y, ztop ) );
            Vec const bottom = c + right * ( w * .8f * s ) + Vec( 0, 0, -h * 1.25f );
            V( bottom );
            V( Vec( bottom.x, bottom.y, 48 * unit ) );
        }
    }
    RenderEnd();
}

//! a dark sky band behind the bowl, lightest at the horizon
void DrawHaze( Layout const & layout, REAL lift = 0 )
{
    std::vector< Station > const & st = layout.stations;
    REAL const unit = layout.unit, d = HAZE_D * unit;
    REAL const z0 = ( 45 + lift ) * unit, z1 = ( 95 + lift * 1.6f ) * unit, z2 = ( 200 + lift * 2 ) * unit;
    REAL const glow = 1 + lift / 40;
    BeginQuads();
    for ( size_t i = 0; i + 1 < st.size(); ++i )
    {
        Station const & s0 = st[i], & s1 = st[i + 1];
        glColor4f( .040f * glow, .055f * glow, .090f * glow, 1 );
        V( At( s0, d, 0 ) );
        V( At( s1, d, 0 ) );
        V( At( s1, d, z0 ) );
        V( At( s0, d, z0 ) );
        V( At( s0, d, z0 ) );
        V( At( s1, d, z0 ) );
        glColor4f( .018f * glow, .025f * glow, .042f * glow, 1 );
        V( At( s1, d, z1 ) );
        V( At( s0, d, z1 ) );
        V( At( s0, d, z1 ) );
        V( At( s1, d, z1 ) );
        glColor4f( 0, 0, 0, 1 );
        V( At( s1, d, z2 ) );
        V( At( s0, d, z2 ) );
    }
    RenderEnd();
}

Layout sg_layout;

// what the compiled lists were built from; any change rebuilds them
struct ListKey
{
    eRectangle bounds, rim;
    REAL size, intensity, reflect;
    std::string text;
    bool textures;
    bool valid;
    bool model;
    int coneWalls;
};
ListKey sg_listKey = { eRectangle(), eRectangle(), 0, 0, 0, std::string(), false, false, false, 0 };
GLuint sg_lists = 0; // four: the reflections, the bowl, what glows, the light cones and pools

bool SameBounds( eRectangle const & a, eRectangle const & b )
{
    return ( a.GetLow() - b.GetLow() ).NormSquared() < 1e-6 && ( a.GetHigh() - b.GetHigh() ).NormSquared() < 1e-6;
}

void ForgetLists()
{
    if ( sg_lists )
        glDeleteLists( sg_lists, 4 );
    sg_lists = 0;
    sg_listKey.valid = false;
}

static rCallbackBeforeScreenModeChange sg_forgetLists( &ForgetLists );

// the parts that never change while the key stays the same; each block leaves
// its own state behind
void StaticReflections( Layout const & layout, REAL reflect )
{
    Additive();
    sg_dashTexture.Select();
    PushMatrix();
    glScalef( 1, 1, -1 );
    DrawBands( layout, reflect * .9f, true );
    PopMatrix();
    ModelMatrix();
    sg_glowTexture.Select();
    BeginQuads();
    glColor4f( .75f, .92f, 1, reflect * .9f );
    for ( size_t i = 0; i < layout.bollards.size(); ++i )
        BollardQuads( layout, layout.bollards[i], -2.6f * layout.bollardUnit, 0 );
    RenderEnd();
}

void StaticBowl( Layout const & layout, bool textures )
{
    Untextured();
    glDepthMask( GL_FALSE );
    DrawHaze( layout );
    Normal();
    Untextured();
    DrawProfile( layout, FACE, false );
    DrawProfile( layout, WALK, false );
    if ( textures )
    {
        sg_crowdTexture.Select();
        DrawProfile( layout, CROWD, true );
    }
    else
        DrawProfile( layout, CROWD, false );
    Untextured();
    DrawStructure( layout );
}

void StaticGlow( Layout & layout, REAL intensity )
{
    Additive();
    sg_crowdLightsTexture.Select();
    DrawCrowdLights( layout, intensity * .9f );
    Untextured();
    DrawSteadySparks( layout, intensity );
    sg_dashTexture.Select();
    DrawBands( layout, 1, false );
    sg_glowTexture.Select();
    BeginQuads();
    glColor4f( .80f, .95f, 1, 1 );
    for ( size_t i = 0; i < layout.bollards.size(); ++i )
        BollardQuads( layout, layout.bollards[i], 0, 2.6f * layout.bollardUnit );
    RenderEnd();
    sg_panelTexture.Select();
    DrawPanels( layout, intensity < 1 ? intensity : 1, 1 );
    sg_boardTexture.Select();
    DrawBoardText( layout, 1, 1 );
}

// *******************************************************************************************
// the modelled stadium: pieces made in Blender (art/stadium/build_stadium.py), lit by
// a baked light in their vertex colours, wrapped round the bowl
// *******************************************************************************************

//! one piece of a .stm file (format: art/stadium/build_stadium.py, "export")
struct StmPart
{
    int blend;              //!< 0 opaque, 1 alpha tested, 2 additive
    float glow;             //!< how much it feeds the bloom
    std::string texture;    //!< "" none, "@name" a generated one, else a data path
    std::vector< float > xyzuv;
    std::vector< unsigned char > rgba;
    std::vector< unsigned > index;
    float top = 0;          //!< highest z (bowl units)
};

struct StmMesh
{
    bool ok = false;
    float length = 0;       //!< a module's length along x (bowl units)
    std::vector< StmPart > parts;
};

bool LoadStm( char const * name, StmMesh & mesh )
{
    mesh = StmMesh();
    tString path = tDirectories::Data().GetReadPath( name );
    std::ifstream in( static_cast< char const * >( path ), std::ios::binary );
    if ( !in )
        return false;
    char magic[4];
    unsigned version = 0, count = 0;
    in.read( magic, 4 );
    in.read( reinterpret_cast< char * >( &version ), 4 );
    in.read( reinterpret_cast< char * >( &mesh.length ), 4 );
    in.read( reinterpret_cast< char * >( &count ), 4 );
    if ( !in || std::string( magic, 4 ) != "STM1" || version != 1 || count > 64 )
        return false;
    for ( unsigned p = 0; p < count; ++p )
    {
        StmPart part;
        unsigned char head[4];
        char texture[64];
        unsigned vertices = 0, indices = 0;
        in.read( reinterpret_cast< char * >( head ), 4 );
        in.read( reinterpret_cast< char * >( &part.glow ), 4 );
        in.read( texture, 64 );
        in.read( reinterpret_cast< char * >( &vertices ), 4 );
        in.read( reinterpret_cast< char * >( &indices ), 4 );
        if ( !in || vertices > 1000000 || indices > 3000000 )
            return false;
        texture[63] = 0;
        part.blend = head[0];
        part.texture = texture;
        part.xyzuv.resize( vertices * 5 );
        part.rgba.resize( vertices * 4 );
        for ( unsigned v = 0; v < vertices; ++v )
        {
            in.read( reinterpret_cast< char * >( &part.xyzuv[v * 5] ), 20 );
            in.read( reinterpret_cast< char * >( &part.rgba[v * 4] ), 4 );
            part.top = std::max( part.top, part.xyzuv[v * 5 + 2] );
        }
        part.index.resize( indices );
        if ( indices )
            in.read( reinterpret_cast< char * >( &part.index[0] ), indices * 4 );
        if ( !in )
            return false;
        mesh.parts.push_back( part );
    }
    mesh.ok = true;
    return true;
}

StmMesh sg_standMesh, sg_floodMesh, sg_boardMesh;
bool sg_meshesTried = false;

static bool sg_stadiumModel = true; // the modelled stadium (0: the procedural one)
static tSettingItem< bool > sg_stadiumModelConf( "STADIUM_MODEL", sg_stadiumModel );

bool ModelReady()
{
    if ( !sg_meshesTried )
    {
        sg_meshesTried = true;
        LoadStm( "models/stadium/stand.stm", sg_standMesh );
        LoadStm( "models/stadium/flood.stm", sg_floodMesh );
        LoadStm( "models/stadium/board.stm", sg_boardMesh );
        if ( !sg_standMesh.ok )
            con << "STADIUM: models/stadium/stand.stm not found, the procedural stadium stands in.\n";
    }
    return sg_stadiumModel && sg_standMesh.ok;
}

// the textures the parts name
std::vector< std::pair< std::string, rFileTexture * > > sg_fileTextures;

rITexture * PartTexture( std::string const & name )
{
    if ( name == "@dash" )
        return &sg_dashTexture;
    if ( name == "@panel" )
        return &sg_panelTexture;
    if ( name == "@glow" )
        return &sg_glowTexture;
    if ( name == "@board" )
        return &sg_boardTexture;
    if ( name.empty() || name[0] == '@' )
        return NULL;
    for ( size_t i = 0; i < sg_fileTextures.size(); ++i )
        if ( sg_fileTextures[i].first == name )
            return sg_fileTextures[i].second;
    rFileTexture * texture = new rFileTexture( rTextureGroups::TEX_OBJ, name.c_str(), true, true, true );
    sg_fileTextures.push_back( std::make_pair( name, texture ) );
    return texture;
}

//! where the bowl's modules go: the rounded rectangle measured at a reference
//! distance out (so the corners have a length too)
struct BowlPath
{
    std::vector< Station > st;
    std::vector< REAL > s;  //!< length at the reference distance up to each station
    REAL unit = 1, total = 0, scale = 1;
    int modules = 0;
};
BowlPath sg_path;
REAL const PATH_REF_D = 30; // bowl units out: the middle of the stands

void BuildPath( eRectangle const & bounds, REAL unit, REAL moduleLength, BowlPath & path )
{
    eCoord const lo = bounds.GetLow(), hi = bounds.GetHigh();
    REAL const half = std::max( hi.x - lo.x, hi.y - lo.y ) * .5f;
    BuildStations( bounds, half * .1f + 1, path.st, 24 );
    path.unit = unit;
    path.s.resize( path.st.size() );
    for ( size_t i = 0; i < path.st.size(); ++i )
        path.s[i] = path.st[i].s + path.st[i].a * PATH_REF_D * unit;
    path.total = path.s.back();
    path.modules = std::max( 4, (int)floor( path.total / ( moduleLength * unit ) + .5f ) );
    path.scale = path.total / ( path.modules * moduleLength * unit );
}

//! a point of the bowl: s along the path (reference length), d out, z up (grid units)
Vec PathPoint( BowlPath const & path, REAL s, REAL d, REAL z, Vec * outward = NULL )
{
    std::vector< REAL > const & ss = path.s;
    if ( s < 0 )
        s += path.total;
    if ( s > path.total )
        s -= path.total;
    size_t hi = std::upper_bound( ss.begin(), ss.end(), s ) - ss.begin();
    if ( hi == 0 )
        hi = 1;
    if ( hi >= ss.size() )
        hi = ss.size() - 1;
    Station const & a = path.st[hi - 1], & b = path.st[hi];
    REAL const span = ss[hi] - ss[hi - 1];
    REAL const f = span > 0 ? ( s - ss[hi - 1] ) / span : 0;
    eCoord const p = a.p + ( b.p - a.p ) * f;
    eCoord n = a.n + ( b.n - a.n ) * f;
    n = n * ( 1 / sqrt( n.NormSquared() ) );
    if ( outward )
        *outward = Vec( n.x, n.y, 0 );
    return Vec( p.x + n.x * d, p.y + n.y * d, z );
}

void PartState( StmPart const & part, REAL alphaRef )
{
    RenderEnd();
    rITexture * texture = PartTexture( part.texture );
    if ( texture )
        texture->Select();
    else
        Untextured();
    if ( part.blend == 2 )
        Additive();
    else
        Normal();
    glAlphaFunc( GL_GREATER, part.blend == 1 ? alphaRef : 0 );
#ifdef AA_GLES2
    sr_GLES2SetGlow( part.blend == 2 ? part.glow : 0 );
#endif
}

//! the stand modules round the whole bowl, parts of one kind (blend) only
void DrawStands( BowlPath const & path, int blend, REAL brightness, bool mirror )
{
    StmMesh const & mesh = sg_standMesh;
    REAL const unit = path.unit;
    for ( size_t p = 0; p < mesh.parts.size(); ++p )
    {
        StmPart const & part = mesh.parts[p];
        if ( part.blend != blend || part.index.empty() )
            continue;
        // the floor reflects the low LED ribbon; mirrored lamps higher up
        // landed on it as hard edged patches
        if ( mirror && ( part.blend != 2 || part.texture != "@dash" || part.top > 10 ) )
            continue;
        PartState( part, .45f );
        bool const board = part.texture == "@board";
        BeginTriangles();
        for ( int m = 0; m < path.modules; ++m )
        {
            REAL const s0 = m * mesh.length * unit * path.scale;
            for ( size_t k = 0; k < part.index.size(); ++k )
            {
                unsigned const v = part.index[k];
                float const * q = &part.xyzuv[v * 5];
                unsigned char const * c = &part.rgba[v * 4];
                REAL const f = blend == 2 ? brightness : 1;
                glColor4f( c[0] / 255.f * f, c[1] / 255.f * f, c[2] / 255.f * f, c[3] / 255.f );
                glTexCoord2f( q[3], board ? 1 - q[4] : q[4] );
                Vec const w = PathPoint( path, s0 + q[0] * unit * path.scale, q[1] * unit, q[2] * unit );
                glVertex3f( w.x, w.y, mirror ? -w.z : w.z );
            }
        }
        RenderEnd();
    }
#ifdef AA_GLES2
    sr_GLES2SetGlow( 0 );
#endif
    glAlphaFunc( GL_GREATER, 0 );
}

//! a rigid piece (floodlight, scoreboard) at `origin` with its x along `right`,
//! y along `out`, z up, scaled per axis
void DrawPiece( StmMesh const & mesh, int blend, Vec const & origin, Vec const & right, Vec const & out,
                Vec const & scale, REAL brightness, bool mirror, REAL u1 = 1, REAL v1 = 1 )
{
    for ( size_t p = 0; p < mesh.parts.size(); ++p )
    {
        StmPart const & part = mesh.parts[p];
        if ( part.blend != blend || part.index.empty() )
            continue;
        if ( mirror && part.blend != 2 )
            continue;
        PartState( part, .45f );
        bool const board = part.texture == "@board";
        BeginTriangles();
        for ( size_t k = 0; k < part.index.size(); ++k )
        {
            unsigned const v = part.index[k];
            float const * q = &part.xyzuv[v * 5];
            unsigned char const * c = &part.rgba[v * 4];
            REAL const f = blend == 2 ? brightness : 1;
            glColor4f( c[0] / 255.f * f, c[1] / 255.f * f, c[2] / 255.f * f, c[3] / 255.f );
            if ( board )
                glTexCoord2f( q[3] * u1, ( 1 - q[4] ) * v1 );
            else
                glTexCoord2f( q[3], q[4] );
            Vec const w = origin + right * ( q[0] * scale.x ) + out * ( q[1] * scale.y ) + Vec( 0, 0, q[2] * scale.z );
            glVertex3f( w.x, w.y, mirror ? -w.z : w.z );
        }
        RenderEnd();
    }
#ifdef AA_GLES2
    sr_GLES2SetGlow( 0 );
#endif
    glAlphaFunc( GL_GREATER, 0 );
}

// the floodlights of the modelled bowl: on the roof's front edge of every module
REAL const FLOOD_X = 12, FLOOD_D = 41.2, FLOOD_Z = 65.0, MODEL_ROOF_Z = 66;

void PlaceFloods( BowlPath const & path, Layout & layout )
{
    layout.panels.clear();
    Vec const center( layout.center.x, layout.center.y, 0 );
    for ( int m = 0; m < path.modules; ++m )
    {
        REAL const s = ( m * sg_standMesh.length + FLOOD_X ) * path.unit * path.scale;
        Vec out;
        Light light;
        light.p = PathPoint( path, s, FLOOD_D * path.unit, FLOOD_Z * path.unit, &out );
        light.facing = out * -1;
        layout.panels.push_back( light );
    }
}

void DrawFloods( Layout const & layout, int blend, REAL brightness, bool mirror )
{
    if ( !sg_floodMesh.ok )
        return;
    REAL const u = sg_path.unit;
    for ( size_t i = 0; i < layout.panels.size(); ++i )
    {
        Vec const out = layout.panels[i].facing * -1;
        Vec const right( -out.y, out.x, 0 );
        DrawPiece( sg_floodMesh, blend, layout.panels[i].p, right, out, Vec( u, u, u ), brightness, mirror );
    }
}

//! the scoreboards: over the roof at the two ends across x
void DrawModelBoards( Layout const & layout, int blend, REAL brightness, bool mirror )
{
    if ( !sg_boardMesh.ok )
        return;
    int columns, rows, cell;
    BoardLayout( columns, rows, cell );
    REAL const u1 = REAL( columns * cell ) / BOARD_W, v1 = REAL( rows * cell ) / BOARD_H;
    eRectangle const area = StadiumFloor();
    eCoord const lo = area.GetLow(), hi = area.GetHigh();
    REAL const unit = sg_path.unit;
    for ( size_t i = 0; i < layout.boards.size(); ++i )
    {
        Station const & anchor = layout.boards[i];
        REAL const side = fabs( anchor.n.x ) > .5f ? hi.y - lo.y : hi.x - lo.x;
        REAL const width = std::min( side * .42f, 50 * unit );
        REAL const height = width * rows / columns;
        eCoord const base = anchor.p + anchor.n * ( 46 * unit );
        Vec const origin( base.x, base.y, ( MODEL_ROOF_Z + 7 ) * unit + height * .5f );
        Vec const out( anchor.n.x, anchor.n.y, 0 );
        Vec const right( anchor.n.y, -anchor.n.x, 0 );
        DrawPiece( sg_boardMesh, blend, origin, right, out, Vec( width, height, height ), brightness, mirror, u1, v1 );
    }
}

// the light cones' walls: 2 both, 1 only the one facing the eye (cheaper; the Low tier's choice,
// gBrowserQuality.cpp). Compiled into the lists
static int sg_stadiumConeWalls = 2;
static tSettingItem< int > sg_stadiumConeWallsConf( "STADIUM_CONE_WALLS", sg_stadiumConeWalls );

// the floodlights' light in a game: their cones, the pools on the floor (and the lamps' glints
// in them), the halos and the glow in the night air. 0 leaves them out of games (the Low tier,
// gBrowserQuality.cpp); the attract scene (gStadiumSetOverride) always has them
static bool sg_stadiumGameLights = true;
static tSettingItem< bool > sg_stadiumGameLightsConf( "STADIUM_GAME_LIGHTS", sg_stadiumGameLights );

//! light falling from the rigs: a faint cone per floodlight bank (every third)
//! and a pool of light where it lands on the floor
void DrawLightCones( Layout const & layout, REAL intensity )
{
    Vec const center( layout.center.x, layout.center.y, 0 );
    REAL const unit = sg_path.unit;
    int const segments = 14;
    Additive();
    Untextured();
#ifdef AA_GLES2
    sr_GLES2SetGlow( 0 );
#endif
    // wound outwards, so culling keeps the wall facing the eye (STADIUM_CONE_WALLS 1)
    bool const oneWall = sg_stadiumConeWalls == 1;
    REAL const coneLight = oneWall ? .066f : .055f;
    if ( oneWall )
        glEnable( GL_CULL_FACE );
    BeginQuads();
    for ( size_t i = 0; i < layout.panels.size(); i += 3 )
    {
        Vec const apex = layout.panels[i].p + Vec( 0, 0, -2 * unit );
        Vec const target( apex.x + ( center.x - apex.x ) * .55f, apex.y + ( center.y - apex.y ) * .55f, 0 );
        Vec const axis = target - apex;
        REAL const length = axis.Length();
        Vec const dir = axis.Normalized();
        Vec a = Cross( dir, Vec( 0, 0, 1 ) ).Normalized();
        Vec const b = Cross( dir, a ).Normalized();
        REAL const r0 = 1.5f * unit, r1 = length * .2f;
        for ( int k = 0; k < segments; ++k )
        {
            REAL const t0 = 2 * M_PI * k / segments, t1 = 2 * M_PI * ( k + 1 ) / segments;
            Vec const d0 = a * cosf( t0 ) + b * sinf( t0 ), d1 = a * cosf( t1 ) + b * sinf( t1 );
            glColor4f( .62f, .70f, .85f, coneLight * intensity );
            glVertex3f( apex.x + d1.x * r0, apex.y + d1.y * r0, apex.z + d1.z * r0 );
            glVertex3f( apex.x + d0.x * r0, apex.y + d0.y * r0, apex.z + d0.z * r0 );
            glColor4f( .62f, .70f, .85f, 0 );
            glVertex3f( target.x + d0.x * r1, target.y + d0.y * r1, target.z + d0.z * r1 );
            glVertex3f( target.x + d1.x * r1, target.y + d1.y * r1, target.z + d1.z * r1 );
        }
    }
    RenderEnd();
    if ( oneWall )
        glDisable( GL_CULL_FACE );
    // the pools: discs of .8 of the glow's radius, not its whole square (GlowImage is below
    // 1/255 of the pool's light past .7), which shades half the pixels
    sg_glowTexture.Select();
    BeginTriangles();
    int const sides = 16;
    REAL const reach = .8f;
    for ( size_t i = 0; i < layout.panels.size(); i += 3 )
    {
        Vec const apex = layout.panels[i].p;
        Vec const target( apex.x + ( center.x - apex.x ) * .55f, apex.y + ( center.y - apex.y ) * .55f, .05f );
        REAL const r = ( target - apex ).Length() * .32f;
        glColor4f( .55f, .64f, .80f, .16f * intensity );
        for ( int k = 0; k < sides; ++k )
        {
            REAL const t0 = 2 * M_PI * k / sides, t1 = 2 * M_PI * ( k + 1 ) / sides;
            REAL const c0 = cosf( t0 ) * reach, s0 = sinf( t0 ) * reach, c1 = cosf( t1 ) * reach, s1 = sinf( t1 ) * reach;
            TV( .5f, .5f, target );
            TV( .5f + c0 * .5f, .5f + s0 * .5f, target + Vec( c0 * r, s0 * r, 0 ) );
            TV( .5f + c1 * .5f, .5f + s1 * .5f, target + Vec( c1 * r, s1 * r, 0 ) );
        }
    }
    RenderEnd();
}

void ModelReflections( Layout const & layout, REAL reflect )
{
    DrawStands( sg_path, 2, reflect * .8f, true );
}

void ModelBowl( Layout const & layout )
{
    Untextured();
    glDepthMask( GL_FALSE );
    DrawHaze( layout, 30 );
    DrawStands( sg_path, 0, 1, false );
    DrawFloods( layout, 0, 1, false );
    DrawModelBoards( layout, 0, 1, false );
    DrawStands( sg_path, 1, 1, false );
}

void ModelGlow( Layout const & layout, REAL intensity )
{
    DrawStands( sg_path, 2, 1, false );
    DrawFloods( layout, 2, intensity < 1 ? intensity : 1, false );
    DrawModelBoards( layout, 2, 1, false );
    // the bollards as plain lit bars (a glow texture on crossed quads read as crosses)
    Additive();
    Untextured();
    BeginQuads();
    for ( size_t i = 0; i < layout.bollards.size(); ++i )
    {
        Vec const & foot = layout.bollards[i];
        REAL const w = .12f * layout.bollardUnit, h = 2.4f * layout.bollardUnit;
        for ( int axis = 0; axis < 2; ++axis )
        {
            Vec const r = axis ? Vec( w, 0, 0 ) : Vec( 0, w, 0 );
            glColor4f( .45f, .70f, .85f, .7f );
            V( foot - r );
            V( foot + r );
            glColor4f( .85f, .97f, 1, 1 );
            V( foot + r + Vec( 0, 0, h ) );
            V( foot - r + Vec( 0, 0, h ) );
        }
    }
    RenderEnd();
}

GLuint NewLists()
{
#ifdef AA_GLES2
    return sr_GLES2GenLists( 4 );
#else
    return glGenLists( 4 );
#endif
}

void Render( eGrid *, eCamera const * camera )
{
    int const mode = sg_stadiumOverride >= 0 ? sg_stadiumOverride : sg_stadium;
    bool const lights = sg_stadiumOverride >= 0 || sg_stadiumGameLights;
    if ( mode <= 0 || !sr_glOut || !camera )
        return;

    eRectangle const bounds = StadiumFloor();
    eRectangle const & rim = eWallRim::GetBounds();
    if ( bounds.GetHigh().x <= bounds.GetLow().x || bounds.GetHigh().y <= bounds.GetLow().y )
        return;

    std::string text( static_cast< char const * >( sg_stadiumScoreboard ) );
    if ( (int)text.size() > BOARD_MAX_CHARS )
        text.resize( BOARD_MAX_CHARS );

    bool const textures = rTextureGroups::TextureMode[ rTextureGroups::TEX_OBJ ] != 0;
    REAL const intensity = sg_stadiumIntensity > 0 ? sg_stadiumIntensity : 0;
    REAL const reflect = sg_stadiumReflect > 0 ? sg_stadiumReflect : 0;
    REAL const time = tSysTimeFloat();
    Vec const eye( camera->CameraPos().x, camera->CameraPos().y, camera->CameraZ() );

    bool const model = ModelReady();
    ListKey & key = sg_listKey;
    bool const rebuild = !key.valid || !SameBounds( bounds, key.bounds ) || !SameBounds( rim, key.rim ) || key.size != sg_stadiumSize
        || key.intensity != intensity || key.reflect != reflect || key.text != text || key.textures != textures
        || key.model != model || key.coneWalls != sg_stadiumConeWalls;
    if ( rebuild )
    {
        if ( !key.valid || !SameBounds( bounds, key.bounds ) || !SameBounds( rim, key.rim ) || key.size != sg_stadiumSize || key.model != model )
        {
            BuildLayout( bounds, rim, sg_layout );
            if ( model )
            {
                BuildPath( bounds, sg_layout.unit, sg_standMesh.length, sg_path );
                PlaceFloods( sg_path, sg_layout );
                size_t corners = 0;
                for ( size_t p = 0; p < sg_standMesh.parts.size(); ++p )
                    corners += sg_standMesh.parts[p].index.size();
                printf( "STADIUM: bowl of %d modules, %u triangle corners (%u per module), %u floodlights\n",
                        sg_path.modules, unsigned( corners * sg_path.modules ), unsigned( corners ),
                        unsigned( sg_layout.panels.size() ) );
            }
        }
        key.model = model;
        key.coneWalls = sg_stadiumConeWalls;
        if ( key.text != text || !key.valid )
        {
            sg_boardText = text;
            sg_boardTexture.Unload();
        }
        key.bounds = bounds;
        key.rim = rim;
        key.size = sg_stadiumSize;
        key.intensity = intensity;
        key.reflect = reflect;
        key.text = text;
        key.textures = textures;
        key.valid = true;
        if ( sg_lists )
            glDeleteLists( sg_lists, 4 );
        sg_lists = 0;
    }
    Layout const & layout = sg_layout;

    RenderEnd();
    ModelMatrix();
    TexMatrix();
    glLoadIdentity();
    ModelMatrix();
    glDisable( GL_LIGHTING );
    glDisable( GL_CULL_FACE );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );

    if ( !sg_lists )
    {
        // textures are uploaded outside a list (an upload is not compiled)
        if ( textures )
        {
            sg_crowdTexture.Select();
            sg_crowdLightsTexture.Select();
            sg_dashTexture.Select();
            sg_glowTexture.Select();
            sg_panelTexture.Select();
            sg_boardTexture.Select();
            if ( model )
                for ( size_t p = 0; p < sg_standMesh.parts.size(); ++p )
                    if ( rITexture * texture = PartTexture( sg_standMesh.parts[p].texture ) )
                        texture->Select();
        }
        sg_lists = NewLists();
        if ( sg_lists )
        {
            glNewList( sg_lists, GL_COMPILE );
            if ( textures && reflect > 0 )
            {
                if ( model )
                    ModelReflections( layout, reflect );
                else
                    StaticReflections( layout, reflect );
            }
            RenderEnd();
            glEndList();
            glNewList( sg_lists + 1, GL_COMPILE );
            if ( model )
                ModelBowl( layout );
            else
                StaticBowl( layout, textures );
            RenderEnd();
            glEndList();
            glNewList( sg_lists + 2, GL_COMPILE );
            if ( textures )
            {
                if ( model )
                    ModelGlow( layout, intensity );
                else
                    StaticGlow( sg_layout, intensity );
            }
            RenderEnd();
            glEndList();
            // the light cones and their pools: a list of their own, so a game without them
            // (STADIUM_GAME_LIGHTS 0) and the menu with them share the other three
            glNewList( sg_lists + 3, GL_COMPILE );
            if ( textures && model )
                DrawLightCones( layout, intensity );
            RenderEnd();
            glEndList();
        }
    }
    bool const lists = sg_lists != 0;
    bool const reflections = reflect > 0 && camera->RenderingMain() && textures;

    // the sky band (no depth writes) and the bowl
    if ( lists )
        glCallList( sg_lists + 1 );
    else if ( model )
        ModelBowl( layout );
    else
        StaticBowl( layout, textures );

    // the lights' reflections on the floor. They lie below the floor, so the
    // bowl's depth already hides those that are behind it.
    if ( reflections )
    {
        if ( lists )
            glCallList( sg_lists );
        else if ( model )
            ModelReflections( layout, reflect );
        else
            StaticReflections( layout, reflect );
        Additive();
        sg_glowTexture.Select();
        BeginQuads();
        for ( size_t i = 0; i < layout.bollards.size(); ++i )
        {
            Vec const & foot = layout.bollards[i];
            Streak( Vec( foot.x, foot.y, 2.0f * layout.bollardUnit ), eye, .7f * layout.bollardUnit,
                    2.4f * layout.bollardUnit, .55f, .80f, 1, reflect * .5f );
        }
        // the roof lamps' glints, only in their pools of light (GlintWeight); the modelled
        // stadium throws a pool from every third bank (DrawLightCones), so only those glint
        Vec const center( layout.center.x, layout.center.y, 0 );
        for ( size_t i = 0; lights && i < layout.panels.size(); i += model ? 3 : 1 )
        {
            REAL const glint = GlintWeight( layout.panels[i].p, eye, center );
            if ( glint > 0 )
                Streak( layout.panels[i].p, eye, 1.4f * layout.unit, 16 * layout.unit, .85f, .90f, 1, reflect * .5f * intensity * glint );
        }
        RenderEnd();
    }

    // what glows
    if ( textures )
    {
        if ( lists )
            glCallList( sg_lists + 2 );
        else if ( model )
            ModelGlow( layout, intensity );
        else
            StaticGlow( sg_layout, intensity );
        if ( !lights )
            ;
        else if ( lists )
            glCallList( sg_lists + 3 );
        else if ( model )
            DrawLightCones( layout, intensity );

        Additive();
        Untextured();
        if ( !model )
            DrawSparks( sg_layout, intensity, time );

        sg_glowTexture.Select();
        REAL const halo = model ? 8 : 13;
        for ( size_t i = 0; lights && i < layout.panels.size(); ++i )
        {
            Sprite( layout.panels[i].p, eye, halo * layout.unit, .55f, .62f, .75f, .50f * intensity );
            // the light the rigs throw into the night air
            if ( !model || i % 3 == 0 )
                Sprite( layout.panels[i].p + Vec( 0, 0, 14 * layout.unit ), eye, 70 * layout.unit,
                        .30f, .36f, .50f, ( model ? .05f : .07f ) * intensity );
        }
        RenderEnd();
    }

    Normal();
    Untextured();
    glEnable( GL_CULL_FACE );
    glColor4f( 1, 1, 1, 1 );
}

struct Registration
{
    Registration()
    {
        eGrid::SetSceneryRenderer( &Render );
    }
};
Registration sg_registration;

} // namespace

eRectangle gStadiumFloor()
{
    return StadiumFloor();
}

#else // DEDICATED

void gStadiumSetOverride( int )
{
}

void gStadiumSetFit( eRectangle const * )
{
}

#endif
