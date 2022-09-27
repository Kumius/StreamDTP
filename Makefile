
CC = gcc
CX = g++

BUILD_DIR = ./build

# DTP_DIR			= ../submodule/quiche
DTP_DIR 		= ../submodule/DTP
DTP_INC			= $(DTP_DIR)/include
DTP_LIB			= $(DTP_DIR)/target/debug

CXFLAGS = -std=c++11 -g -MMD

INCS = -I$(CURDIR) -I$(DTP_INC)

LIBS = -L$(CURDIR) -lswscale -lswresample -lavformat \
			-lavdevice -lavcodec -lavutil -lavfilter \
			-lSDL2 \
			-L$(DTP_LIB) -lquiche \
			-lm -lz -lev -ldl -pthread \

TARGETS = client dclient dserver dplay uclient userver

all: $(TARGETS)

client : sodtp_client.cxx sodtp_jitter.cxx sodtp_block.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

dclient : dtp_client.cxx sodtp_jitter.cxx sodtp_block.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

dserver : dtp_server.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

uclient : udp_client.cxx udp_client.h libsodtp.so
	$(CX) $(CXFLAGS) -o $@ $< $(INCS) $(LIBS) -lsodtp

# TODO fix udp server.h dependency
userver : udp_server.cxx udp_server.h
	$(CX) $(CXFLAGS) -o $@ $< $(INCS) $(LIBS)

dplay : dplay.cxx sodtp_jitter.cxx sodtp_block.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

libsodtp.so : sodtp_block.cxx sodtp_jitter.cxx
	$(CX) $(CXFLAGS) -fPIC -shared -o $@ $^ $(INCS) $(LIBS)

clean:
	rm -rf $(TARGETS)
