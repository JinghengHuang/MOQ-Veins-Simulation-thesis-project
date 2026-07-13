package moqveinssim.applications.mqtt;

import inet.common.SimpleModule;
import inet.applications.contract.IApp;

//
// MQTT v5.0 broker. Forwards PUBLISH to the subscribers of a topic, and enforces the
// Message Expiry Interval (deleting a message only if it expires before onward delivery starts).
//
simple MqttBrokerApp extends SimpleModule like IApp {
    parameters:
        @class(::moqveinssim::MqttBrokerApp);
        @display("i=block/app");
        @lifecycleSupport;
        string protocol = default("tcp");
        int qos = default(0);
        string localAddress = default("");
        int localPort = default(9100);
    gates:
        input socketIn;
        output socketOut;
}
