/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2015, Live Networks, Inc.  All rights reserved
// A subclass of "RTSPServer" that creates "ServerMediaSession"s on demand,
// based on whether or not the specified stream name exists as a file
// Implementation

#include "DynamicRTSPServer.hh"
#include <liveMedia.hh>
#include <string.h>

#include "rtsp_util_def.h"
#include "AmbaNetFifo.h"
#include "AmbaVideoServerMediaSubsession.h"
#include "AmbaAudioServerMediaSubsession.h"
#include "MPEG2TransportStreamMultiplexor.hh"
//=============================================================================
//                Macro Definition
//=============================================================================
////////// DynamicRTSPServer //////////
DynamicRTSPServer*
DynamicRTSPServer::createNew(UsageEnvironment& env, Port ourPort,
                             UserAuthenticationDatabase* authDatabase,
                             unsigned reclamationTestSeconds)
{
    int ourSocket = setUpOurSocket(env, ourPort);
    if (ourSocket == -1) return NULL;

    return new DynamicRTSPServer(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds);
}

DynamicRTSPServer::DynamicRTSPServer(UsageEnvironment& env, int ourSocket,
                                     Port ourPort,
                                     UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
    : RTSPServerSupportingHTTPStreaming(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds),
      fStream_reader(NULL)
{
    stream_reader_init_info_t   init_info = {0};

    StreamReader_CreateHandle(&fStream_reader, &init_info);
}

DynamicRTSPServer::~DynamicRTSPServer()
{
    StreamReader_DestroyHandle(&fStream_reader);
}

static int
_check_media_configuration(
    stream_reader_t *pStream_reader,
    char const      *streamName,
    Boolean         *pHave_amba_audio,
    Boolean         *pHave_amba_video)
{
    int                             result = 0;
    AMBA_NETFIFO_MOVIE_INFO_CFG_s   movie_info = {0};

    *pHave_amba_audio = False;
    *pHave_amba_video = False;

    do{
        if( streamName == NULL )
        {
            // live
            AMBA_NETFIFO_MEDIA_STREAMID_LIST_s  stream_list = {0};

            result = AmbaNetFifo_GetMediaStreamIDList(&stream_list);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaStreamIDList()\n");
                break;
            }

            if( stream_list.Amount < 1 )
            {
                err_msg("There is no valid stream. Maybe video record does not started yet.\n");
                result = 1;
                break;
            }

            result = AmbaNetFifo_GetMediaInfo(stream_list.StreamID_List[stream_list.Amount-1], &movie_info);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaInfo()\n");
                break;
            }
        }
        else
        {
            // palyback
            int                                 i;
            u_int32_t                           cur_stream_tag = 0;
            AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_in = {0,{0}};
            AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_out = {0,{0}};

            if( pStream_reader->playback_cnt >= STREAM_PLAYBACK_STATUS_NUM )
            {
                err_msg("Out playback file support conunt !!\n");
                break;
            }

            cur_stream_tag = calculateCRC((u_int8_t const*)streamName, (unsigned)strlen((const char*)streamName));
            for(i = 0; i < STREAM_PLAYBACK_STATUS_NUM; i++)
            {
                if( pStream_reader->playback_status[i].stream_tag == cur_stream_tag )
                {
                    cur_stream_tag = 0;
                    err_msg("Playback file only support 1 client !!\n");
                    break;
                }
            }

            if( cur_stream_tag == 0 )
                break;

            param_in.OP = STREAM_READER_CMD_PLAYBACK_OPEN;
            snprintf((char*)param_in.Param, 128, "%s", streamName);
            result = AmbaNetFifo_PlayBack_OP(&param_in, &param_out);
            if( result < 0 )
            {
                err_msg("fail to do AmbaNetFifo_PlayBack_OP(0x%08x), %d\n", param_in.OP, result);
                break;
            }

            result = AmbaNetFifo_GetMediaInfo(param_out.OP, &movie_info);
            if( result < 0 )
            {
                err_msg("Fail to do AmbaNetFifo_GetMediaInfo()\n");
                break;
            }
        }

        if( movie_info.nTrack )
        {
            int     i;
            for(i = 0; i < movie_info.nTrack; i++)
            {
                switch( movie_info.Track[i].nTrackType )
                {
                    case AMBA_NETFIFO_MEDIA_TRACK_TYPE_VIDEO:
                        *pHave_amba_video = True;
                        break;
                    case AMBA_NETFIFO_MEDIA_TRACK_TYPE_AUDIO:
                        *pHave_amba_audio = True;
                        break;
                    default:
                        break;
                }
            }
        }

        {
            AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_in = {0,{0}};
            AMBA_NETFIFO_PLAYBACK_OP_PARAM_s    param_out = {0,{0}};

            param_in.OP = STREAM_READER_CMD_PLAYBACK_STOP;
            AmbaNetFifo_PlayBack_OP(&param_in, &param_out);
        }
    }while(0);

    return 0;
}

