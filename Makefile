#
# OMNeT++/OMNEST Makefile for libRSTP
#
# This file was generated with the command:
#  opp_makemake -f --deep --make-so -O out -I../inetmanet/src/networklayer/arp -I../inetmanet/src/linklayer/etherswitch -I../inetmanet/src/transport/sctp -I../inetmanet/src/world -I../inetmanet/src/transport/contract -I../inetmanet/src/linklayer/mfcore -I../inetmanet/src/linklayer/ethernet -I../inetmanet/src/util -I../inetmanet/src/networklayer/ted -I../inetmanet/src/linklayer/ieee80211/mac -I../inetmanet/src/networklayer/ipv6 -I../inetmanet/src/networklayer/common -I../inetmanet/src/applications/pingapp -I../inetmanet/src/networklayer/ldp -I../inetmanet/src/transport/tcp -I../inetmanet/src/util/headerserializers -I../inetmanet/src/transport/udp -I../inetmanet/src/networklayer/rsvp_te -I../inetmanet/src/networklayer/ipv4 -I../inetmanet/src/networklayer/icmpv6 -I../inetmanet/src/base -I../inetmanet/src/networklayer/contract -I../inetmanet/src/networklayer/manetrouting/base -I../inetmanet/src/networklayer/mpls -I../inetmanet/src/linklayer/contract -L../DHCP/out/$(CONFIGNAME)/src -L../inetmanet/out/$(CONFIGNAME)/src -L../Channels/out/$(CONFIGNAME) -lDHCP -linet -lChannels -KDHCP_PROJ=../DHCP -KINETMANET_PROJ=../inetmanet -KCHANNELS_PROJ=../Channels
#

# Name of target to be created (-o option)
TARGET = libRSTP$(SHARED_LIB_SUFFIX)

# C++ include paths (with -I)
INCLUDE_PATH = \
    -I../inetmanet/src/networklayer/arp \
    -I../inetmanet/src/linklayer/etherswitch \
    -I../inetmanet/src/transport/sctp \
    -I../inetmanet/src/world \
    -I../inetmanet/src/transport/contract \
    -I../inetmanet/src/linklayer/mfcore \
    -I../inetmanet/src/linklayer/ethernet \
    -I../inetmanet/src/util \
    -I../inetmanet/src/networklayer/ted \
    -I../inetmanet/src/linklayer/ieee80211/mac \
    -I../inetmanet/src/networklayer/ipv6 \
    -I../inetmanet/src/networklayer/common \
    -I../inetmanet/src/applications/pingapp \
    -I../inetmanet/src/networklayer/ldp \
    -I../inetmanet/src/transport/tcp \
    -I../inetmanet/src/util/headerserializers \
    -I../inetmanet/src/transport/udp \
    -I../inetmanet/src/networklayer/rsvp_te \
    -I../inetmanet/src/networklayer/ipv4 \
    -I../inetmanet/src/networklayer/icmpv6 \
    -I../inetmanet/src/base \
    -I../inetmanet/src/networklayer/contract \
    -I../inetmanet/src/networklayer/manetrouting/base \
    -I../inetmanet/src/networklayer/mpls \
    -I../inetmanet/src/linklayer/contract \
    -I. \
    -Iexamples \
    -Iexamples/crossed \
    -Iexamples/crossed/results \
    -Iexamples/messageAge \
    -Iexamples/messageAge/results \
    -Iexamples/redundant \
    -Iexamples/reference \
    -Iexamples/reference/results \
    -Iexamples/triangle \
    -Iexamples/triangle/results \
    -Iexamples/twoSwitch \
    -Isrc

# Additional object and library files to link with
EXTRA_OBJS =

# Additional libraries (-L, -l options)
LIBS = -L../DHCP/out/$(CONFIGNAME)/src -L../inetmanet/out/$(CONFIGNAME)/src -L../Channels/out/$(CONFIGNAME)  -lDHCP -linet -lChannels
LIBS += -Wl,-rpath,`abspath ../DHCP/out/$(CONFIGNAME)/src` -Wl,-rpath,`abspath ../inetmanet/out/$(CONFIGNAME)/src` -Wl,-rpath,`abspath ../Channels/out/$(CONFIGNAME)`

# Output directory
PROJECT_OUTPUT_DIR = out
PROJECTRELATIVE_PATH =
O = $(PROJECT_OUTPUT_DIR)/$(CONFIGNAME)/$(PROJECTRELATIVE_PATH)

# Object files for local .cc and .msg files
OBJS = $O/src/MACRelayUnitSTPNP.o $O/src/STPDefinitions.o $O/src/STPTimers_m.o $O/src/BPDU_m.o

# Message files
MSGFILES = \
    src/STPTimers.msg \
    src/BPDU.msg

# Other makefile variables (-K)
DHCP_PROJ=../DHCP
INETMANET_PROJ=../inetmanet
CHANNELS_PROJ=../Channels

#------------------------------------------------------------------------------

# Pull in OMNeT++ configuration (Makefile.inc or configuser.vc)

ifneq ("$(OMNETPP_CONFIGFILE)","")
CONFIGFILE = $(OMNETPP_CONFIGFILE)
else
ifneq ("$(OMNETPP_ROOT)","")
CONFIGFILE = $(OMNETPP_ROOT)/Makefile.inc
else
CONFIGFILE = $(shell opp_configfilepath)
endif
endif

