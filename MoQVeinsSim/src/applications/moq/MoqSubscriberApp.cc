#include "MoqSubscriberApp.h"

#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "models/MoqObjectChunk_m.h"
#include "models/MoqPublisherAnnounce_m.h"
#include <algorithm>

#include <omnetpp.h>

namespace moqveinssim {
Define_Module(MoqSubscriberApp);

MoqSubscriberApp::MoqSubscriberApp() {
    timerConnect = new inet::cMessage("MOQ Publisher Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Publisher Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
}

MoqSubscriberApp::~MoqSubscriberApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
    
    for (auto& track : tracks) {
        cancelAndDelete(track.second.timer);
        track.second.timer = nullptr;
    }
    errorEvent = nullptr;
}

void MoqSubscriberApp::handleTimeout(omnetpp::cMessage *msg)
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

void MoqSubscriberApp::handleMessageWhenUp(omnetpp::cMessage *msg)
{
    EV_DEBUG << "handle message of kind " << msg->getKind() << std::endl;
    if (msg->isSelfMessage()) {
        int id = msg->getKind();
        if (id == TIMER_CONNECT || id == TIMER_LIMIT_RUNTIME){
            handleTimeout(msg);
        }
    } else if (msg->arrivedOn("socketIn")) { // from QUIC
        // TODO: Add and handle events: case QUIC_I_SENDQUEUE_DRAINING and QUIC_I_SENDQUEUE_FULL
        socket.processMessage(msg);
        //delete msg;
    } else { // something really strange...
        throw omnetpp::cRuntimeError("Invalid message: %d", (int) msg->getKind());
    }
}


void MoqSubscriberApp::handleStartOperation(inet::LifecycleOperation *operation)
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

void MoqSubscriberApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqSubscriberApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Publisher FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqSubscriberApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
    sendTrackAnnouncementData();
}

void MoqSubscriberApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_DEBUG << "Data arrived" << std::endl;
}

void MoqSubscriberApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqSubscriberApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqSubscriberApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}

// Based on track configurations, send track announcement data
void MoqSubscriberApp::sendTrackAnnouncementData(){
    
    for (auto & track : tracks){

        scheduleAt(inet::simTime(), track.second.timer);
    }
}
// Send track announcement data
void MoqSubscriberApp::sendTrackData(long tid){
    
    const auto track = tracks.find(tid);
    if(track != tracks.end()){
        scheduleAt(inet::simTime() + track->second.sendInterval, track->second.timer);
    }else{
        errorEvent = new omnetpp::cMessage("SUB_ERROR");
        errorEvent->setKind(SUB_ERROR);
        scheduleAt(inet::simTime(), errorEvent);
    }
}

}
