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
}
