package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BaseRelayApp extends SimpleModule like IApp  {
    parameters:
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9001); // port number to listen on
        string connectAddress = default(""); // Server address or empty ("")
        int connectPort = default(9000); // Server port number
    gates:
        input socketIn;
        output socketOut;
}