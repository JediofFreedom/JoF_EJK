/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// snd_mem.c: sound caching

#include "snd_local.h"
#include "snd_mp3.h"
#include "snd_ambient.h"
#include "snd_load_profile.h"

#include <string>
#include <climits>
#include <cstdint>
#include <cstring>

#ifdef USE_OPENAL
// Open AL
void S_PreProcessLipSync(sfx_t *sfx);
extern int s_UseOpenAL;
#endif
/*
===============================================================================

WAV loading

===============================================================================
*/

extern sfx_t		s_knownSfx[];
extern	int			s_numSfx;

extern cvar_t		*s_lip_threshold_1;
extern cvar_t		*s_lip_threshold_2;
extern cvar_t		*s_lip_threshold_3;
extern cvar_t		*s_lip_threshold_4;

// Parse only complete, bounded chunks. Invalid metadata never reaches resampling.
wavinfo_t GetWavinfo(const char *name, byte *wav, int wavlength) {
    const wavinfo_t invalid = {};
    if (!wav || wavlength < 12 || std::memcmp(wav, "RIFF", 4) || std::memcmp(wav + 8, "WAVE", 4)) return invalid;
    const auto le16 = [](const byte *p) -> unsigned { return unsigned(p[0]) | (unsigned(p[1]) << 8); };
    const auto le32 = [](const byte *p) -> unsigned {
        return unsigned(p[0]) | (unsigned(p[1]) << 8) | (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
    };
    const size_t riffLength = le32(wav + 4);
    if (riffLength < 4 || riffLength > size_t(wavlength) - 8) return invalid;
    const size_t end = riffLength + 8;
    const byte *format = NULL, *data = NULL;
    size_t dataLength = 0;
    for (size_t offset = 12; offset < end;) {
        if (end - offset < 8) return invalid;
        const size_t size = le32(wav + offset + 4);
        if (size > end - offset - 8) return invalid;
        if (!format && !std::memcmp(wav + offset, "fmt ", 4)) {
            if (size < 16) return invalid;
            format = wav + offset + 8;
        } else if (!data && !std::memcmp(wav + offset, "data", 4)) {
            data = wav + offset + 8;
            dataLength = size;
        }
        if (format && data) break;
        offset += 8 + size;
        if (size & 1) { if (offset == end) break; ++offset; }
    }
    if (!format || !data) return invalid;
    const unsigned bits = le16(format + 14), rate = le32(format + 4), channels = le16(format + 2);
    if (le16(format) != 1 || (bits != 8 && bits != 16) || !rate || rate > 0x7fffffffU ||
        (channels != 1 && channels != 2) || !dataLength || dataLength % (channels * (bits / 8))) {
        Com_Printf("Invalid or unsupported PCM WAV: %s\n", name);
        return invalid;
    }
    wavinfo_t info = {};
    info.format = 1;
    info.channels = channels;
    info.rate = rate;
    info.width = bits / 8;
    info.samples = static_cast<int>(dataLength / info.width);
    info.dataofs = static_cast<int>(data - wav);
    return info;
}

/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
void ResampleSfx (sfx_t *sfx, int iInRate, int iInWidth, byte *pData)
{
	SoundLoadProfile::Scope profile(SoundLoadProfile::Resample);
	const int inputCount = sfx->iSoundLengthInSamples;
	sfx->iSoundLengthInSamples = 0;
	sfx->pSoundData = NULL;
	sfx->fVolRange = 0;
	if (!pData || inputCount <= 0 || iInRate <= 0 || dma.speed <= 0 ||
		(iInWidth != 1 && iInWidth != 2)) return;

	// Retain the existing nearest-sample conversion and output lengths. Check
	// the float result before converting it or doubling the allocation size.
	const float stepScale = static_cast<float>(iInRate) / dma.speed;
	const float outputCount = inputCount / stepScale;
	if (!(outputCount >= 1) || outputCount > static_cast<double>(INT_MAX / 2)) return;
	sfx->iSoundLengthInSamples = static_cast<int>(outputCount);
	sfx->pSoundData = reinterpret_cast<short *>(SND_malloc(sfx->iSoundLengthInSamples * 2, sfx));
	if (!sfx->pSoundData) { sfx->iSoundLengthInSamples = 0; return; }

	// These common PCM cases do not need a phase accumulator or a per-sample
	// bounds clamp. Preserve the computed output length: very large sample counts
	// can round during the legacy float calculation, so those keep the old loop.
	const bool sameRate = iInRate == dma.speed && sfx->iSoundLengthInSamples == inputCount;
	const bool doubleRate = int64_t(iInRate) * 2 == dma.speed &&
		int64_t(inputCount) * 2 == sfx->iSoundLengthInSamples;
	if (s_resampleFast && s_resampleFast->integer && (sameRate || doubleRate)) {
		int magnitude = 0;
		const int copies = doubleRate ? 2 : 1;
		if (iInWidth == 2) {
			for (int i = 0; i < inputCount; ++i) {
				short value;
				std::memcpy(&value, pData + size_t(i) * 2, sizeof(value));
				const int sample = LittleShort(value);
				sfx->pSoundData[i * copies] = static_cast<short>(sample);
				if (doubleRate) sfx->pSoundData[i * copies + 1] = static_cast<short>(sample);
				const int absolute = sample < 0 ? -sample : sample;
				if (magnitude < absolute) magnitude = absolute;
			}
		} else {
			for (int i = 0; i < inputCount; ++i) {
				const int sample = (int(pData[i]) - 128) * 256;
				sfx->pSoundData[i * copies] = static_cast<short>(sample);
				if (doubleRate) sfx->pSoundData[i * copies + 1] = static_cast<short>(sample);
				const int absolute = sample < 0 ? -sample : sample;
				if (magnitude < absolute) magnitude = absolute;
			}
		}
		sfx->fVolRange = magnitude >> 8;
		return;
	}

	// A 32-bit 8-bit-fraction accumulator wrapped after 2^24 input samples,
	// replaying the beginning of long sounds. Its step still determines normal
	// conversion; the rational fallback handles rates below 1/256 output rate.
	uint64_t sampleFraction = 0;
	const uint64_t fractionStep = static_cast<uint64_t>(stepScale * 256.0f);
	for (int i = 0; i < sfx->iSoundLengthInSamples; ++i) {
		const uint64_t source = fractionStep ? sampleFraction >> 8 : uint64_t(i) * iInRate / dma.speed;
		const int sourceSample = static_cast<int>(source < uint64_t(inputCount) ? source : inputCount - 1);
		sampleFraction += fractionStep;
		int sample;
		if (iInWidth == 2) {
			short value;
			std::memcpy(&value, pData + size_t(sourceSample) * 2, sizeof(value));
			sample = LittleShort(value);
		} else sample = (int(pData[sourceSample]) - 128) * 256;
		sfx->pSoundData[i] = static_cast<short>(sample);
		if (sample < 0) sample = -sample;
		if (sfx->fVolRange < (sample >> 8)) sfx->fVolRange = sample >> 8;
	}
}


//=============================================================================


void S_LoadSound_Finalize(wavinfo_t	*info, sfx_t *sfx, byte *data)
{
	sfx->eSoundCompressionMethod = ct_16;
	sfx->iSoundLengthInSamples	 = info->samples;
	ResampleSfx( sfx, info->rate, info->width, data + info->dataofs );
}





// maybe I'm re-inventing the wheel, here, but I can't see any functions that already do this, so...
//
char *Filename_WithoutPath(const char *psFilename)
{
	static char sString[MAX_QPATH];	// !!
	const char *p = strrchr(psFilename,'\\');

  	if (!p++)
		p=psFilename;

	strcpy(sString,p);

	return sString;

}

// returns (eg) "\dir\name" for "\dir\name.bmp"
//
char *Filename_WithoutExt(const char *psFilename)
{
	static char sString[MAX_QPATH];	// !

	strcpy(sString,psFilename);

	char *p = strrchr(sString,'.');
	char *p2= strrchr(sString,'\\');

	// special check, make sure the first suffix we found from the end wasn't just a directory suffix (eg on a path'd filename with no extension anyway)
	//
	if (p && (p2==0 || (p2 && p>p2)))
		*p=0;

	return sString;

}



int iFilesFound;
int iFilesUpdated;
int iErrors;
qboolean qbForceRescan;
qboolean qbForceStereo;
std::string strErrors;

void R_CheckMP3s( const char *psDir )
{
//	Com_Printf(va("Scanning Dir: %s\n",psDir));
	Com_Printf(".");	// stops useful info scrolling off screen

	char	**sysFiles, **dirFiles;
	int		numSysFiles, i, numdirs;

	dirFiles = FS_ListFiles( psDir, "/", &numdirs);
	if (numdirs > 2)
	{
		for (i=2;i<numdirs;i++)
		{
			char	sDirName[MAX_QPATH];
			sprintf(sDirName, "%s\\%s", psDir, dirFiles[i]);
			R_CheckMP3s(sDirName);
		}
	}

	sysFiles = FS_ListFiles( psDir, ".mp3", &numSysFiles );
	for(i=0; i<numSysFiles; i++)
	{
		char	sFilename[MAX_QPATH];
		sprintf(sFilename,"%s\\%s", psDir, sysFiles[i]);

		Com_Printf("%sFound file: %s",!i?"\n":"",sFilename);

		iFilesFound++;

		// read it in...
		//
		byte *pbData = NULL;
		int iSize = FS_ReadFile( sFilename, (void **)&pbData);

		if (pbData)
		{
			id3v1_1* pTAG;

			// do NOT check 'qbForceRescan' here as an opt, because we need to actually fill in 'pTAG' if there is one...
			//
			qboolean qbTagNeedsUpdating = (/* qbForceRescan || */ !MP3_ReadSpecialTagInfo(pbData, iSize, &pTAG))?qtrue:qfalse;

			if (pTAG == NULL || qbTagNeedsUpdating || qbForceRescan)
			{
				Com_Printf(" ( Updating )\n");

				// I need to scan this file to get the volume...
				//
				// For EF1 I used a temp sfx_t struct, but I can't do that now with this new alloc scheme,
				//	I have to ask for it legally, so I'll keep re-using one, and restoring it's name after use.
				//	(slightly dodgy, but works ok if no-one else changes stuff)
				//
				//sfx_t SFX = {0};
				extern sfx_t *S_FindName( const char *name );
				//
				static sfx_t *pSFX = NULL;
				const char sReservedSFXEntrynameForMP3[] = "reserved_for_mp3";	// ( strlen() < MAX_QPATH )

				if (pSFX == NULL)	// once only
				{
					pSFX = S_FindName(sReservedSFXEntrynameForMP3);	// always returns, else ERR_FATAL
				}

				if (MP3_IsValid(sFilename,pbData, iSize, qbForceStereo))
				{
					wavinfo_t info;

					int iRawPCMDataSize = MP3_GetUnpackedSize(sFilename, pbData, iSize, qtrue, qbForceStereo);

					if (iRawPCMDataSize)	// should always be true, unless file is fucked, in which case, stop this conversion process
					{
						float fMaxVol = 128;	// any old default
						int iActualUnpackedSize = iRawPCMDataSize;	// default, override later if not doing music

						if (!qbForceStereo)	// no point for stereo files, which are for music and therefore no lip-sync
						{
							byte *pbUnpackBuffer = (byte *) Z_Malloc( iRawPCMDataSize+10, TAG_TEMP_WORKSPACE, qfalse );	// won't return if fails

							iActualUnpackedSize = MP3_UnpackRawPCM( sFilename, pbData, iSize, pbUnpackBuffer );
							if (iActualUnpackedSize != iRawPCMDataSize)
							{
								Com_Error(ERR_DROP, "******* Whoah! MP3 %s unpacked to %d bytes, but size calc said %d!\n",sFilename,iActualUnpackedSize,iRawPCMDataSize);
							}

							// fake up a WAV structure so I can use the other post-load sound code such as volume calc for lip-synching
							//
							MP3_FakeUpWAVInfo( sFilename, pbData, iSize, iActualUnpackedSize,
												// these params are all references...
												info.format, info.rate, info.width, info.channels, info.samples, info.dataofs
												);

							S_LoadSound_Finalize(&info, pSFX, pbUnpackBuffer);	// all this just for lipsynch. Oh well.

							fMaxVol = pSFX->fVolRange;

							// free sfx->data...
							//
							{
								#ifndef INT_MIN
								#define INT_MIN     (-2147483647 - 1) /* minimum (signed) int value */
								#endif
								//
								pSFX->iLastTimeUsed = INT_MIN;		// force this to be oldest sound file, therefore disposable...
								pSFX->bInMemory = qtrue;
								SND_FreeOldestSound();		// ... and do the disposal

								// now set our temp SFX struct back to default name so nothing else accidentally uses it...
								//
								strcpy(pSFX->sSoundName, sReservedSFXEntrynameForMP3);
								pSFX->bDefaultSound = qfalse;
							}

//							Com_OPrintf("File: \"%s\"   MaxVol %f\n",sFilename,pSFX->fVolRange);

							// other stuff...
							//
							Z_Free(pbUnpackBuffer);
						}

						// well, time to update the file now...
						//
						fileHandle_t f = FS_FOpenFileWrite( sFilename );
						if (f)
						{
							// write the file back out, but omitting the tag if there was one...
							//
							int iWritten = FS_Write(pbData, iSize-(pTAG?sizeof(*pTAG):0), f);

							if (iWritten)
							{
								// make up a new tag if we didn't find one in the original file...
								//
								id3v1_1 TAG;
								if (!pTAG)
								{
									pTAG = &TAG;
									memset(&TAG,0,sizeof(TAG));
									strncpy(pTAG->id,"TAG",3);
								}

								strncpy(pTAG->title,	Filename_WithoutPath(Filename_WithoutExt(sFilename)), sizeof(pTAG->title));
								strncpy(pTAG->artist,	"Raven Software",						sizeof(pTAG->artist)	);
								strncpy(pTAG->year,		"2002",									sizeof(pTAG->year)		);
								strncpy(pTAG->comment,	va("%s %g",sKEY_MAXVOL,fMaxVol),		sizeof(pTAG->comment)	);
								strncpy(pTAG->album,	va("%s %d",sKEY_UNCOMP,iActualUnpackedSize),sizeof(pTAG->album)	);

								if (FS_Write( pTAG, sizeof(*pTAG), f ))	// NZ = success
								{
									iFilesUpdated++;
								}
								else
								{
									Com_Printf("*********** Failed write to file \"%s\"!\n",sFilename);
									iErrors++;
									strErrors += va("Failed to write: \"%s\"\n",sFilename);
								}
							}
							else
							{
								Com_Printf("*********** Failed write to file \"%s\"!\n",sFilename);
								iErrors++;
								strErrors += va("Failed to write: \"%s\"\n",sFilename);
							}
							FS_FCloseFile( f );
						}
						else
						{
							Com_Printf("*********** Failed to re-open for write \"%s\"!\n",sFilename);
							iErrors++;
							strErrors += va("Failed to re-open for write: \"%s\"\n",sFilename);
						}
					}
					else
					{
						Com_Error(ERR_DROP, "******* This MP3 should be deleted: \"%s\"\n",sFilename);
					}
				}
				else
				{
					Com_Printf("*********** File was not a valid MP3!: \"%s\"\n",sFilename);
					iErrors++;
					strErrors += va("Not game-legal MP3 format: \"%s\"\n",sFilename);
				}
			}
			else
			{
				Com_Printf(" ( OK )\n");
			}

			FS_FreeFile( pbData );
		}
	}
	FS_FreeFileList( sysFiles );
	FS_FreeFileList( dirFiles );
}

// this console-function is for development purposes, and makes sure that sound/*.mp3 /s have tags in them
//	specifying stuff like their max volume (and uncompressed size) etc...
//
void S_MP3_CalcVols_f( void )
{
	char sStartDir[MAX_QPATH] = {"sound"};
	const char sUsage[] = "Usage: mp3_calcvols [-rescan] <startdir>\ne.g. mp3_calcvols sound/chars";

	if (Cmd_Argc() == 1 || Cmd_Argc()>4)	// 3 optional arguments
	{
		Com_Printf(sUsage);
		return;
	}

	S_StopAllSounds();


	qbForceRescan = qfalse;
	qbForceStereo = qfalse;
	iFilesFound		= 0;
	iFilesUpdated	= 0;
	iErrors			= 0;
	strErrors		= "";

	for (int i=1; i<Cmd_Argc(); i++)
	{
		if (Cmd_Argv(i)[0] == '-')
		{
			if (!Q_stricmp(Cmd_Argv(i),"-rescan"))
			{
				qbForceRescan = qtrue;
			}
			else
			if (!Q_stricmp(Cmd_Argv(i),"-stereo"))
			{
				qbForceStereo = qtrue;
			}
			else
			{
				// unknown switch...
				//
				Com_Printf(sUsage);
				return;
			}
			continue;
		}
		strcpy(sStartDir,Cmd_Argv(i));
	}

	Com_Printf(va("Starting Scan for Updates in Dir: %s\n",sStartDir));
	R_CheckMP3s( sStartDir );

	Com_Printf("\n%d files found/scanned, %d files updated      ( %d errors total)\n",iFilesFound,iFilesUpdated,iErrors);

	if (iErrors)
	{
		Com_Printf("\nBad Files:\n%s\n",strErrors.c_str());
	}
}





// adjust filename for foreign languages and WAV/MP3 issues.
//
// returns qfalse if failed to load, else fills in *pData
//
extern	cvar_t	*com_buildScript;
static qboolean S_LoadSound_FileLoadAndNameAdjuster(char *psFilename, byte **pData, int *piSize, int iNameStrlen)
{
	SoundLoadProfile::Scope profile(SoundLoadProfile::Source);
	char *psVoice = strstr(psFilename,"chars");
	if (psVoice)
	{
		// cache foreign voices...
		//
		if (com_buildScript->integer)
		{
			fileHandle_t hFile;
			//German
			strncpy(psVoice,"chr_d",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				strcpy(&psFilename[iNameStrlen-3],"mp3");		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			strcpy(&psFilename[iNameStrlen-3],"wav");	//put it back to wav

			//French
			strncpy(psVoice,"chr_f",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				strcpy(&psFilename[iNameStrlen-3],"mp3");		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			strcpy(&psFilename[iNameStrlen-3],"wav");	//put it back to wav

			//Spanish
			strncpy(psVoice,"chr_e",5);	// same number of letters as "chars"
			FS_FOpenFileRead(psFilename, &hFile, qfalse);		//cache the wav
			if (!hFile)
			{
				strcpy(&psFilename[iNameStrlen-3],"mp3");		//not there try mp3
				FS_FOpenFileRead(psFilename, &hFile, qfalse);	//cache the mp3
			}
			if (hFile)
			{
				FS_FCloseFile(hFile);
			}
			strcpy(&psFilename[iNameStrlen-3],"wav");	//put it back to wav

			strncpy(psVoice,"chars",5);	//put it back to chars
		}

		// account for foreign voices...
		//
		extern cvar_t* s_language;
		if ( s_language ) {
				 if ( !Q_stricmp( "DEUTSCH", s_language->string ) )
				strncpy( psVoice, "chr_d", 5 );	// same number of letters as "chars"
			else if ( !Q_stricmp( "FRANCAIS", s_language->string ) )
				strncpy( psVoice, "chr_f", 5 );	// same number of letters as "chars"
			else if ( !Q_stricmp( "ESPANOL", s_language->string ) )
				strncpy( psVoice, "chr_e", 5 );	// same number of letters as "chars"
			else
				psVoice = NULL;
		}
		else
		{
			psVoice = NULL;	// use this ptr as a flag as to whether or not we substituted with a foreign version
		}
	}

	*piSize = FS_ReadFile( psFilename, (void **)pData );	// try WAV
	if ( !*pData ) {
		psFilename[iNameStrlen-3] = 'm';
		psFilename[iNameStrlen-2] = 'p';
		psFilename[iNameStrlen-1] = '3';
		*piSize = FS_ReadFile( psFilename, (void **)pData );	// try MP3

		if ( !*pData )
		{
			//hmmm, not found, ok, maybe we were trying a foreign noise ("arghhhhh.mp3" that doesn't matter?) but it
			// was missing?   Can't tell really, since both types are now in sound/chars. Oh well, fall back to English for now...

			if (psVoice)	// were we trying to load foreign?
			{
				// yep, so fallback to re-try the english...
				//
#ifndef FINAL_BUILD
				Com_Printf(S_COLOR_YELLOW "Foreign file missing: \"%s\"! (using English...)\n",psFilename);
#endif

				strncpy(psVoice,"chars",5);

				psFilename[iNameStrlen-3] = 'w';
				psFilename[iNameStrlen-2] = 'a';
				psFilename[iNameStrlen-1] = 'v';
				*piSize = FS_ReadFile( psFilename, (void **)pData );	// try English WAV
				if ( !*pData )
				{
					psFilename[iNameStrlen-3] = 'm';
					psFilename[iNameStrlen-2] = 'p';
					psFilename[iNameStrlen-1] = '3';
					*piSize = FS_ReadFile( psFilename, (void **)pData );	// try English MP3
				}
			}

			if (!*pData)
			{
				return qfalse;	// sod it, give up...
			}
		}
	}

	return qtrue;
}

// returns qtrue if this dir is allowed to keep loaded MP3s, else qfalse if they should be WAV'd instead...
//
// note that this is passed the original, un-language'd name
//
// (I was going to remove this, but on kejim_post I hit an assert because someone had got an ambient sound when the
//	perimter fence goes online that was an MP3, then it tried to get added as looping. Presumably it sounded ok or
//	they'd have noticed, but we therefore need to stop other levels using those. "sound/ambience" I can check for,
//	but doors etc could be anything. Sigh...)
//
#define SOUND_CHARS_DIR "sound/chars/"
#define SOUND_CHARS_DIR_LENGTH 12 // strlen( SOUND_CHARS_DIR )
static qboolean S_LoadSound_DirIsAllowedToKeepMP3s(const char *psFilename)
{
	if ( Q_stricmpn( psFilename, SOUND_CHARS_DIR, SOUND_CHARS_DIR_LENGTH ) == 0 )
		return qtrue;	// found a dir that's allowed to keep MP3s

	return qfalse;
}

/*
==============
S_LoadSound

The filename may be different than sfx->name in the case
of a forced fallback of a player specific sound	(or of a wav/mp3 substitution now -Ste)
==============
*/
qboolean gbInsideLoadSound = qfalse;
static qboolean S_LoadSound_Actual( sfx_t *sfx )
{
	byte	*data;
	wavinfo_t	info;
	int		size;
	char	*psExt;
	char	sLoadName[MAX_QPATH];

	int		len = strlen(sfx->sSoundName);
	if (len<5)
	{
		return qfalse;
	}

	// player specific sounds are never directly loaded...
	//
	if ( sfx->sSoundName[0] == '*') {
		return qfalse;
	}
	// make up a local filename to try wav/mp3 substitutes...
	//
	Q_strncpyz(sLoadName, sfx->sSoundName, sizeof(sLoadName));
	Q_strlwr( sLoadName );
	//
	// Ensure name has an extension (which it must have, but you never know), and get ptr to it...
	//
	psExt = &sLoadName[strlen(sLoadName)-4];
	if (*psExt != '.')
	{
		//Com_Printf( "WARNING: soundname '%s' does not have 3-letter extension\n",sLoadName);
		COM_DefaultExtension(sLoadName,sizeof(sLoadName),".wav");	// so psExt below is always valid
		psExt = &sLoadName[strlen(sLoadName)-4];
		len = strlen(sLoadName);
	}

	if (!S_LoadSound_FileLoadAndNameAdjuster(sLoadName, &data, &size, len))
	{
		return qfalse;
	}

	SND_TouchSFX(sfx);

//=========
	if (Q_stricmpn(psExt,".mp3",4)==0)
	{
		// load MP3 file instead...
		//
		if (MP3_IsValid(sLoadName,data, size, qfalse))
		{
			int iRawPCMDataSize = MP3_GetUnpackedSize(sLoadName,data,size,qfalse,qfalse);

			if (S_LoadSound_DirIsAllowedToKeepMP3s(sfx->sSoundName)	// NOT sLoadName, this uses original un-languaged name
				&&
				MP3Stream_InitFromFile(sfx, data, size, sLoadName, iRawPCMDataSize + 2304 /* + 1 MP3 frame size, jic */,qfalse)
				)
			{
//				Com_DPrintf("(Keeping file \"%s\" as MP3)\n",sLoadName);

#ifdef USE_OPENAL
				if (s_UseOpenAL)
				{
					SoundLoadProfile::Scope profile(SoundLoadProfile::OpenAL);
					// Create space for lipsync data (4 lip sync values per streaming AL buffer)
					if ((strstr(sfx->sSoundName, "chars")) || (strstr(sfx->sSoundName, "CHARS")))
						sfx->lipSyncData = (char *)Z_Malloc(16, TAG_SND_RAWDATA, qfalse);
					else
						sfx->lipSyncData = NULL;
				}
#endif
			}
			else
			{
				// small file, not worth keeping as MP3 since it would increase in size (with MP3 header etc)...
				//
				Com_DPrintf("S_LoadSound: Unpacking MP3 file(%i) \"%s\" to wav(%i).\n",size,sLoadName,iRawPCMDataSize);
				//
				// unpack and convert into WAV...
				//
				{
					byte *pbUnpackBuffer = (byte *) Z_Malloc( iRawPCMDataSize+10 +2304 /* <g> */, TAG_TEMP_WORKSPACE, qfalse );	// won't return if fails

					{
						int iResultBytes = MP3_UnpackRawPCM( sLoadName, data, size, pbUnpackBuffer, qfalse );

						if (iResultBytes!= iRawPCMDataSize){
							Com_Printf(S_COLOR_YELLOW"**** MP3 %s final unpack size %d different to previous value %d\n",sLoadName,iResultBytes,iRawPCMDataSize);
							//assert (iResultBytes == iRawPCMDataSize);
						}


						// fake up a WAV structure so I can use the other post-load sound code such as volume calc for lip-synching
						//
						// (this is a bit crap really, but it lets me drop through into existing code)...
						//
						MP3_FakeUpWAVInfo( sLoadName, data, size, iResultBytes,
											// these params are all references...
											info.format, info.rate, info.width, info.channels, info.samples, info.dataofs,
											qfalse
										);

						S_LoadSound_Finalize(&info,sfx,pbUnpackBuffer);

#ifdef Q3_BIG_ENDIAN
						// the MP3 decoder returns the samples in the correct endianness, but ResampleSfx byteswaps them,
						// so we have to swap them again...
						sfx->fVolRange	= 0;

						for (int i = 0; i < sfx->iSoundLengthInSamples; i++)
						{
							sfx->pSoundData[i] = LittleShort(sfx->pSoundData[i]);
							// C++11 defines double abs(short) which is not what we want here,
							// because double >> int is not defined. Force interpretation as int
							if (sfx->fVolRange < (abs(static_cast<int>(sfx->pSoundData[i])) >> 8))
							{
								sfx->fVolRange = abs(static_cast<int>(sfx->pSoundData[i])) >> 8;
							}
						}
#endif

						// Open AL
#ifdef USE_OPENAL
						if (s_UseOpenAL)
						{
							SoundLoadProfile::Scope profile(SoundLoadProfile::OpenAL);
							if ((strstr(sfx->sSoundName, "chars")) || (strstr(sfx->sSoundName, "CHARS")))
							{
								sfx->lipSyncData = (char *)Z_Malloc((sfx->iSoundLengthInSamples / 1000) + 1, TAG_SND_RAWDATA, qfalse);
								S_PreProcessLipSync(sfx);
							}
							else
								sfx->lipSyncData = NULL;

							// Clear Open AL Error state
							alGetError();

							// Generate AL Buffer
                            ALuint Buffer;
							alGenBuffers(1, &Buffer);
							if (alGetError() == AL_NO_ERROR)
							{
								// Copy audio data to AL Buffer
								alBufferData(Buffer, AL_FORMAT_MONO16, sfx->pSoundData, sfx->iSoundLengthInSamples*2, 22050);
								if (alGetError() == AL_NO_ERROR)
								{
									sfx->Buffer = Buffer;
									Z_Free(sfx->pSoundData);
									sfx->pSoundData = NULL;
								}
							}
						}
#endif

						Z_Free(pbUnpackBuffer);
					}
				}
			}
		}
		else
		{
			// MP3_IsValid() will already have printed any errors via Com_Printf at this point...
			//
			FS_FreeFile (data);
			return qfalse;
		}
	}
	else
	{
		// loading a WAV, presumably...

//=========

		info = GetWavinfo( sLoadName, data, size );
		if ( info.channels != 1 ) {
			Com_Printf ("%s is a stereo wav file\n", sLoadName);
			FS_FreeFile (data);
			return qfalse;
		}

/*		if ( info.width == 1 ) {
			Com_Printf(S_COLOR_YELLOW "WARNING: %s is a 8 bit wav file\n", sLoadName);
		}

		if ( info.rate != 22050 ) {
			Com_Printf(S_COLOR_YELLOW "WARNING: %s is not a 22kHz wav file\n", sLoadName);
		}
*/

		sfx->eSoundCompressionMethod = ct_16;
		sfx->iSoundLengthInSamples	 = info.samples;
		sfx->pSoundData = NULL;
		ResampleSfx( sfx, info.rate, info.width, data + info.dataofs );
		if (!sfx->pSoundData) {
			FS_FreeFile(data);
			return qfalse;
		}

		// Open AL
#ifdef USE_OPENAL
		if (s_UseOpenAL)
		{
			SoundLoadProfile::Scope profile(SoundLoadProfile::OpenAL);
			if ((strstr(sfx->sSoundName, "chars")) || (strstr(sfx->sSoundName, "CHARS")))
			{
				sfx->lipSyncData = (char *)Z_Malloc((sfx->iSoundLengthInSamples / 1000) + 1, TAG_SND_RAWDATA, qfalse);
				S_PreProcessLipSync(sfx);
			}
			else
				sfx->lipSyncData = NULL;

			// Clear Open AL Error State
			alGetError();

			// Generate AL Buffer
            ALuint Buffer;
			alGenBuffers(1, &Buffer);
			if (alGetError() == AL_NO_ERROR)
			{
				// Copy audio data to AL Buffer
				alBufferData(Buffer, AL_FORMAT_MONO16, sfx->pSoundData, sfx->iSoundLengthInSamples*2, 22050);
				if (alGetError() == AL_NO_ERROR)
				{
					// Store AL Buffer in sfx struct, and release sample data
					sfx->Buffer = Buffer;
					Z_Free(sfx->pSoundData);
					sfx->pSoundData = NULL;
				}
			}
		}
#endif

	}

	FS_FreeFile( data );

	return qtrue;
}


// wrapper function for above so I can guarantee that we don't attempt any audio-dumping during this call because
//	of a z_malloc() fail recovery...
//
qboolean S_LoadSound( sfx_t *sfx )
{
	SoundLoadProfile::Scope profile(SoundLoadProfile::Total);
	gbInsideLoadSound = qtrue;	// !!!!!!!!!!!!!

		qboolean bReturn = S_LoadSound_Actual( sfx );

	gbInsideLoadSound = qfalse;	// !!!!!!!!!!!!!

	return bReturn;
}

#ifdef USE_OPENAL
/*
	Precalculate the lipsync values for the whole sample
*/
void S_PreProcessLipSync(sfx_t *sfx)
{
	int i, j;
	int sample;
	int sampleTotal = 0;

	j = 0;
	for (i = 0; i < sfx->iSoundLengthInSamples; i += 100)
	{
		sample = LittleShort(sfx->pSoundData[i]);

		sample = sample >> 8;
		sampleTotal += sample * sample;
		if (((i + 100) % 1000) == 0)
		{
			sampleTotal /= 10;

			if (sampleTotal < sfx->fVolRange *  s_lip_threshold_1->value)
			{
				// tell the scripts that are relying on this that we are still going, but actually silent right now.
				sample = -1;
			}
			else if (sampleTotal < sfx->fVolRange * s_lip_threshold_2->value)
				sample = 1;
			else if (sampleTotal < sfx->fVolRange * s_lip_threshold_3->value)
				sample = 2;
			else if (sampleTotal < sfx->fVolRange * s_lip_threshold_4->value)
				sample = 3;
			else
				sample = 4;

			sfx->lipSyncData[j] = sample;
			j++;

			sampleTotal = 0;
		}
	}

	if ((i % 1000) == 0)
		return;

	i -= 100;
	i = i % 1000;
	i = i / 100;
	// Process last < 1000 samples
	if (i != 0)
		sampleTotal /= i;
	else
		sampleTotal = 0;

	if (sampleTotal < sfx->fVolRange * s_lip_threshold_1->value)
	{
		// tell the scripts that are relying on this that we are still going, but actually silent right now.
		sample = -1;
	}
	else if (sampleTotal < sfx->fVolRange * s_lip_threshold_2->value)
		sample = 1;
	else if (sampleTotal < sfx->fVolRange * s_lip_threshold_3->value)
		sample = 2;
	else if (sampleTotal < sfx->fVolRange * s_lip_threshold_4->value)
		sample = 3;
	else
		sample = 4;

	sfx->lipSyncData[j] = sample;
}
#endif
