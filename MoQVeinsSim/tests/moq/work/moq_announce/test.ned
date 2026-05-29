
import inet.node.inet.StandardHost;
import inet.node.ethernet.Eth100M;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;

network TestMoqAnnounce
{
    submodules:
        configurator: Ipv4NetworkConfigurator;
        relay: StandardHost;
        publisher: StandardHost;
    connections:
        publisher.ethg++ <--> Eth100M <--> relay.ethg++;
}