static ServerMediaSession* createNewSMS(UsageEnvironment& env,
                                        stream_reader_t *pStream_reader,
                                        Boolean has_amba_audio, Boolean has_amba_video,
                                        char const* fileName, FILE* fid); // forward

ServerMediaSession* DynamicRTSPServer
::lookupServerMediaSession(char const* streamName, Boolean isFirstLookupInSession)
{
    // First, check whether the specified "streamName" exists as a local file:
    FILE    *fid = (streamName[0] == '/') ? fopen(streamName, "rb") : NULL;
    Boolean fileExists = fid != NULL;

    dbg_msg("***** get streamName= %s, fid=%p\n", streamName, fid);

    // Next, check whether we already have a "ServerMediaSession" for this file:
    ServerMediaSession* sms = RTSPServer::lookupServerMediaSession(streamName);
    Boolean smsExists = sms != NULL;

    // Handle the four possibilities for "fileExists" and "smsExists":
    if (!fileExists) {
#if defined(CFG_AMBA_STREAM)
        if( 0 == strcasecmp(LIVE_VEIW_NAME, streamName) )
        {
            _check_media_configuration(fStream_reader, NULL, &fHave_amba_audio, &fHave_amba_video);

            // no source can get but media session exist.
            if( smsExists == True &&
                (fHave_amba_video == False && fHave_amba_audio == False) )
            {
                removeServerMediaSession(sms);
                return NULL;
            }

            if( smsExists == False &&
                (fHave_amba_video || fHave_amba_audio) )
            {
                char const* descStr = LIVE_VEIW_NAME ", streamed by the Media Server";
                sms = ServerMediaSession::createNew(envir(), streamName, streamName, descStr);
                if( sms ) {
                    OutPacketBuffer::maxSize = 300000;
                    if( fHave_amba_video )
                        sms->addSubsession(AmbaVideoServerMediaSubsession::createNew(envir(), fStream_reader, NULL, True));
                    if( fHave_amba_audio )
                        sms->addSubsession(AmbaAudioServerMediaSubsession::createNew(envir(), fStream_reader, NULL, True));
                    if( fHave_amba_video || fHave_amba_audio )
                        addServerMediaSession(sms);
                }
            }
            return sms;
        }
#endif
        if (smsExists) {
            // "sms" was created for a file that no longer exists. Remove it:
            removeServerMediaSession(sms);
            sms = NULL;
        }
        return NULL;
    } else {
        if (smsExists && isFirstLookupInSession) {
            // Remove the existing "ServerMediaSession" and create a new one, in case the underlying
            // file has changed in some way:
            removeServerMediaSession(sms);
            sms = NULL;
        }

        if (sms == NULL) {
#if defined(CFG_AMBA_STREAM)
            _check_media_configuration(fStream_reader, streamName, &fHave_amba_audio, &fHave_amba_video);
#endif
            sms = createNewSMS(envir(), fStream_reader, fHave_amba_audio, fHave_amba_video, streamName, fid);
            addServerMediaSession(sms);
        }

        fclose(fid);
        return sms;
    }
}

// Special code for handling Matroska files:
struct MatroskaDemuxCreationState {
    MatroskaFileServerDemux* demux;
    char watchVariable;
};

static void onMatroskaDemuxCreation(MatroskaFileServerDemux* newDemux, void* clientData)
{
    MatroskaDemuxCreationState* creationState = (MatroskaDemuxCreationState*)clientData;
    creationState->demux = newDemux;
    creationState->watchVariable = 1;
}
// END Special code for handling Matroska files:

