package moqveinssim.applications.moq;

import moqveinssim.applications.base.BaseSubscriberApp;
import inet.applications.contract.IApp;

simple MoqSubscriberApp extends BaseSubscriberApp  like IApp {
    parameters:
        @class(::moqveinssim::MoqSubscriberApp);
        @signal[endToEndLatency](type=double);
        @statistic[endToEndLatency](title="End-to-end latency per object"; unit=s; record=mean,max,min,histogram);
        @signal[objectCompletionTime](type=double);
        @statistic[objectCompletionTime](title="Object completion time (first->last fragment)"; unit=s; record=mean,max,min,histogram);
        @signal[e2eJitter](type=double);
        @statistic[e2eJitter](title="End-to-end jitter (|L_i - L_i-1|)"; unit=s; record=mean,max,histogram);
        @signal[deadlineMiss](type=long);
        @statistic[deadlineMiss](title="Deadline miss (1=miss); mean=miss ratio"; record=mean,count,sum);
        object tracks = default([]); // list of tracks to subscribe to
}