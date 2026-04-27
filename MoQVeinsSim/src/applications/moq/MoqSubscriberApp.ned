package moqveinssim.applications.moq;

import inet.applications.quicapp.QuicClient;

simple MoqSubscriberApp extends QuicClient {
    @class(MoqSubscriberApp);
    @signal[bytesReceived];
    @statistic[bytesReceived](record=vector);
}