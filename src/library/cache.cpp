#include "library/cache.h"
#include <algorithm>
#include <limits>
#include <unordered_set>

namespace playden {
namespace {
constexpr std::size_t kMaximumCacheBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaximumGames = 100000;
const Bytes kMagic = {'P','D','L','I','B',0,1,0};
bool ValidAccount(const std::string& account) {
    return !account.empty() && account.size() <= 20 && account[0] != '0' &&
        std::all_of(account.begin(),account.end(),[](char c){return c>='0'&&c<='9';});
}
std::string Path(const std::string& account) { return "library-"+account+".cache"; }
std::uint32_t Checksum(const Bytes& bytes,std::size_t size) {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<size;++i){crc^=bytes[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}
    return ~crc;
}
void Number(Bytes& out,std::uint64_t value,unsigned count) {
    for(unsigned i=0;i<count;++i){out.push_back(static_cast<std::uint8_t>(value));value>>=8;}
}
void String(Bytes& out,const std::string& value) {
    Number(out,value.size(),4);out.insert(out.end(),value.begin(),value.end());
}
struct Reader {
    const Bytes& bytes;
    std::size_t offset=0,limit=0;
    bool okay=true;
    std::uint64_t Number(unsigned count) {
        if(!okay||offset>limit||count>limit-offset){okay=false;return 0;}
        std::uint64_t value=0;
        for(unsigned i=0;i<count;++i)value|=std::uint64_t(bytes[offset++])<<(i*8);
        return value;
    }
    std::string String(std::size_t maximum) {
        auto count=Number(4);
        if(!okay||count>maximum||offset>limit||count>limit-offset){okay=false;return {};}
        std::string value(bytes.begin()+offset,bytes.begin()+offset+count);offset+=count;return value;
    }
};
bool ValidGame(const GameSummary& game) {
    return game.id!=0 && !game.title.empty() && game.title.size()<=4096 &&
        game.artwork_source.size()<=8192 && game.description.size()<=65536 && game.developer.size()<=4096;
}
Result<void> Invalid(const char* message) {return Result<void>::Failure(StorageError::kInvalidData,message);}
}
Result<void> LibraryCache::Save(const std::string& account,const LibrarySnapshot& snapshot) const {
    if(!ValidAccount(account))return Invalid("Invalid cache account identifier.");
    if(snapshot.schema_version!=1||snapshot.games.size()>kMaximumGames)return Invalid("Unsupported library cache schema or size.");
    Bytes encoded=kMagic;
    String(encoded,account);Number(encoded,snapshot.retrieved_at_unix_seconds,8);Number(encoded,snapshot.games.size(),4);
    std::unordered_set<GameId> seen;
    for(const auto& game:snapshot.games){
        if(!ValidGame(game)||!seen.insert(game.id).second)return Invalid("Invalid or duplicate cached game.");
        Number(encoded,game.id,4);String(encoded,game.title);String(encoded,game.artwork_source);String(encoded,game.description);String(encoded,game.developer);
        if(encoded.size()>kMaximumCacheBytes-4)return Invalid("Library cache exceeds its storage limit.");
    }
    Number(encoded,Checksum(encoded,encoded.size()),4);
    return storage_.WriteAtomic(Path(account),encoded);
}
Result<LibrarySnapshot> LibraryCache::Load(const std::string& account) const {
    auto invalid=[](){return Result<LibrarySnapshot>::Failure(StorageError::kInvalidData,"Library cache is corrupt or belongs to another account.");};
    if(!ValidAccount(account))return invalid();
    const auto read=storage_.Read(Path(account),kMaximumCacheBytes);
    if(!read.ok())return Result<LibrarySnapshot>::Failure(read.error(),read.message());
    const auto& bytes=read.value();
    if(bytes.size()<kMagic.size()+4||!std::equal(kMagic.begin(),kMagic.end(),bytes.begin()))return invalid();
    Reader tail{bytes,bytes.size()-4,bytes.size()};
    if(tail.Number(4)!=Checksum(bytes,bytes.size()-4))return invalid();
    Reader reader{bytes,kMagic.size(),bytes.size()-4};
    if(reader.String(20)!=account)return invalid();
    LibrarySnapshot snapshot;
    snapshot.retrieved_at_unix_seconds=reader.Number(8);
    const auto count=reader.Number(4);
    if(!reader.okay||count>kMaximumGames)return invalid();
    std::unordered_set<GameId> seen;
    for(std::uint64_t i=0;i<count;++i){
        GameSummary game;
        game.id=static_cast<GameId>(reader.Number(4));game.title=reader.String(4096);
        game.artwork_source=reader.String(8192);game.description=reader.String(65536);game.developer=reader.String(4096);
        if(!reader.okay||!ValidGame(game)||!seen.insert(game.id).second)return invalid();
        snapshot.games.push_back(std::move(game));
    }
    if(reader.offset!=reader.limit)return invalid();
    return Result<LibrarySnapshot>::Success(std::move(snapshot));
}
} // namespace playden
