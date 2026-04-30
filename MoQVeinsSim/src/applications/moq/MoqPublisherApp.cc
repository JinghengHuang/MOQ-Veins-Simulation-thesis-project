#include "applications/moq/MoqPublisherApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
// #include "models/MoqTrackHeader_m.h"

namespace moqveinssim {


Define_Module(MoqPublisherApp);
MoqPublisherApp::MoqPublisherApp() {
    timerConnect = new inet::cMessage("MOQ Publisher Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Publisher Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);


    const auto* arr = omnetpp::check_and_cast<omnetpp::cValueArray*>(par("tracks").objectValue());

    for (int i = 0; i < arr->size(); i++) {
        auto& elem = arr->get(i);
        const auto* map = omnetpp::check_and_cast<omnetpp::cValueMap*>(elem.objectValue());
        TrackMeta track;
        track.trackId = i;
        track.trackName = (*map)["trackName"].stringValue();
        track.packetSize = (*map)["packetSize"].intValue();
        track.sendInterval = (*map)["sendInterval"].intValue();
        track.priority = (*map)["priority"].intValue();
        tracks.push_back(track);
        EV << "Track: " << track.trackName << " obj size =" << track.packetSize << std::endl;
    }
}

MoqPublisherApp::~MoqPublisherApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
}

void MoqPublisherApp::handleTimeout(omnetpp::cMessage *msg)
{
    EV_DETAIL << "handle timeout of kind " << msg->getKind() << std::endl;
    switch (msg->getKind()) {
        case TIMER_CONNECT:
            EV_INFO << "connect - address: " << connectAddress << std::endl;

            //socket.connect(connectAddress, connectPort, 0, 0, 0);
            socket.connect(connectAddress, connectPort);
            break;
        case TIMER_LIMIT_RUNTIME:
            socket.close();
            finish();
            break;
        default:
            throw omnetpp::cRuntimeError("Invalid timer: %d", (int) msg->getKind());
    }
}

void MoqPublisherApp::handleMessageWhenUp(omnetpp::cMessage *msg)
{
    EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
    if (msg->isSelfMessage()) {
        handleTimeout(msg);
    } else if (msg->arrivedOn("socketIn")) { // from QUIC
        // TODO: Add and handle events: case QUIC_I_SENDQUEUE_DRAINING and QUIC_I_SENDQUEUE_FULL
        socket.processMessage(msg);
        //delete msg;
    } else { // something really strange...
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}


void MoqPublisherApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqPublisherApp" << std::endl;

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);

    scheduleAt(par("connectTime"), timerConnect);
}

void MoqPublisherApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqPublisherApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Publisher FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqPublisherApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
}

void MoqPublisherApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_DEBUG << "Data arrived" << std::endl;
}

void MoqPublisherApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqPublisherApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqPublisherApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}

// Based on track configurations, send track data
void MoqPublisherApp::sendTrackData(omnetpp::cMessage* msg){
    
    for (auto & track : tracks){

        EV_INFO << "sendData - track " << track.trackId << " - name " << track.trackName << " - size " << track.packetSize << " byte" << std::endl;

        inet::Packet *packet = new inet::Packet("MoqTrackObjectData");

        // inet::Ptr<MoqTrackHeader> header = inet::makeShared<MoqTrackHeader>();
        // header->
        
        

        // packet->insertAtFront(header);

        // stream mapping
        if (trackToStreamMap.find(track.trackId) != trackToStreamMap.end()){
            trackToStreamMap[track.trackId] = track.trackId;
        }
        int streamId = trackToStreamMap[track.trackId];
        packet->addTagIfAbsent<inet::QuicStreamReq>()->setStreamID(streamId);

        socket.send(packet);
    }
}

}
