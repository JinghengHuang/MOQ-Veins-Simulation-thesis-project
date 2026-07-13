package moqveinssim.applications.mqtt;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

//
// MQTT v5.0 publisher. Publishes one topic per configured track.
//
simple MqttPublisherApp extends SimpleModule like IApp {
    parameters:
        @class(::moqveinssim::MqttPublisherApp);
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("tcp");   // "tcp" (standard MQTT) or "quic"
        int qos = default(0);               // 0 = at most once, 1 = at least once (QoS 2 not modelled)
        string localAddress = default("");
        int localPort = default(9101);
        string connectAddress = default("");
        int connectPort = default(9100);
        double connectTime @unit(s) = default(0.1s);
        int sendQueueLimit = default(20000);          // outbound queue bound, in messages
        double flushInterval @unit(s) = default(0.01s); // retry writing the outbound queue
        object tracks;
    gates:
        input socketIn;
        output socketOut;
}
