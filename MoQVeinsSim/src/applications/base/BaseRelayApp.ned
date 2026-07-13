package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BaseRelayApp extends SimpleModule like IApp  {
    parameters:
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("quic"); // underlying transport: "quic" | "tcp" | "udp"
        int udpFragmentSize @unit(B) = default(1200B); // max inner-frame bytes per UDP datagram
        // How often the sender sweeps objects it has already written to the transport and resets
        // the stream of any that have exceeded their delivery timeout (MoQ draft-14 10.4.3).
        // Bounds how late a stale object can be reclaimed; it is not the timeout itself.
        double deliveryTimeoutCheckInterval @unit(s) = default(0.05s);
        // QUIC: max bytes of one object handed to a subscriber socket per write. See the same
        // parameter on BasePublisherApp: it keeps a bulk object from filling QUIC's connection-wide
        // send queue on its own and stalling the latency-critical track behind it.
        int quicChunkBytes @unit(B) = default(16384B);
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9001); // port number to listen on
    gates:
        input socketIn;
        output socketOut;
}