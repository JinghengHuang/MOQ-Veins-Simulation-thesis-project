
import inet.common.INETDefs;
import inet.common.packet.chunk.Chunk;
namespace moqveinssim;

class MoqSubscriber extends inet::FieldsChunk
{
    string subscriberId;
    string trackNamespace;
    string trackName;
    long subscriberPriority;
    long startObjectId;
}