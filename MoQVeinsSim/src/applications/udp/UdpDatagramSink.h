
#ifndef __MOQSIM_UDPSINK_H
#define __MOQSIM_UDPSINK_H


#include "inet/applications/udpapp/UdpSink.h"
#include "inet/common/packet/Packet.h"
using namespace inet;
class UdpDatagramSink : public UdpSink
{
  protected:

    virtual void processPacket(Packet *pk) override;
};
#endif