
import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
namespace moqveinssim;

class MoqTrackHeader extends inet::FieldsChunk
{
    long trackId;
    string trackName;
    long objectId;
    long payloadSize;
    simtime_t sendInterval;
    simtime_t creationTime;
    long priority;
}