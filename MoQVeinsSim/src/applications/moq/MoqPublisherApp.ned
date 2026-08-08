package moqveinssim.applications.moq;

import moqveinssim.applications.base.BasePublisherApp;
import inet.applications.contract.IApp;

simple MoqPublisherApp extends BasePublisherApp like IApp {
    parameters:
        @class(::moqveinssim::MoqPublisherApp);
        @signal[objectSent](type=long);
        @statistic[objectSent](title="Objects offered to network"; unit=B; record=count,sum);
        // App send-buffer occupancy over time. Mirrors the relay's relayQueueDepth. The slope is
        // what says whether delivery-timeout shedding actually BOUNDS the backlog (plateau) or
        // merely slows its growth (ramp) -- endpoint counts alone cannot distinguish the two.
        @signal[sendBufferDepth](type=long);
        @statistic[sendBufferDepth](title="Publisher send-buffer backlog (objects)"; record=vector,max,timeavg);
        object tracks = default([]); // list of tracks
}