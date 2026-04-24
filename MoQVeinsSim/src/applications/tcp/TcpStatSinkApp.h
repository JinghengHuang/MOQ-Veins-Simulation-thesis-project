//
// Copyright (C) 2004 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __MOQSIM_TCPSTATSINKAPP_H
#define __MOQSIM_TCPSTATSINKAPP_H

#include "inet/applications/tcpapp/TcpGenericServerApp.h"

using namespace inet;

/**
 * Accepts any number of incoming connections, and discards whatever arrives
 * on them.
 */
class TcpStatSinkApp : public TcpGenericServerApp
{
  protected:
    long bytesRcvd = 0;

  protected:
    virtual void sendBack(cMessage *msg) override;
    friend class TcpServerThreadBase;
};


#endif

