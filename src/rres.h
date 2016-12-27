/**********************************************************************************************
*
*   rres - raylib Resource custom format management functions
*
*   Basic functions to load/save rRES resource files
*
*   External libs:
*       tinfl   -  DEFLATE decompression functions
*
*   Module Configuration Flags:
*
*       #define RREM_IMPLEMENTATION
*           Generates the implementation of the library into the included file.
*
*
*   Copyright (c) 2016-2017 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#ifndef RRES_H
#define RRES_H

#if !defined(RRES_STANDALONE)
    #include "raylib.h"
#endif

//#define RRES_STATIC
#ifdef RRES_STATIC
    #define RRESDEF static              // Functions just visible to module including this file
#else
    #ifdef __cplusplus
        #define RRESDEF extern "C"      // Functions visible from other files (no name mangling of functions in C++)
    #else
        #define RRESDEF extern          // Functions visible from other files
    #endif
#endif

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MAX_RESOURCES_SUPPORTED   256

//----------------------------------------------------------------------------------
// Types and Structures Definition
// NOTE: Some types are required for RAYGUI_STANDALONE usage
//----------------------------------------------------------------------------------
#if defined(RRES_STANDALONE)
    // rRES data returned when reading a resource, it contains all required data for user (24 byte)
    typedef struct {
        unsigned int type;          // Resource type (4 byte)
        
        unsigned int param1;        // Resouce parameter 1 (4 byte)
        unsigned int param2;        // Resouce parameter 2 (4 byte)
        unsigned int param3;        // Resouce parameter 3 (4 byte)
        unsigned int param4;        // Resouce parameter 4 (4 byte)
        
        void *data;                 // Resource data pointer (4 byte)
    } RRESData;
    
    typedef enum { 
        RRES_RAW = 0, 
        RRES_IMAGE, 
        RRES_WAVE, 
        RRES_VERTEX, 
        RRES_TEXT 
    } RRESDataType;
#endif

//----------------------------------------------------------------------------------
// Global variables
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
RRESDEF RRESData LoadResource(const char *rresFileName);
RRESDEF RRESData LoadResourceById(const char *rresFileName, int rresId);
RRESDEF void UnloadResource(RRESData rres);

#endif // RRES_H


/***********************************************************************************
*
*   RRES IMPLEMENTATION
*
************************************************************************************/

#if defined(RRES_IMPLEMENTATION)

#include <stdio.h>          // Required for: FILE, fopen(), fclose()

// Check if custom malloc/free functions defined, if not, using standard ones
#if !defined(RRES_MALLOC)
    #include <stdlib.h>     // Required for: malloc(), free()

    #define RRES_MALLOC(size)  malloc(size)
    #define RRES_FREE(ptr)     free(ptr)
#endif

#include "external/tinfl.c" // Required for: tinfl_decompress_mem_to_mem()
                            // NOTE: DEFLATE algorythm data decompression

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------

// rRES file header (8 byte)
typedef struct {
    char id[4];                 // File identifier: rRES (4 byte)
    unsigned short version;     // File version and subversion (2 byte)
    unsigned short count;       // Number of resources in this file (2 byte)
} RRESFileHeader;

// rRES info header, every resource includes this header (12 byte + 16 byte)
typedef struct {
    unsigned short id;          // Resource unique identifier (2 byte)
    unsigned char dataType;     // Resource data type (1 byte)
    unsigned char compType;     // Resource data compression type (1 byte)
    unsigned int dataSize;      // Resource data size (compressed or not, only DATA) (4 byte)
    unsigned int uncompSize;    // Resource data size (uncompressed, only DATA) (4 byte)

    unsigned int param1;        // Resouce parameter 1 (4 byte)
    unsigned int param2;        // Resouce parameter 2 (4 byte)
    unsigned int param3;        // Resouce parameter 3 (4 byte)
    unsigned int param4;        // Resouce parameter 4 (4 byte)
} RRESInfoHeader;

// Compression types
typedef enum { 
    RRES_COMP_NONE = 0,         // No data compression
    RRES_COMP_DEFLATE,          // DEFLATE compression
    RRES_COMP_LZ4,              // LZ4 compression
    RRES_COMP_LZMA,             // LZMA compression
    // brotli, zopfli, gzip     // Other compression algorythms...
} RRESCompressionType;

#if defined(RRES_STANDALONE)
typedef enum { INFO = 0, ERROR, WARNING, DEBUG, OTHER } TraceLogType;
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Module specific Functions Declaration
//----------------------------------------------------------------------------------
static void *DecompressData(const unsigned char *data, unsigned long compSize, int uncompSize);

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------

