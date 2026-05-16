/* --- MoqRelayApp.cpp --- */

/* ------------------------------------------
author: undefined
date: 5/15/2026
------------------------------------------ */

#include "MoqRelayApp.h"
#include "inet/networklayer/common/L3AddressResolver.h"
#include "inet/common/packet/chunk/ByteCountChunk.h"

namespace moqveinssim {

Define_Module(MoqRelayApp);

MoqRelayApp::MoqRelayApp() {
    timerConnect = new inet::cMessage("MOQ Publisher Timer - Connect");
    timerConnect->setKind(TIMER_CONNECT);

    timerLimitRuntime = new inet::cMessage("MOQ Publisher Timer - Runtime limit");
    timerLimitRuntime->setKind(TIMER_LIMIT_RUNTIME);
}

MoqRelayApp::~MoqRelayApp() {
    cancelAndDelete(timerConnect);
    cancelAndDelete(timerLimitRuntime);
}

void MoqRelayApp::handleStartOperation(inet::LifecycleOperation *operation)
{
    EV_DEBUG << "initialize MoqRelayApp" << std::endl;

    connectPort = par("connectPort");
    connectAddress = inet::L3AddressResolver().resolve(par("connectAddress"));
    socket.setOutputGate(gate("socketOut"));
    socket.setCallback(this);

    inet::L3Address localAddress = inet::L3AddressResolver().resolve(par("localAddress"));
    int localPort = par("localPort");
    socket.bind(localAddress, localPort);
    
}


void MoqRelayApp::handleStopOperation(inet::LifecycleOperation *operation)
{
    EV_INFO << "handleStopOperation" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqRelayApp::handleCrashOperation(inet::LifecycleOperation *operation)
{
    EV_ERROR << "MOQ Relay FAILED!" << std::endl;
    cancelEvent(timerConnect);
    cancelEvent(timerLimitRuntime);
}

void MoqRelayApp::socketEstablished(inet::QuicSocket *socket) {
    EV_INFO << "socketEstablished" << std::endl;
    sendingAllowed = true;
}

void MoqRelayApp::socketDataArrived(inet::QuicSocket* socket, inet::Packet *packet) {
    EV_DEBUG << "Data arrived" << std::endl;
}

void MoqRelayApp::socketClosed(inet::QuicSocket *socket) {
    EV_INFO << "socketClosed" << std::endl;
}
void MoqRelayApp::socketSendQueueFull(inet::QuicSocket *socket)
{
    sendingAllowed = false;
}

void MoqRelayApp::socketSendQueueDrain(inet::QuicSocket *socket)
{
    sendingAllowed = true;
}


void MoqRelayApp::onSubscribe(std::string sid, std::string trackAlias, long streamId){
    auto response = new inet::Packet("SUBSCRIBE_OK");
    response->insertAtBack(inet::makeShared<inet::ByteCountChunk>(inet::B(1)));

    auto streamReq = response->addTagIfAbsent<inet::QuicStreamReq>();
    streamReq->setStreamID(streamId);

    socket.send(response);
}

void MoqRelayApp::onPublish(std::string pid, TrackMeta){
    
}

void MoqRelayApp::relayTrackData(std::string trackAlias, std::string sid){
    
}
}
