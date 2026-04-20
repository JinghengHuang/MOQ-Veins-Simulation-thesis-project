package moqveinssim.applications.udp;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;
import inet.applications.udpapp.UdpBasicApp;

simple UdpApp extends UdpBasicApp
{
    parameters:
        @class(UdpApp);
        string appName = default("UdpApp");
}