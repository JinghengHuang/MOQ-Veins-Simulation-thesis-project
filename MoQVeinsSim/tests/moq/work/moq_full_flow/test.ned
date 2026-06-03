package moq_full_flow;
import inet.node.inet.StandardHost;
import inet.node.ethernet.Eth100M;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;

network TestMoqFullFlow
{
    submodules:
        configurator: Ipv4NetworkConfigurator;
        relay: StandardHost;
        publisher1: StandardHost;
        publisher2: StandardHost;
        subscriber1: StandardHost;
        subscriber2: StandardHost;
    connections:
        publisher1.ethg++  <--> Eth100M <--> relay.ethg++;
        subscriber1.ethg++ <--> Eth100M <--> relay.ethg++;
        publisher2.ethg++  <--> Eth100M <--> relay.ethg++;
        subscriber2.ethg++ <--> Eth100M <--> relay.ethg++;
}

