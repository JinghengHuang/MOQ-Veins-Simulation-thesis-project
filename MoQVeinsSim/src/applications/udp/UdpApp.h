
#ifndef __MOQSIM_UDPAPP_H
#define __MOQSIM_UDPAPP_H

#include <vector>

#include "inet/applications/udpapp/UdpBasicApp.h"
#include "inet/common/packet/Packet.h"
using namespace inet;

/**
 * UDP application. See NED for more info.
 */
class UdpApp : public UdpBasicApp
{
  protected:
    int mySeq = 0;

    virtual void sendPacket() override;
};
#endif

