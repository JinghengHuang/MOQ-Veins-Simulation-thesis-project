//
// Copyright (C) 2004 OpenSim Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later
//


#ifndef __MOQSIM_TCPCLIENTAPP_H
#define __MOQSIM_TCPCLIENTAPP_H

#include "inet/applications/tcpapp/TcpBasicClientApp.h"

using namespace inet;
/**
 * An example request-reply based client application.
 */
class TcpClientApp : public TcpBasicClientApp
{
  protected:

    virtual void sendRequest();

};


#endif

