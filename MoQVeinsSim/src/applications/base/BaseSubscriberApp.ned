package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BaseSubscriberApp extends SimpleModule like IApp  {
    parameters:
        @display("i=block/app");
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9002); // port number to listen on
        string connectAddress = default(""); // Relay/Server address or empty ("")
        int connectPort = default(9001); // Relay/Server port number

    gates:
        input socketIn;
        output socketOut;
}