// Load resource from file (only one)
// NOTE: Returns uncompressed data with parameters, only first resource found
RRESDEF RRESData LoadResource(const char *fileName)
{
    RRESData rres = { 0 };
    
    RRESFileHeader fileHeader;
    RRESInfoHeader infoHeader;
    
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile == NULL) TraceLog(WARNING, "[%s] rRES raylib resource file could not be opened", fileName);
    else
    {
        // Read rres file info header
        fread(&fileHeader.id[0], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[1], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[2], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[3], sizeof(char), 1, rresFile);
        fread(&fileHeader.version, sizeof(short), 1, rresFile);
        fread(&fileHeader.count, sizeof(short), 1, rresFile);

        // Verify "rRES" identifier
        if ((fileHeader.id[0] != 'r') && (fileHeader.id[1] != 'R') && (fileHeader.id[2] != 'E') && (fileHeader.id[3] != 'S'))
        {
            TraceLog(WARNING, "[%s] This is not a valid raylib resource file", fileName);
        }
        else
        {
            // Read first resource info and parameters
            fread(&infoHeader, sizeof(RRESInfoHeader), 1, rresFile);
            
            // Register data type and parameters
            rres.type = infoHeader.dataType;
            rres.param1 = infoHeader.param1;
            rres.param2 = infoHeader.param2;
            rres.param3 = infoHeader.param3;
            rres.param4 = infoHeader.param4;

            // Read resource data block
            void *data = RRES_MALLOC(infoHeader.dataSize);
            fread(data, infoHeader.dataSize, 1, rresFile);

            if (infoHeader.compType == RRES_COMP_DEFLATE)
            {
                void *uncompData = DecompressData(data, infoHeader.dataSize, infoHeader.uncompSize);
                
                rres.data = uncompData;
                
                RRES_FREE(data);
            }
            else rres.data = data;

            if (rres.data != NULL) TraceLog(INFO, "[%s] Resource data loaded successfully", fileName);
        }

        fclose(rresFile);
    }

    return rres;
}

// Load resource from file by id
// NOTE: Returns uncompressed data with parameters, search resource by id
RRESDEF RRESData LoadResourceById(const char *fileName, int rresId)
{
    RRESData rres = { 0 };
    
    RRESFileHeader fileHeader;
    RRESInfoHeader infoHeader;
    
    FILE *rresFile = fopen(fileName, "rb");

    if (rresFile == NULL) TraceLog(WARNING, "[%s] rRES raylib resource file could not be opened", fileName);
    else
    {
        // Read rres file info header
        fread(&fileHeader.id[0], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[1], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[2], sizeof(char), 1, rresFile);
        fread(&fileHeader.id[3], sizeof(char), 1, rresFile);
        fread(&fileHeader.version, sizeof(short), 1, rresFile);
        fread(&fileHeader.count, sizeof(short), 1, rresFile);

        // Verify "rRES" identifier
        if ((fileHeader.id[0] != 'r') && (fileHeader.id[1] != 'R') && (fileHeader.id[2] != 'E') && (fileHeader.id[3] != 'S'))
        {
            TraceLog(WARNING, "[%s] This is not a valid raylib resource file", fileName);
        }
        else
        {
            for (int i = 0; i < fileHeader.count; i++)
            {
                // Read resource info and parameters
                fread(&infoHeader, sizeof(RRESInfoHeader), 1, rresFile);

                if (infoHeader.id == rresId)
                {
                    // Register data type and parameters
                    rres.type = infoHeader.dataType;
                    rres.param1 = infoHeader.param1;
                    rres.param2 = infoHeader.param2;
                    rres.param3 = infoHeader.param3;
                    rres.param4 = infoHeader.param4;

                    // Read resource data block
                    void *data = RRES_MALLOC(infoHeader.dataSize);
                    fread(data, infoHeader.dataSize, 1, rresFile);

                    if (infoHeader.compType == RRES_COMP_DEFLATE)
                    {
                        void *uncompData = DecompressData(data, infoHeader.dataSize, infoHeader.uncompSize);
                        
                        rres.data = uncompData;
                        
                        RRES_FREE(data);
                    }
                    else rres.data = data;

                    if (rres.data != NULL) TraceLog(INFO, "[%s][ID %i] Resource data loaded successfully", fileName, (int)rresId);
                }
                else
                {
                    // Skip required data to read next resource infoHeader
                    fseek(rresFile, infoHeader.dataSize, SEEK_CUR);
                }
            }
            
            if (rres.data == NULL) TraceLog(WARNING, "[%s][ID %i] Requested resource could not be found, wrong id?", fileName, (int)rresId);
        }

        fclose(rresFile);
    }

    return rres;
}

