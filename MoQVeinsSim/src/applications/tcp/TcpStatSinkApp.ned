package moqveinssim.applications.tcp;

import inet.common.SimpleModule;
import inet.applications.tcpapp.TcpGenericServerApp;
//
// TCP sink, but with stats for evaluating metrics
module TcpStatSinkApp extends TcpGenericServerApp
{
    parameters:
        @class(TcpStatSinkApp);
}

