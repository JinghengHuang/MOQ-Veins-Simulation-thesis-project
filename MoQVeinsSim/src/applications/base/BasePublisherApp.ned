package moqveinssim.applications.base;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

simple BasePublisherApp extends SimpleModule like IApp {
    parameters:
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("quic"); // underlying transport: "quic" | "tcp" | "udp"
        int udpFragmentSize @unit(B) = default(1200B); // max inner-frame bytes per UDP datagram
        // QUIC: max objects buffered when the QUIC send queue is full; on overflow the
        // lowest-priority, oldest object is dropped (priority/age-aware shedding).
        int sendBufferLimit = default(2000);
        // QUIC: max bytes of one object handed to the socket per write. Must stay well under the
        // quic module's sendQueueLimit, whose admission gate is connection-wide and all-or-nothing:
        // a single write larger than that limit locks every stream out, high-priority ones included,
        // until ACKs drain it. Chunking also keeps the backlog in this app's priority-ordered buffer,
        // where it can still be reordered and shed, rather than stranded inside QUIC.
        int quicChunkBytes @unit(B) = default(16384B);
        string localAddress = default(""); // Local address or empty ("")
        int localPort = default(9000); // port number to listen on
        string connectAddress = default(""); // Relay/Server address or empty ("")
        int connectPort = default(9001); // Relay/Server port number

        double connectTime @unit(s) = default(0.1s);
    gates:
        input socketIn;
        output socketOut;
}