// Special code for handling Ogg files:
struct OggDemuxCreationState {
    OggFileServerDemux* demux;
    char watchVariable;
};

static void onOggDemuxCreation(OggFileServerDemux* newDemux, void* clientData)
{
    OggDemuxCreationState* creationState = (OggDemuxCreationState*)clientData;
    creationState->demux = newDemux;
    creationState->watchVariable = 1;
}
// END Special code for handling Ogg files:

#define NEW_SMS(description) do {\
        char const* descStr = description\
            ", streamed by the LIVE555 Media Server";\
        sms = ServerMediaSession::createNew(env, fileName, fileName, descStr);\
    } while(0)

static ServerMediaSession* createNewSMS(UsageEnvironment& env,
                                        stream_reader_t *pStream_reader,
                                        Boolean has_amba_audio, Boolean has_amba_video,
                                        char const* fileName, FILE* /*fid*/)
{
    // Use the file name extension to determine the type of "ServerMediaSession":
    char const* extension = strrchr(fileName, '.');
    if (extension == NULL) return NULL;

    ServerMediaSession* sms = NULL;
    Boolean const reuseSource = False;
#if defined(CFG_AMBA_STREAM)
    if( (has_amba_video == True || has_amba_audio == True) &&
        strcasecmp(extension, ".mp4") == 0 )
    {
        NEW_SMS("Amba MP4");
        if( sms ) {
            AmbaVideoServerMediaSubsession   *vid_subsession = NULL;
            AmbaAudioServerMediaSubsession   *aud_subsession = NULL;
            OutPacketBuffer::maxSize = 300000;

            if( has_amba_video )
            {
                vid_subsession = AmbaVideoServerMediaSubsession::createNew(env, pStream_reader, fileName, True);
                sms->addSubsession(vid_subsession);
            }

            if( has_amba_audio )
            {
                aud_subsession = AmbaAudioServerMediaSubsession::createNew(env, pStream_reader, fileName, True);
                sms->addSubsession(aud_subsession);
            }
        }
    }
    else
#endif
    if(strcmp(extension, ".aac") == 0) {
        // Assumed to be an AAC Audio (ADTS format) file:
        NEW_SMS("AAC Audio");
        sms->addSubsession(ADTSAudioFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".amr") == 0) {
        // Assumed to be an AMR Audio file:
        NEW_SMS("AMR Audio");
        sms->addSubsession(AMRAudioFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".ac3") == 0) {
        // Assumed to be an AC-3 Audio file:
        NEW_SMS("AC-3 Audio");
        sms->addSubsession(AC3AudioFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".m4e") == 0) {
        // Assumed to be a MPEG-4 Video Elementary Stream file:
        NEW_SMS("MPEG-4 Video");
        sms->addSubsession(MPEG4VideoFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".264") == 0) {
        // Assumed to be a H.264 Video Elementary Stream file:
        NEW_SMS("H.264 Video");
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large H.264 frames
        sms->addSubsession(H264VideoFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".265") == 0) {
        // Assumed to be a H.265 Video Elementary Stream file:
        NEW_SMS("H.265 Video");
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large H.265 frames
        sms->addSubsession(H265VideoFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".mp3") == 0) {
        // Assumed to be a MPEG-1 or 2 Audio file:
        NEW_SMS("MPEG-1 or 2 Audio");
        // To stream using 'ADUs' rather than raw MP3 frames, uncomment the following:
//#define STREAM_USING_ADUS 1
        // To also reorder ADUs before streaming, uncomment the following:
//#define INTERLEAVE_ADUS 1
        // (For more information about ADUs and interleaving,
        //  see <http://www.live555.com/rtp-mp3/>)
        Boolean useADUs = False;
        Interleaving* interleaving = NULL;
#ifdef STREAM_USING_ADUS
        useADUs = True;
#ifdef INTERLEAVE_ADUS
        unsigned char interleaveCycle[] = {0,2,1,3}; // or choose your own...
        unsigned const interleaveCycleSize
            = (sizeof interleaveCycle)/(sizeof (unsigned char));
        interleaving = new Interleaving(interleaveCycleSize, interleaveCycle);
#endif
#endif
        sms->addSubsession(MP3AudioFileServerMediaSubsession::createNew(env, fileName, reuseSource, useADUs, interleaving));
    } else if (strcmp(extension, ".mpg") == 0) {
        // Assumed to be a MPEG-1 or 2 Program Stream (audio+video) file:
        NEW_SMS("MPEG-1 or 2 Program Stream");
        MPEG1or2FileServerDemux* demux
            = MPEG1or2FileServerDemux::createNew(env, fileName, reuseSource);
        sms->addSubsession(demux->newVideoServerMediaSubsession());
        sms->addSubsession(demux->newAudioServerMediaSubsession());
    } else if (strcmp(extension, ".vob") == 0) {
        // Assumed to be a VOB (MPEG-2 Program Stream, with AC-3 audio) file:
        NEW_SMS("VOB (MPEG-2 video with AC-3 audio)");
        MPEG1or2FileServerDemux* demux
            = MPEG1or2FileServerDemux::createNew(env, fileName, reuseSource);
        sms->addSubsession(demux->newVideoServerMediaSubsession());
        sms->addSubsession(demux->newAC3AudioServerMediaSubsession());
    } else if (strcmp(extension, ".ts") == 0) {
        // Assumed to be a MPEG Transport Stream file:
        // Use an index file name that's the same as the TS file name, except with ".tsx":
        unsigned indexFileNameLen = strlen(fileName) + 2; // allow for trailing "x\0"
        char* indexFileName = new char[indexFileNameLen];
        sprintf(indexFileName, "%sx", fileName);
        NEW_SMS("MPEG Transport Stream");
        sms->addSubsession(MPEG2TransportFileServerMediaSubsession::createNew(env, fileName, indexFileName, reuseSource));
        delete[] indexFileName;
    } else if (strcmp(extension, ".wav") == 0) {
        // Assumed to be a WAV Audio file:
        NEW_SMS("WAV Audio Stream");
        // To convert 16-bit PCM data to 8-bit u-law, prior to streaming,
        // change the following to True:
        Boolean convertToULaw = False;
        sms->addSubsession(WAVAudioFileServerMediaSubsession::createNew(env, fileName, reuseSource, convertToULaw));
    } else if (strcmp(extension, ".dv") == 0) {
        // Assumed to be a DV Video file
        // First, make sure that the RTPSinks' buffers will be large enough to handle the huge size of DV frames (as big as 288000).
        OutPacketBuffer::maxSize = 300000;

        NEW_SMS("DV Video");
        sms->addSubsession(DVVideoFileServerMediaSubsession::createNew(env, fileName, reuseSource));
    } else if (strcmp(extension, ".mkv") == 0 || strcmp(extension, ".webm") == 0) {
        // Assumed to be a Matroska file (note that WebM ('.webm') files are also Matroska files)
        OutPacketBuffer::maxSize = 100000; // allow for some possibly large VP8 or VP9 frames
        NEW_SMS("Matroska video+audio+(optional)subtitles");

        // Create a Matroska file server demultiplexor for the specified file.
        // (We enter the event loop to wait for this to complete.)
        MatroskaDemuxCreationState creationState;
        creationState.watchVariable = 0;
        MatroskaFileServerDemux::createNew(env, fileName, onMatroskaDemuxCreation, &creationState);
        env.taskScheduler().doEventLoop(&creationState.watchVariable);

        ServerMediaSubsession* smss;
        while ((smss = creationState.demux->newServerMediaSubsession()) != NULL) {
            sms->addSubsession(smss);
        }
    } else if (strcmp(extension, ".ogg") == 0 || strcmp(extension, ".ogv") == 0 || strcmp(extension, ".opus") == 0) {
        // Assumed to be an Ogg file
        NEW_SMS("Ogg video and/or audio");

        // Create a Ogg file server demultiplexor for the specified file.
        // (We enter the event loop to wait for this to complete.)
        OggDemuxCreationState creationState;
        creationState.watchVariable = 0;
        OggFileServerDemux::createNew(env, fileName, onOggDemuxCreation, &creationState);
        env.taskScheduler().doEventLoop(&creationState.watchVariable);

        ServerMediaSubsession* smss;
        while ((smss = creationState.demux->newServerMediaSubsession()) != NULL) {
            sms->addSubsession(smss);
        }
    }

    return sms;
}
