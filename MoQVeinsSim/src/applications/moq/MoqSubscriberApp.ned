package moqveinssim.applications.moq;

import moqveinssim.applications.base.BaseSubscriberApp;
import inet.applications.contract.IApp;

simple MoqSubscriberApp extends BaseSubscriberApp  like IApp {
    parameters:
        @class(::moqveinssim::MoqSubscriberApp);
        object tracks = default([]); // list of tracks to subscribe to
}