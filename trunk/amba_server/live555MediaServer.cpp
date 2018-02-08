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
// LIVE555 Media Server
// main program

#include <BasicUsageEnvironment.hh>
#include "DynamicRTSPServer.hh"
#include "version.hh"

#include "GroupsockHelper.hh"
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <signal.h>
/////////////////////////////////
static UsageEnvironment* env;
static RTSPServer* rtspServer;
static char stopEventLoop = 0;
/////////////////////////////////

static int _SetupInterfaceAddr(void)
{
    int             i, sockfd;
    struct ifconf   ifconf = {0};
    struct ifreq    *ifreq;
    char            buf[512] = {0};

    ifconf.ifc_len = 512;
    ifconf.ifc_buf = buf;
    if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 )
    {
        fprintf(stderr, "[%s #%u] create socket fail !\n", __func__ , __LINE__);
        return -1;
    }

    if( ioctl(sockfd, SIOCGIFCONF, &ifconf) < 0 )
    {
        fprintf(stderr, "[%s #%u] failed to get IFCONF!\n", __func__ , __LINE__);
    }
    close(sockfd);

    ifreq = (struct ifreq*)buf;
    for(i = (ifconf.ifc_len/sizeof(struct ifreq)); i > 0; i--)
    {
        char                *pIp_addr = 0;
        struct sockaddr_in  *pSockaddr = (struct sockaddr_in*)&(ifreq->ifr_addr);

        pIp_addr = inet_ntoa(pSockaddr->sin_addr);
        if( strcmp(pIp_addr, "127.0.0.1") == 0 )
        {
            ifreq++;
            continue;
        }

        ReceivingInterfaceAddr = (netAddressBits)pSockaddr->sin_addr.s_addr;
        fprintf(stderr, "[%s #%u] get if_addr = 0x%x !\n", __func__ , __LINE__, ReceivingInterfaceAddr);
        return 0;
    }
    return -1;
}

static void
_signalHandlerShutdown(int sig)
{
    fprintf(stderr, "%s: Got signal %d, program exits!\n", __FUNCTION__, sig);
    Medium::close(rtspServer);
    stopEventLoop = 1;

    if( SIGSEGV == sig )
        exit(-1);

    return;
}
/////////////////////////////////
int main(int argc, char** argv)
{
    // Begin by setting up our usage environment:
    TaskScheduler* scheduler = BasicTaskScheduler::createNew();

    env = BasicUsageEnvironment::createNew(*scheduler);

    UserAuthenticationDatabase* authDB = NULL;
#ifdef ACCESS_CONTROL
    // To implement client access control to the RTSP server, do the following:
    authDB = new UserAuthenticationDatabase;
    authDB->addUserRecord("username1", "password1"); // replace these with real strings
    // Repeat the above with each <username>, <password> that you wish to allow
    // access to the server.
#endif

    // setup local net interface address
    _SetupInterfaceAddr();

    /* Allow ourselves to be shut down gracefully by a signal */
    signal(SIGTERM, _signalHandlerShutdown);
    signal(SIGHUP, _signalHandlerShutdown);
    signal(SIGUSR1, _signalHandlerShutdown);
    signal(SIGQUIT, _signalHandlerShutdown);
    signal(SIGINT, _signalHandlerShutdown);
    signal(SIGKILL, _signalHandlerShutdown);
    signal(SIGSEGV, _signalHandlerShutdown);

    // Create the RTSP server.  Try first with the default port number (554),
    // and then with the alternative port number (8554):

    portNumBits rtspServerPortNum = 554;
    rtspServer = DynamicRTSPServer::createNew(*env, rtspServerPortNum, authDB);
    if (rtspServer == NULL) {
        rtspServerPortNum = 8554;
        rtspServer = DynamicRTSPServer::createNew(*env, rtspServerPortNum, authDB);
    }
    if (rtspServer == NULL) {
        *env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
        exit(1);
    }

    *env << "LIVE555 Media Server\n";
    *env << "\tversion " << MEDIA_SERVER_VERSION_STRING
         << " (LIVE555 Streaming Media library version "
         << LIVEMEDIA_LIBRARY_VERSION_STRING << ").\n";

    char* urlPrefix = rtspServer->rtspURLPrefix();
    *env << "Play streams from this server using the URL\n\t"
         << urlPrefix << "<file path>\nwhere <file path> is a file present in the current directory.\n";

    *env << "or live view URL\n\t"
         << urlPrefix << LIVE_VEIW_NAME;

    // *env << "Each file's type is inferred from its name suffix:\n";
    // *env << "\t\".264\" => a H.264 Video Elementary Stream file\n";
    // *env << "\t\".265\" => a H.265 Video Elementary Stream file\n";
    // *env << "\t\".aac\" => an AAC Audio (ADTS format) file\n";
    // *env << "\t\".ac3\" => an AC-3 Audio file\n";
    // *env << "\t\".amr\" => an AMR Audio file\n";
    // *env << "\t\".dv\" => a DV Video file\n";
    // *env << "\t\".m4e\" => a MPEG-4 Video Elementary Stream file\n";
    // *env << "\t\".mkv\" => a Matroska audio+video+(optional)subtitles file\n";
    // *env << "\t\".mp3\" => a MPEG-1 or 2 Audio file\n";
    // *env << "\t\".mpg\" => a MPEG-1 or 2 Program Stream (audio+video) file\n";
    // *env << "\t\".ogg\" or \".ogv\" or \".opus\" => an Ogg audio and/or video file\n";
    // *env << "\t\".ts\" => a MPEG Transport Stream file\n";
    // *env << "\t\t(a \".tsx\" index file - if present - provides server 'trick play' support)\n";
    // *env << "\t\".vob\" => a VOB (MPEG-2 video with AC-3 audio) file\n";
    // *env << "\t\".wav\" => a WAV Audio file\n";
    // *env << "\t\".webm\" => a WebM audio(Vorbis)+video(VP8) file\n";
    // *env << "\nSee http://www.live555.com/mediaServer/ for additional documentation.\n";

    // Also, attempt to create a HTTP server for RTSP-over-HTTP tunneling.
    // Try first with the default HTTP port (80), and then with the alternative HTTP
    // port numbers (8000 and 8080).

    if (rtspServer->setUpTunnelingOverHTTP(80) || rtspServer->setUpTunnelingOverHTTP(8000) || rtspServer->setUpTunnelingOverHTTP(8080)) {
        *env << "\n\t(We use port " << rtspServer->httpServerPortNum() << " for optional RTSP-over-HTTP tunneling, or for HTTP live streaming (for indexed Transport Stream files only).)\n\n\n";
    } else {
        *env << "(RTSP-over-HTTP tunneling is not available.)\n\n\n";
    }

    env->taskScheduler().doEventLoop(&stopEventLoop); // does not return

    env->reclaim();
    delete scheduler;

    return 0; // only to prevent compiler warning
}
