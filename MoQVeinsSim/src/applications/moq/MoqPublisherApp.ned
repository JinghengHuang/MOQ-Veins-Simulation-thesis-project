package moqveinssim.applications.moq;

import moqveinssim.applications.base.BasePublisherApp;
import inet.applications.contract.IApp;

simple MoqPublisherApp extends BasePublisherApp like IApp {
    parameters:
        @class(moqveinssim::MoqPublisherApp);
        object tracks = default([]); // list of tracks
}