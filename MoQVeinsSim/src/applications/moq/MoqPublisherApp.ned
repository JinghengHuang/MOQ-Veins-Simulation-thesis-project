package moqveinssim.applications.moq;

import moqveinssim.applications.base.BasePublisherApp;
import inet.applications.contract.IApp;

simple MoqPublisherApp extends BasePublisherApp like IApp {
    parameters:
        @class(::moqveinssim::MoqPublisherApp);
        @signal[objectSent](type=long);
        @statistic[objectSent](title="Objects offered to network"; unit=B; record=count,sum);
        object tracks = default([]); // list of tracks
}