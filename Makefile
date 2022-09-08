
CC = gcc
CX = g++


# DTP_DIR			= ../submodule/quiche
DTP_DIR 		= ../submodule/DTP
DTP_INC			= $(DTP_DIR)/include
DTP_LIB			= $(DTP_DIR)/target/release

CXFLAGS = -std=c++11 -g

INCS = -I$(CURDIR) -I$(DTP_INC)

LIBS = -lswscale -lswresample -lavformat \
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

uclient : udp_client.cxx sodtp_jitter.cxx sodtp_block.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

userver : udp_server.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

dplay : dplay.cxx sodtp_jitter.cxx sodtp_block.cxx
	$(CX) $(CXFLAGS) -o $@ $^ $(INCS) $(LIBS)

clean:
	rm -rf $(TARGETS)
