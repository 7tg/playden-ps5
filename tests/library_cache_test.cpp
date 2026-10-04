#include "library/cache.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
using namespace playden;
void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
 char root[]="/tmp/playden-cache-XXXXXX";Check(mkdtemp(root),"temporary root");
 Storage storage(root);LibraryCache cache(storage);
 LibrarySnapshot original{1,123456,{{10,"Portal","https://example.invalid/10.jpg","Puzzle","Valve"},{20,"星の旅","","Travel","Studio"}}};
 Check(cache.Save("76561198000000001",original).ok(),"save a real account snapshot");
 auto restored=cache.Load("76561198000000001");Check(restored.ok(),"load snapshot after creating a new cache reader");
 Check(restored.value().games.size()==2 && restored.value().games[1].title=="星の旅","Unicode title roundtrip");
 Check(restored.value().retrieved_at_unix_seconds==123456,"stale timestamp preserved");
 Check(!cache.Load("76561198000000002").ok(),"another account never receives first account data");
 Check(!cache.Save("../escape",original).ok(),"reject account path traversal");
 auto bad=original;bad.schema_version=99;Check(!cache.Save("76561198000000001",bad).ok(),"unsupported schema cannot replace valid cache");
 Check(cache.Load("76561198000000001").ok(),"rejected update preserves previous snapshot");
 bad=original;bad.games.push_back(bad.games.front());Check(!cache.Save("76561198000000001",bad).ok(),"duplicate identities rejected before persistence");
 auto bytes=storage.Read("library-76561198000000001.cache");Check(bytes.ok(),"read cache for corruption fixture");
 auto corrupt=bytes.value();corrupt[corrupt.size()/2]^=1;Check(storage.WriteAtomic("library-76561198000000001.cache",corrupt).ok(),"write corruption fixture");
 Check(!cache.Load("76561198000000001").ok(),"checksum detects damaged cache");
 Check(storage.WriteAtomic("library-76561198000000002.cache",bytes.value()).ok(),"copy fixture into different account namespace");
 Check(!cache.Load("76561198000000002").ok(),"embedded account rejects wrongly copied cache");
 auto truncated=bytes.value();truncated.resize(10);
 Check(storage.WriteAtomic("library-76561198000000001.cache",truncated).ok(),"write truncated fixture");
 Check(!cache.Load("76561198000000001").ok(),"truncated cache rejected");
 LibrarySnapshot large{1,123458,{}};
 for(unsigned i=1;i<=32;++i)large.games.push_back({i,"Large game","",std::string(40000,'x'),"Studio"});
 Check(cache.Save("76561198000000001",large).ok(),"save snapshot exceeding preference read limit");
 Check(!storage.Read("library-76561198000000001.cache").ok(),"default storage limit remains bounded");
 auto large_restored=cache.Load("76561198000000001");
 Check(large_restored.ok() && large_restored.value().games.size()==32,"cache explicitly reads bounded larger snapshot");
 auto limited=storage.Read("library-76561198000000001.cache",1234);
 Check(!limited.ok() && limited.message().find("1234-byte limit")!=std::string::npos,"read failure reports actual caller bound");
 Check(!storage.Read("library-76561198000000001.cache",0).ok(),"zero read bound rejected");
 Check(!storage.Read("library-76561198000000001.cache",33*1024*1024).ok(),"excessive read bound rejected");
 LibrarySnapshot empty{1,123457,{}};Check(cache.Save("76561198000000001",empty).ok(),"empty library is legitimate cached state");
 Check(cache.Load("76561198000000001").value().games.empty(),"empty library survives restart");
 std::remove((std::string(root)+"/library-76561198000000001.cache").c_str());std::remove((std::string(root)+"/library-76561198000000002.cache").c_str());rmdir(root);
 puts("library_cache_test passed");
}
