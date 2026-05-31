package moq_subscribe;
import inet.node.inet.StandardHost;
import inet.node.ethernet.Eth100M;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;

network TestMoqSubscribe
{
    submodules:
        configurator: Ipv4NetworkConfigurator;
        relay: StandardHost;
        publisher: StandardHost;
        subscriber: StandardHost;
    connections:
        publisher.ethg++  <--> Eth100M <--> relay.ethg++;
        subscriber.ethg++ <--> Eth100M <--> relay.ethg++;
}

