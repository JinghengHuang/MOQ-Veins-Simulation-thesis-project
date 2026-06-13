package moqveinssim.applications.moq;

import moqveinssim.applications.base.BaseRelayApp;
import inet.applications.contract.IApp;
simple MoqRelayApp extends BaseRelayApp like IApp  {
    parameters:
        @class(::moqveinssim::MoqRelayApp);
        @signal[relayQueueDepth](type=long);
        @statistic[relayQueueDepth](title="Relay reassembly backlog (objects)"; record=vector,max,timeavg);
        @signal[relayForwardDelay](type=double);
        @statistic[relayForwardDelay](title="Relay store-and-forward delay"; unit=s; record=mean,max,min,histogram);
        @signal[objectForwarded](type=long);
        @statistic[objectForwarded](title="Objects forwarded downstream"; unit=B; record=count,sum);
}