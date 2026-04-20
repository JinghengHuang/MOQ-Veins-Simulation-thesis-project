#include "UdpApp.h"

#include "inet/common/TimeTag_m.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "inet/common/packet/Packet.h"

Define_Module(UdpApp);

void UdpApp::sendPacket()
{
    //Make packet 
    char pkname[40];
    sprintf(pkname, "MyUdpPacket-%d", mySeq);

    auto packet = new Packet(pkname);

    //payload
    const auto& payload = makeShared<ByteCountChunk>(B(100));  // 100 bytes
    packet->insertAtBack(payload);

    //Add timestap on send
    packet->addTag<CreationTimeTag>()->setCreationTime(simTime());

    EV << "Sending packet " << pkname << " at " << simTime() << endl;

    //send packet
    socket.sendTo(packet, destAddresses[0], destPort);

    mySeq++;
}