RRESDEF void UnloadResource(RRESData rres)
{
    if (rres.data != NULL) free(rres.data);
}

//----------------------------------------------------------------------------------
// Module specific Functions Definition
//----------------------------------------------------------------------------------

// Data decompression function
// NOTE: Allocated data MUST be freed by user
static void *DecompressData(const unsigned char *data, unsigned long compSize, int uncompSize)
{
    int tempUncompSize;
    void *uncompData;

    // Allocate buffer to hold decompressed data
    uncompData = (mz_uint8 *)RRES_MALLOC((size_t)uncompSize);

    // Check correct memory allocation
    if (uncompData == NULL)
    {
        TraceLog(WARNING, "Out of memory while decompressing data");
    }
    else
    {
        // Decompress data
        tempUncompSize = tinfl_decompress_mem_to_mem(uncompData, (size_t)uncompSize, data, compSize, 1);

        if (tempUncompSize == -1)
        {
            TraceLog(WARNING, "Data decompression failed");
            RRES_FREE(uncompData);
        }

        if (uncompSize != (int)tempUncompSize)
        {
            TraceLog(WARNING, "Expected uncompressed size do not match, data may be corrupted");
            TraceLog(WARNING, " -- Expected uncompressed size: %i", uncompSize);
            TraceLog(WARNING, " -- Returned uncompressed size: %i", tempUncompSize);
        }

        TraceLog(INFO, "Data decompressed successfully from %u bytes to %u bytes", (mz_uint32)compSize, (mz_uint32)tempUncompSize);
    }

    return uncompData;
}


// Some required functions for rres standalone module version
#if defined(RRES_STANDALONE)
// Outputs a trace log message (INFO, ERROR, WARNING)
// NOTE: If a file has been init, output log is written there
void TraceLog(int msgType, const char *text, ...)
{
    va_list args;
    int traceDebugMsgs = 0;

#ifdef DO_NOT_TRACE_DEBUG_MSGS
    traceDebugMsgs = 0;
#endif

    switch (msgType)
    {
        case INFO: fprintf(stdout, "INFO: "); break;
        case ERROR: fprintf(stdout, "ERROR: "); break;
        case WARNING: fprintf(stdout, "WARNING: "); break;
        case DEBUG: if (traceDebugMsgs) fprintf(stdout, "DEBUG: "); break;
        default: break;
    }

    if ((msgType != DEBUG) || ((msgType == DEBUG) && (traceDebugMsgs)))
    {
        va_start(args, text);
        vfprintf(stdout, text, args);
        va_end(args);

        fprintf(stdout, "\n");
    }

    if (msgType == ERROR) exit(1);      // If ERROR message, exit program
}
#endif

#endif // RAYGUI_IMPLEMENTATION


/*
//T LoadResource(const char *rresFileName, int resId);

// ASSUMPTION: rRES files only contain one resource (solution to id requirement...)

// Now, rres file check and data loading can be managed inside each function:
Image LoadImage();          // -> Texture2D
Wave LoadWave()             // -> Sound, Music
const char *LoadText();     // -> Shader, Material

// NOTE: RRESData uses void* data pointer, so we can load to image.data, wave.data, mesh.*, (unsigned char *)

Image LoadImagePro(void *data, int width, int height, int format);
Image LoadImagePro(rres.data, rres.param1, rres.param2, rres.param3);

Mesh LoadMeshEx(int numVertex, float *vData, float *vtData, float *vnData, Color *cData);
Mesh LoadMeshEx(rres.param1, rres.data, rres.data + offset, rres.data + offset*2, rres.data + offset*3);

Shader LoadShaderV(const char *vsText, int vsLength);
Shader LoadShaderV(rres.data, rres.param1);

Wave LoadWaveEx(void *data, int sampleCount, int sampleRate, int sampleSize, int channels);
Wave LoadWaveEx(rres.data, rres.param1, (int)rres.param2, (int)rres.param3, (int)rres.param4);

// Max value for an unsigned short: 65535

// Parameters information depending on resource type (IMAGE, WAVE, MESH, TEXT)

// Image data params
int imgWidth, imgHeight;
char colorFormat, mipmaps;

// Wave data params
short sampleRate, bps;
char channels, reserved;

// Mesh data params
int vertexCount, reserved;
short vertexTypesMask, vertexFormatsMask;

// Text data params
int numChars;
char textFormat, language, charsetCode;
*/