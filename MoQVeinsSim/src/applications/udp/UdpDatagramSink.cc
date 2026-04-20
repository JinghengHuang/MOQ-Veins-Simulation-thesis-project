#include "UdpDatagramSink.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/Packet.h"

Define_Module(UdpDatagramSink);

void UdpDatagramSink::processPacket(Packet *pk)
{
    EV << "Received packet: " << pk->getName() << endl;

    //Compute delay
    auto creationTimeTag = pk->findTag<CreationTimeTag>();
    if (creationTimeTag != nullptr) {
        simtime_t delay = simTime() - creationTimeTag->getCreationTime();
        EV << "End-to-end delay: " << delay << endl;
    }

    delete pk;
}
