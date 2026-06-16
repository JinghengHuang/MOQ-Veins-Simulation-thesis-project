package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BasePublisherApp extends SimpleModule like IApp {
    parameters:
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("quic"); // underlying transport: "quic" | "tcp" | "udp"
        int udpFragmentSize @unit(B) = default(1200B); // max inner-frame bytes per UDP datagram
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9000); // port number to listen on
        string connectAddress = default(""); // Relay/Server address or empty ("")
        int connectPort = default(9001); // Relay/Server port number

        double connectTime @unit(s) = default(0.1s);
    gates:
        input socketIn;
        output socketOut;
}