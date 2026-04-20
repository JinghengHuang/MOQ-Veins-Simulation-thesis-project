package moqveinssim.applications.udp;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;
import inet.applications.udpapp.UdpSink;

simple UdpDatagramSink extends UdpSink
{
    parameters:
        @class(UdpDatagramSink);
        string appName = default("UdpDatagramSink");
}