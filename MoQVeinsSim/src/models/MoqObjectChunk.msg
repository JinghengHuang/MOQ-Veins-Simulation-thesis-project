
import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
namespace moqveinssim;

class MoqObjectChunk extends inet::FieldsChunk
{
    long trackId;
    string trackAlias;
    long groupId;
    long objectId;
    long priority;
    long payloadLength;
    simtime_t creationTime;
}