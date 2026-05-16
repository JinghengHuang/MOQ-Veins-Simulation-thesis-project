
import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
namespace moqveinssim;

class MoqPublisherAnnounce extends inet::FieldsChunk
{
    long publisherId;
    long trackId;
    string trackNamespace;
    string trackName;
    string trackAlias;
    string objectType;
    long payloadSize;
    simtime_t sendInterval;
    long priority;
}