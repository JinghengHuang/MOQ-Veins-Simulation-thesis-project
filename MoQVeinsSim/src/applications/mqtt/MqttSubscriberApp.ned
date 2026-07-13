package moqveinssim.applications.mqtt;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

//
// MQTT v5.0 subscriber. Statistics mirror MoqSubscriberApp exactly (shared implementation),
// so the protocols can be compared on identical metrics.
//
simple MqttSubscriberApp extends SimpleModule like IApp {
    parameters:
        @class(::moqveinssim::MqttSubscriberApp);
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("tcp");
        int qos = default(0);
        string localAddress = default("");
        int localPort = default(9102);
        string connectAddress = default("");
        int connectPort = default(9100);
        double connectTime @unit(s) = default(0.1s);
        object tracks;

        @signal[endToEndLatency](type=double);
        @statistic[endToEndLatency](title="End-to-end message latency"; unit=s; record=mean,max,min,histogram,vector);
        @signal[e2eJitter](type=double);
        @statistic[e2eJitter](title="Inter-message latency jitter"; unit=s; record=mean,max,histogram);
        @signal[deadlineMiss](type=long);
        @statistic[deadlineMiss](title="Deadline miss (1/0)"; record=mean,count,sum);
    gates:
        input socketIn;
        output socketOut;
}