ifeq ("$(wildcard $(CONFIGFILE))","")
$(error Config file '$(CONFIGFILE)' does not exist -- add the OMNeT++ bin directory to the path so that opp_configfilepath can be found, or set the OMNETPP_CONFIGFILE variable to point to Makefile.inc)
endif

include $(CONFIGFILE)

# Simulation kernel and user interface libraries
OMNETPP_LIB_SUBDIR = $(OMNETPP_LIB_DIR)/$(TOOLCHAIN_NAME)
OMNETPP_LIBS = -L"$(OMNETPP_LIB_SUBDIR)" -L"$(OMNETPP_LIB_DIR)" -loppenvir$D $(KERNEL_LIBS) $(SYS_LIBS)

COPTS = $(CFLAGS)  $(INCLUDE_PATH) -I$(OMNETPP_INCL_DIR)
MSGCOPTS = $(INCLUDE_PATH)

#------------------------------------------------------------------------------
# User-supplied makefile fragment(s)
# >>>
# <<<
#------------------------------------------------------------------------------

# Main target
all: $(TARGET)

$(TARGET) : $O/$(TARGET)
	$(LN) $O/$(TARGET) .

$O/$(TARGET): $(OBJS)  $(wildcard $(EXTRA_OBJS)) Makefile
	@$(MKPATH) $O
	$(SHLIB_LD) -o $O/$(TARGET)  $(OBJS) $(EXTRA_OBJS) $(LIBS) $(OMNETPP_LIBS) $(LDFLAGS)
	$(SHLIB_POSTPROCESS) $O/$(TARGET)

.PHONY:

.SUFFIXES: .cc

$O/%.o: %.cc
	@$(MKPATH) $(dir $@)
	$(CXX) -c $(COPTS) -o $@ $<

%_m.cc %_m.h: %.msg
	$(MSGC) -s _m.cc $(MSGCOPTS) $?

msgheaders: $(MSGFILES:.msg=_m.h)

clean:
	-rm -rf $O
	-rm -f RSTP RSTP.exe libRSTP.so libRSTP.a libRSTP.dll libRSTP.dylib
	-rm -f ./*_m.cc ./*_m.h
	-rm -f examples/*_m.cc examples/*_m.h
	-rm -f examples/crossed/*_m.cc examples/crossed/*_m.h
	-rm -f examples/crossed/results/*_m.cc examples/crossed/results/*_m.h
	-rm -f examples/messageAge/*_m.cc examples/messageAge/*_m.h
	-rm -f examples/messageAge/results/*_m.cc examples/messageAge/results/*_m.h
	-rm -f examples/redundant/*_m.cc examples/redundant/*_m.h
	-rm -f examples/reference/*_m.cc examples/reference/*_m.h
	-rm -f examples/reference/results/*_m.cc examples/reference/results/*_m.h
	-rm -f examples/triangle/*_m.cc examples/triangle/*_m.h
	-rm -f examples/triangle/results/*_m.cc examples/triangle/results/*_m.h
	-rm -f examples/twoSwitch/*_m.cc examples/twoSwitch/*_m.h
	-rm -f src/*_m.cc src/*_m.h

cleanall: clean
	-rm -rf $(PROJECT_OUTPUT_DIR)

depend:
	$(MAKEDEPEND) $(INCLUDE_PATH) -f Makefile -P\$$O/ -- $(MSG_CC_FILES)  ./*.cc examples/*.cc examples/crossed/*.cc examples/crossed/results/*.cc examples/messageAge/*.cc examples/messageAge/results/*.cc examples/redundant/*.cc examples/reference/*.cc examples/reference/results/*.cc examples/triangle/*.cc examples/triangle/results/*.cc examples/twoSwitch/*.cc src/*.cc

# DO NOT DELETE THIS LINE -- make depend depends on it.
$O/src/STPTimers_m.o: src/STPTimers_m.cc \
	src/STPTimers_m.h
$O/src/BPDU_m.o: src/BPDU_m.cc \
	$(INETMANET_PROJ)/src/base/INETDefs.h \
	src/BPDU_m.h \
	$(INETMANET_PROJ)/src/linklayer/contract/MACAddress.h \
	src/STPDefinitions.h
$O/src/STPDefinitions.o: src/STPDefinitions.cc \
	$(INETMANET_PROJ)/src/base/INETDefs.h \
	$(INETMANET_PROJ)/src/linklayer/contract/MACAddress.h \
	src/STPDefinitions.h
$O/src/MACRelayUnitSTPNP.o: src/MACRelayUnitSTPNP.cc \
	$(INETMANET_PROJ)/src/linklayer/etherswitch/MACRelayUnitNP.h \
	src/MACRelayUnitSTPNP.h \
	$(INETMANET_PROJ)/src/linklayer/contract/Ieee802Ctrl_m.h \
	$(INETMANET_PROJ)/src/linklayer/ethernet/EtherFrame_m.h \
	$(INETMANET_PROJ)/src/linklayer/etherswitch/MACRelayUnitBase.h \
	$(INETMANET_PROJ)/src/base/INETDefs.h \
	src/BPDU_m.h \
	$(INETMANET_PROJ)/src/linklayer/contract/MACAddress.h \
	src/STPDefinitions.h \
	src/STPTimers_m.h \
	$(INETMANET_PROJ)/src/linklayer/ethernet/Ethernet.h

