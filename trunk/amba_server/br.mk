INCLUDES   = -I$(STAGING_DIR)/usr/include/UsageEnvironment \
			 -I$(STAGING_DIR)/usr/include/groupsock \
			 -I$(STAGING_DIR)/usr/include/liveMedia \
			 -I$(STAGING_DIR)/usr/include/BasicUsageEnvironment

##### Change the following for your environment:
COMPILE_OPTS =		$(INCLUDES) -I. -O2 -DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
C_COMPILER =		cc
C_FLAGS =		$(COMPILE_OPTS) $(CPPFLAGS) $(CFLAGS)
CPLUSPLUS_COMPILER =	c++
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS) -Wall -DBSD=1 $(CPPFLAGS) $(CXXFLAGS)
LINK =			c++ -o
LINK_OPTS =		-L. $(LDFLAGS)
CONSOLE_LINK_OPTS =	$(LINK_OPTS)
LIBRARY_LINK =		ar cr
LIBRARY_LINK_OPTS =
LIB_SUFFIX =			a

USER_OPTS = -DCFG_AMBA_STREAM
COMPILE_OPTS = $(INCLUDES) -I. -DSOCKLEN_T=socklen_t $(TARGET_CFLAGS) -DLOCALE_NOT_USED $(USER_OPTS)
C_COMPILER = $(CC)
CPLUSPLUS_COMPILER = $(CXX)
LINK = $(CXX) -o
LINK_OPTS = -L.
##### End of variables to change

MEDIA_SERVER = liveMediaServer

ALL = $(MEDIA_SERVER)
all: $(ALL)
.c.o:
	$(C_COMPILER) -c $(C_FLAGS) $<
.cpp.o:
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $<

MEDIA_SERVER_OBJS = live555MediaServer.o \
                    DynamicRTSPServer.o \
                    AmbaAudioStreamSource.o \
                    AmbaAudioServerMediaSubsession.o \
                    AmbaVideoStreamSource.o \
                    AmbaVideoServerMediaSubsession.o \
                    stream_reader.o \
                    stream_reader_fifo_live.o \
                    stream_reader_fifo_playback.o \
                    ring_frame_buf_opt.o \
                    stream_codec_h264.o \
                    stream_codec_aac.o \
                    stream_codec_pcm.o \
                    stream_command.o

USAGE_ENVIRONMENT_LIB       = $(STAGING_DIR)/usr/lib/libUsageEnvironment.$(LIB_SUFFIX)
BASIC_USAGE_ENVIRONMENT_LIB = $(STAGING_DIR)/usr/lib/libBasicUsageEnvironment.$(LIB_SUFFIX)
LIVEMEDIA_LIB               = $(STAGING_DIR)/usr/lib/libliveMedia.$(LIB_SUFFIX)
GROUPSOCK_LIB               = $(STAGING_DIR)/usr/lib/libgroupsock.$(LIB_SUFFIX)

LOCAL_LIBS =	$(LIVEMEDIA_LIB) $(GROUPSOCK_LIB) \
		$(BASIC_USAGE_ENVIRONMENT_LIB) $(USAGE_ENVIRONMENT_LIB)
LIBS =			$(LOCAL_LIBS) -lambanetfifo -lpthread

$(MEDIA_SERVER):	$(MEDIA_SERVER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MEDIA_SERVER_OBJS) $(LIBS)

clean:
	$(RM) -rf $(MEDIA_SERVER) *.o

install: $(MEDIA_SERVER)
	mkdir -p $(DESTDIR)/usr/bin;
	for i in $(MEDIA_SERVER); do install -D -m 755  $$i $(DESTDIR)/usr/bin/$$i; done

##### Any additional, platform-specific rules come here:
