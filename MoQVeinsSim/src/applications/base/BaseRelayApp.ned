package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BaseRelayApp extends SimpleModule like IApp  {
    parameters:
        @display("i=block/app");
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9001); // port number to listen on
    gates:
        input socketIn;
        output socketOut;
}