#include <array>
#include <chrono>
#include <deque>
#include <fstream>
#include <set>
#include <minizip/unzip.h>
static unzFile CatalogOpen(const char *name);
#include "services.h"
extern "C" unsigned long long catalog_header_parses;
extern "C" void catalog_private_state(unzFile,unsigned long long *);
extern "C" unsigned long long catalog_stream_position(unzFile);
struct Counters { zlib_filefunc64_def base; uint64_t reads=0,bytes=0,seeks=0; };
static std::deque<Counters> callbacks;
static uLong ZCALLBACK countedRead(voidpf opaque,voidpf file,void *buffer,uLong bytes) {
    auto *c=static_cast<Counters *>(opaque); ++c->reads;
    uLong result=c->base.zread_file(c->base.opaque,file,buffer,bytes); c->bytes+=result; return result;
}
static long ZCALLBACK countedSeek(voidpf opaque,voidpf file,ZPOS64_T offset,int origin) {
    auto *c=static_cast<Counters *>(opaque); ++c->seeks; return c->base.zseek64_file(c->base.opaque,file,offset,origin);
}
static unzFile CatalogOpen(const char *name) {
    callbacks.emplace_back(); auto &c=callbacks.back(); fill_fopen64_filefunc(&c.base); auto api=c.base;
    api.opaque=&c; api.zread_file=countedRead; api.zseek64_file=countedSeek; return unzOpen2_64(name,&api);
}
struct Hash {
    uint64_t value=1469598103934665603ULL;
    std::vector<byte> trace;
    void bytes(const void *p,size_t length) { const auto *b=static_cast<const byte *>(p); trace.insert(trace.end(),b,b+length);while(length--) value=(value^*b++)*1099511628211ULL; }
    void number(uint64_t n) { for(int i=0;i<8;++i){const byte b=byte(n);bytes(&b,1);n>>=8;} }
    void text(const char *s) {bytes(s,std::strlen(s)+1);}
};
struct Memory {
    const std::vector<byte> *bytes; uint64_t position=0; size_t cap=0; bool errorAtEnd=false; uint64_t failedSeek=~uint64_t(0);
};
static voidpf ZCALLBACK memOpen(voidpf opaque,const void *,int) {return opaque;}
static uLong ZCALLBACK memRead(voidpf,voidpf stream,void *buffer,uLong bytes) {
    auto *m=static_cast<Memory *>(stream); size_t remaining=m->position<m->bytes->size()?m->bytes->size()-size_t(m->position):0;
    size_t amount=std::min<size_t>(bytes,remaining); if(m->cap) amount=std::min(amount,m->cap);
    if(amount) std::memcpy(buffer,m->bytes->data()+m->position,amount);m->position+=amount;return uLong(amount);
}
static long ZCALLBACK memSeek(voidpf,voidpf stream,ZPOS64_T offset,int origin) {
    auto *m=static_cast<Memory *>(stream);
    int64_t position=origin==ZLIB_FILEFUNC_SEEK_SET?int64_t(offset):origin==ZLIB_FILEFUNC_SEEK_CUR?int64_t(m->position)+int64_t(offset):int64_t(m->bytes->size())+int64_t(offset);
    if(position<0||uint64_t(position)==m->failedSeek)return -1;m->position=uint64_t(position);return 0;
}
static ZPOS64_T ZCALLBACK memTell(voidpf,voidpf stream){return static_cast<Memory *>(stream)->position;}
static int ZCALLBACK memClose(voidpf,voidpf){return 0;}
static int ZCALLBACK memError(voidpf,voidpf stream){const auto *m=static_cast<Memory *>(stream);return m->errorAtEnd&&m->position>=m->bytes->size();}
static unzFile openMemory(Memory &memory) {
    zlib_filefunc64_def api={};api.opaque=&memory;api.zopen64_file=memOpen;api.zread_file=memRead;
    api.ztell64_file=memTell;api.zseek64_file=memSeek;api.zclose_file=memClose;api.zerror_file=memError;return unzOpen2_64("memory",&api);
}
static std::array<unsigned long long,10> state(unzFile file){std::array<unsigned long long,10>s;catalog_private_state(file,s.data());return s;}
static std::vector<byte> readBytes(const std::filesystem::path &path){std::ifstream f(path,std::ios::binary);CHECK(f.good());return std::vector<byte>((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());}
static void apiEquivalent(const std::vector<byte> &data,size_t capacity,size_t chunk,bool errorAtEnd) {
    Memory oldMemory{&data},newMemory{&data};oldMemory.cap=newMemory.cap=chunk;oldMemory.errorAtEnd=newMemory.errorAtEnd=errorAtEnd;
    unzFile oldFile=openMemory(oldMemory),newFile=openMemory(newMemory);CHECK(bool(oldFile)==bool(newFile));
    if(!oldFile)return;
    bool first=true;
    for(size_t iteration=0;iteration<10000;++iteration) {
        unz_file_info oldInfo,newInfo;std::memset(&oldInfo,0x5a,sizeof(oldInfo));newInfo=oldInfo;
        std::vector<char> oldName(65536,'Z'),newName=oldName;
        const auto before=state(newFile);const auto oldOutput=oldName;
        int expected=first?unzGoToFirstFile(oldFile):unzGoToNextFile(oldFile);
        if(expected==UNZ_OK)expected=unzGetCurrentFileInfo(oldFile,&oldInfo,oldName.data(),uLong(capacity),nullptr,0,nullptr,0);
        int actual=first?unzGoToFirstFileInfo(newFile,&newInfo,newName.data(),uLong(capacity)):unzGoToNextFileInfo(newFile,&newInfo,newName.data(),uLong(capacity));
        CHECK(actual==expected&&oldName==newName&&std::memcmp(&oldInfo,&newInfo,sizeof(oldInfo))==0);
        if(actual==UNZ_END_OF_LIST_OF_FILE){CHECK(state(newFile)==before&&newName==oldOutput);break;}
        if(actual!=UNZ_OK){
            CHECK(!state(newFile)[2]); // New combined API explicitly invalidates on any parse error.
            const auto failed=state(newFile);CHECK(unzGoToNextFileInfo(newFile,&newInfo,newName.data(),uLong(capacity))==UNZ_END_OF_LIST_OF_FILE);CHECK(state(newFile)==failed);break;
        }
        CHECK(state(oldFile)==state(newFile)&&catalog_stream_position(oldFile)==catalog_stream_position(newFile));
        unz64_file_pos oldPosition,newPosition;CHECK(unzGetFilePos64(oldFile,&oldPosition)==UNZ_OK&&unzGetFilePos64(newFile,&newPosition)==UNZ_OK);
        CHECK(oldPosition.pos_in_zip_directory==newPosition.pos_in_zip_directory&&oldPosition.num_of_file==newPosition.num_of_file);
        CHECK(unzGetOffset(oldFile)==unzGetOffset(newFile));
        // Ordinary saved-position navigation can resume the new iterator.
        CHECK(unzGoToFilePos64(oldFile,&oldPosition)==UNZ_OK&&unzGoToFilePos64(newFile,&newPosition)==UNZ_OK);
        CHECK(state(oldFile)==state(newFile));first=false;
        if(iteration==9999)CHECK(false);
    }
    CHECK(unzClose(oldFile)==UNZ_OK&&unzClose(newFile)==UNZ_OK);
}
static void apiChecks(const std::filesystem::path &directory) {
    unz_file_info info,before;std::memset(&info,0x5a,sizeof(info));before=info;char name[8]="kept";
    CHECK(unzGoToFirstFileInfo(nullptr,&info,name,sizeof(name))==UNZ_PARAMERROR&&unzGoToNextFileInfo(nullptr,&info,name,sizeof(name))==UNZ_PARAMERROR);
    CHECK(std::memcmp(&info,&before,sizeof(info))==0&&!std::strcmp(name,"kept"));
    size_t tested=0;
    for(const auto &entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==".zip"||entry.path().extension()==".pk3") {
        const auto bytes=readBytes(entry.path());apiEquivalent(bytes,65536,0,false);++tested;
    }
    const auto bytes=readBytes(directory/"catalog.pk3");
    for(size_t capacity:{0u,1u,4u,5u,6u,256u})for(size_t cap:{0u,1u,2u,4u,8u})for(bool error:{false,true})apiEquivalent(bytes,capacity,cap,error);
    Memory left{&bytes},right{&bytes};unzFile a=openMemory(left),b=openMemory(right);CHECK(a&&b);
    CHECK(unzGoToFirstFile(a)==UNZ_OK&&unzGetCurrentFileInfo(a,nullptr,nullptr,0,nullptr,0,nullptr,0)==UNZ_OK);
    CHECK(unzGoToFirstFileInfo(b,nullptr,nullptr,0)==UNZ_OK&&state(a)==state(b)&&left.position==right.position);
    // Seek errors propagate without output changes or automatic retry.
    left.failedSeek=right.failedSeek=state(a)[0]+state(a)[4];
    CHECK(unzGoToFirstFile(a)==unzGoToFirstFileInfo(b,&info,name,sizeof(name))&&!state(b)[2]);
    unzClose(a);unzClose(b);CHECK(liveAllocations==0);
    std::printf("Catalogue API: %zu archive fixtures, partial callbacks, EOF/error, buffers, private state, cursor, ZIP64/SFX and saved-position checks passed.\n",tested);
}
static void payloadChecks(const std::filesystem::path &directory) {
    const auto bytes=readBytes(directory/"catalog.pk3");Memory memory{&bytes};unzFile file=openMemory(memory);CHECK(file);
    unz_file_info info;char name[256];int result=unzGoToFirstFileInfo(file,&info,name,sizeof(name));int entries=0;
    while(result==UNZ_OK){CHECK(unzOpenCurrentFile(file)==UNZ_OK);char buffer[4096];int read;uint64_t size=0;uLong crc=crc32(0,Z_NULL,0);
        while((read=unzReadCurrentFile(file,buffer,sizeof(buffer)))>0){size+=read;crc=crc32(crc,reinterpret_cast<Bytef *>(buffer),uInt(read));}
        CHECK(read==0&&size==info.uncompressed_size&&crc==info.crc&&unzCloseCurrentFile(file)==UNZ_OK);++entries;
        result=unzGoToNextFileInfo(file,&info,name,sizeof(name));}
    CHECK(result==UNZ_END_OF_LIST_OF_FILE&&entries==7&&unzClose(file)==UNZ_OK&&liveAllocations==0);
}
struct Scan {uint64_t fingerprint=0,parses=0,reads=0,bytes=0,seeks=0;size_t archives=0,entries=0;double ms=0;std::vector<byte> trace;};
static void setting(bool fast) {
    if(fs_searchpaths)FS_Shutdown(qtrue);CHECK(liveAllocations==0);callbacks.clear();cvars.clear();fs_numServerPaks=0;fs_reordered=false;fs_packFiles=0;
    fs_debug=Cvar_Get("fs_debug","0",0);fs_copyfiles=Cvar_Get("fs_copyfiles","0",0);fs_dirbeforepak=Cvar_Get("fs_dirbeforepak","0",0);
    fs_cdpath=Cvar_Get("fs_cdpath","",0);fs_basepath=Cvar_Get("fs_basepath","",0);fs_homepath=Cvar_Get("fs_homepath","",0);fs_gamedirvar=Cvar_Get("fs_game","",0);
    fs_lookupIndex=Cvar_Get("fs_lookupIndex","1",0);fs_restartCache=Cvar_Get("fs_restartCache","1",0);fs_zipCatalogFast=Cvar_Get("fs_zipCatalogFast",fast?"1":"0",0);
    FSRestartMetadataCache::recordCatalogs=true;fs_checksumFeed=0x12345678;
}
static void fingerprintPack(Hash &hash,const pack_t *pack) {
    hash.text(pack->pakFilename);hash.number(pack->checksum);hash.number(pack->pure_checksum);hash.number(pack->numfiles);hash.number(pack->referenced);hash.number(pack->hashSize);
    hash.number(pack->numChecksumHeaders);for(int i=0;i<pack->numChecksumHeaders;++i)hash.number(uint32_t(pack->checksumHeaders[i]));
    for(int i=0;i<pack->numfiles;++i){const auto &entry=pack->buildBuffer[i];hash.text(entry.name);hash.number(entry.pos);hash.number(entry.len);hash.number(pack->nameIndex[i]);}
    for(int i=0;i<pack->hashSize;++i){hash.number(i);for(const fileInPack_t *entry=pack->hashTable[i];entry;entry=entry->next)hash.number(entry-pack->buildBuffer);hash.number(~uint64_t(0));}
}
static void packChecks(const std::filesystem::path &directory) {
    for(const auto &entry:std::filesystem::directory_iterator(directory))if(entry.path().extension()==".zip"||entry.path().extension()==".pk3") {
        Hash hash[2];bool mounted[2]={};
        for(int fast=0;fast<2;++fast){setting(fast!=0);pack_t *pack=FS_LoadZipFile(entry.path().string().c_str(),"fixture.pk3");mounted[fast]=pack!=nullptr;
            if(pack){fingerprintPack(hash[fast],pack);FS_FreePak(pack);}CHECK(liveAllocations==0);}
        CHECK(mounted[0]==mounted[1]&&hash[0].trace==hash[1].trace);
        const auto file=entry.path().filename().string();if(file=="catalog.pk3"||file=="catalog-sfx.pk3")CHECK(mounted[1]);
        if(file=="catalog-long.pk3"||file=="catalog-damaged.pk3"||file=="catalog-truncated-name.pk3")CHECK(!mounted[1]);
    }
}
static void rawMetadata(const pack_t *pack) {
    unzFile legacy=unzOpen(pack->pakFilename),fast=unzOpen(pack->pakFilename);CHECK(legacy&&fast);
    int expected=unzGoToFirstFile(legacy);unz_file_info current;char name[256];
    int actual=unzGoToFirstFileInfo(fast,&current,name,sizeof(name));int header=1;
    for(int i=0;i<pack->numfiles;++i){
        CHECK(expected==UNZ_OK&&actual==UNZ_OK);unz_file_info info;std::memset(&info,0,sizeof(info));char oldName[256];
        CHECK(unzGetCurrentFileInfo(legacy,&info,oldName,sizeof(oldName),nullptr,0,nullptr,0)==UNZ_OK);
        CHECK(!std::strcmp(name,oldName)&&!std::strcmp(name,pack->buildBuffer[i].name));
        CHECK(current.version==info.version&&current.version_needed==info.version_needed&&current.flag==info.flag&&current.compression_method==info.compression_method&&current.dosDate==info.dosDate&&current.crc==info.crc);
        CHECK(current.compressed_size==info.compressed_size&&current.uncompressed_size==info.uncompressed_size&&current.size_filename==info.size_filename&&current.size_file_extra==info.size_file_extra&&current.size_file_comment==info.size_file_comment);
        CHECK(current.disk_num_start==info.disk_num_start&&current.internal_fa==info.internal_fa&&current.external_fa==info.external_fa&&std::memcmp(&current.tmu_date,&info.tmu_date,sizeof(info.tmu_date))==0);
        CHECK(pack->buildBuffer[i].len==info.uncompressed_size&&pack->buildBuffer[i].pos==unzGetOffset(legacy)&&unzGetOffset(legacy)==unzGetOffset(fast));
        CHECK(state(legacy)==state(fast)&&catalog_stream_position(legacy)==catalog_stream_position(fast));
        if(info.uncompressed_size){CHECK(header<pack->numChecksumHeaders&&pack->checksumHeaders[header++]==LittleLong(info.crc));}
        expected=unzGoToNextFile(legacy);actual=unzGoToNextFileInfo(fast,&current,name,sizeof(name));
    }
    CHECK(expected==actual&&header==pack->numChecksumHeaders);unzClose(legacy);unzClose(fast);
}
static Scan mountedScan(const std::filesystem::path &directory,bool fast) {
    setting(fast);catalog_header_parses=0;
    const auto start=std::chrono::steady_clock::now();FS_AddGameDirectory(directory.parent_path().string().c_str(),directory.filename().string().c_str());
    Scan result;result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();result.parses=catalog_header_parses;
    for(const auto &c:callbacks){result.reads+=c.reads;result.bytes+=c.bytes;result.seeks+=c.seeks;}
    Hash hash;std::vector<std::string> names;std::vector<pack_t *> packs;
    for(searchpath_t *search=fs_searchpaths;search;search=search->next){
        if(!search->pack){hash.text(search->dir->fullpath);continue;}pack_t *p=search->pack;packs.push_back(p);++result.archives;result.entries+=p->numfiles;fingerprintPack(hash,p);rawMetadata(p);
        for(int i=0;i<p->numfiles;++i)names.emplace_back(p->buildBuffer[i].name);
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    for(int pure=0;pure<2;++pure){
        fs_numServerPaks=0;
        if(pure)for(size_t i=0;i<packs.size();i+=2){CHECK(fs_numServerPaks<int(ARRAY_LEN(fs_serverPaks)));fs_serverPaks[fs_numServerPaks++]=packs[i]->checksum;}
        fs_pakLookupIndex.invalidate();FS_ClearPakReferences(0);
        for(const auto &name:names){fsReadFileIdentity_t id={};const qboolean found=FS_GetReadFileIdentity(name.c_str(),&id,qtrue);hash.number(found);if(found){hash.number(id.position);hash.number(id.size);hash.number(id.provider);
            const pack_t *owner=nullptr;for(const auto *p:packs)if(p->sourceIdentity==id.container){owner=p;break;}hash.text(owner?owner->pakFilename:"loose");}}
        for(const auto *p:packs)hash.number(p->referenced);
    }
    // Real file-open reference policy and duplicate winner on the synthetic mount.
    fs_numServerPaks=0;FS_ClearPakReferences(0);fs_pakLookupIndex.invalidate();
    for(const auto *name:{"same.bin","uix86.dll","cgamex86.dll"}){
        int handle=0;const long length=FS_FOpenFileRead(name,&handle,qtrue);hash.number(uint64_t(length));if(handle)FS_FCloseFile(handle);
    }
    for(const auto *p:packs)hash.number(p->referenced);
    result.fingerprint=hash.value;result.trace=std::move(hash.trace);FS_Shutdown(qtrue);CHECK(liveAllocations==0);callbacks.clear();return result;
}
static void compareMounts(const std::filesystem::path &directory) {
    const auto legacy=mountedScan(directory,false),fast=mountedScan(directory,true);
    CHECK(legacy.trace==fast.trace&&legacy.archives==fast.archives&&legacy.entries==fast.entries);
    CHECK(legacy.parses==2*legacy.entries+legacy.archives&&fast.parses==fast.entries+fast.archives);
    CHECK(legacy.parses-fast.parses==legacy.entries&&fast.reads<legacy.reads&&fast.seeks<legacy.seeks);
    std::printf("{\"directory\":\"%s\",\"archives\":%zu,\"entries\":%zu,\"fingerprint\":\"%016llx\",\"legacy\":{\"parses\":%llu,\"reads\":%llu,\"bytes\":%llu,\"seeks\":%llu,\"ms\":%.6f},\"fast\":{\"parses\":%llu,\"reads\":%llu,\"bytes\":%llu,\"seeks\":%llu,\"ms\":%.6f}}\n",directory.generic_string().c_str(),legacy.archives,legacy.entries,(unsigned long long)legacy.fingerprint,(unsigned long long)legacy.parses,(unsigned long long)legacy.reads,(unsigned long long)legacy.bytes,(unsigned long long)legacy.seeks,legacy.ms,(unsigned long long)fast.parses,(unsigned long long)fast.reads,(unsigned long long)fast.bytes,(unsigned long long)fast.seeks,fast.ms);
}
int main(int argc,char **argv) {
    CHECK(argc==2||argc==3);const std::filesystem::path fixtures=argv[1];apiChecks(fixtures);payloadChecks(fixtures);packChecks(fixtures);
    const auto mount=fixtures/"mount";std::filesystem::create_directories(mount);
    std::filesystem::copy_file(fixtures/"catalog.pk3",mount/"pak0.pk3",std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(fixtures/"catalog-sfx.pk3",mount/"pak9.pk3",std::filesystem::copy_options::overwrite_existing);
    compareMounts(mount);if(argc==3)compareMounts(std::filesystem::canonical(argv[2]));
    CHECK(liveAllocations==0);std::puts("Actual FS catalogue metadata/order/checksums/pure/reference/lookup equality and eliminated-parse checks passed.");
}
