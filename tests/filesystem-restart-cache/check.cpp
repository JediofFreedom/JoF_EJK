#include <algorithm>
#include <cassert>
#include <cctype>
#include <climits>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <minizip/unzip.h>
#include "filesystem_archive_identity.h"
#include "filesystem_pak_index.h"
#include "filesystem_loose_probe.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1); } } while(0)
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
#define QDECL
#define BASEGAME "base"
#define ETERNALJKGAME "EternalJK"
#define Q3CONFIG_CFG "eternaljk.cfg"
#define Q3CONFIG_AUTO "autoexec.cfg"
#define FINAL_BUILD
#define S_COLOR_YELLOW ""
typedef int qboolean;
typedef unsigned char byte;
typedef int fileHandle_t;
enum {qfalse,qtrue,TAG_FILESYS,ERR_FATAL,ERR_DROP,FS_GENERAL_REF=1,FS_CGAME_REF=2,FS_UI_REF=4};
enum {CVAR_INIT=1,CVAR_PROTECTED=2,CVAR_ARCHIVE=4,CVAR_SYSTEMINFO=8,CVAR_NORESTART=16};
const int MAX_OSPATH=512,MAX_QPATH=64,MAX_ZPATH=256,MAX_FILEHASH_SIZE=1024,MAX_PAKFILES=1024,MAX_FILE_HANDLES=16;
const char PATH_SEP='/';
static int liveAllocations,closedFiles,centralReads,directoryScans;
static int catalogMode;
static void *Z_Malloc(size_t bytes,int,qboolean=qfalse) {void *p=std::calloc(1,bytes?bytes:1);CHECK(p);++liveAllocations;return p;}
static void Z_Free(void *p) {if(p)--liveAllocations;std::free(p);}
extern "C" void *openjk_minizip_malloc(int bytes) {return Z_Malloc(bytes,TAG_FILESYS);}
extern "C" int openjk_minizip_free(void *p) {Z_Free(p);return 0;}
static int LittleLong(unsigned int value) {return int(value);}
static void Q_strncpyz(char *out,const char *in,size_t size) {std::snprintf(out,size,"%s",in);}
static int Q_stricmp(const char *a,const char *b) {for(;;){int x=std::tolower((unsigned char)*a++),y=std::tolower((unsigned char)*b++);if(x!=y)return x-y;if(!x)return 0;}}
static void Com_Printf(const char *,...) {}
static void Com_DPrintf(const char *,...) {}
static void Com_Error(int code,const char *,...) {throw code;}
static bool com_fullyInitialized=true;
#include "checksum.h"
#include "types.h"
static std::mutex fs_listMutex;
static searchpath_t *fs_searchpaths;
static PakProviderIndex<searchpath_t> fs_pakLookupIndex;
struct Cvar {std::string storage;char *string=nullptr;int integer=0;qboolean modified=qfalse;};
typedef Cvar cvar_t;
static std::map<std::string,Cvar> cvars;
static cvar_t *fs_debug,*fs_copyfiles,*fs_cdpath,*fs_basepath,*fs_basegame,*fs_homepath,*fs_gamedirvar,*fs_dirbeforepak;
static cvar_t *fs_lookupIndex,*fs_restartCache,*fs_zipCatalogFast,*fs_metadataProbe,*fs_loadpakdlls,*fs_globalcfg,*fs_portable,*sv_skipServersideDownloads;
static char fs_cmdlineGame[MAX_QPATH],fs_gamedir[MAX_OSPATH],lastValidBase[MAX_OSPATH],lastValidGame[MAX_OSPATH];
static std::string fs_restartConfiguration;
static int fs_checksumFeed=0x12345678,fs_packFiles,fs_fakeChkSum,fs_numServerPaks,fs_serverPaks[4096];
static bool fs_reordered;
static int fs_temporaryFileWriteIdx;
static char fs_temporaryFileNames[8][MAX_OSPATH];
static std::string fixtureRoot,baseDirectory,homeDirectory;
static std::function<void()> beforeEnumeration;
static std::function<void(const char *)> afterOpen;
static bool throwEnumeration,missingCfgOnce;
static cvar_t *Cvar_Get(const char *name,const char *value,int,const char * = nullptr) {
    auto inserted=cvars.emplace(name,Cvar{});Cvar &v=inserted.first->second;
    if(inserted.second){v.storage=value;v.string=&v.storage[0];v.integer=std::atoi(value);}return &v;
}
static void Cvar_Set(const char *name,const char *value) {
    Cvar *v=Cvar_Get(name,value,0);if(v->storage!=value){v->storage=value;v->string=&v->storage[0];v->integer=std::atoi(value);v->modified=qtrue;}
}
static const char *Cvar_VariableString(const char *name) {auto i=cvars.find(name);return i==cvars.end()?"":i->second.string;}
static int Cvar_VariableIntegerValue(const char *name) {return std::atoi(Cvar_VariableString(name));}
static void Cvar_CheckRange(cvar_t *,int,int,qboolean) {}
static const char *Sys_DefaultInstallPath() {return baseDirectory.c_str();}
static const char *Sys_DefaultHomePath() {return homeDirectory.c_str();}
static bool Sys_PathCmp(const char *a,const char *b) {return !Q_stricmp(a,b);}
static int FS_PathCmp(const char *a,const char *b) {return Q_stricmp(a,b);}
static void FS_CreatePath(const char *path) {std::filesystem::create_directories(path);}
static char *FS_BuildOSPath(const char *base,const char *dir,const char *file) {
    static char values[8][2048];static unsigned next;char *out=values[next++%8];
    std::snprintf(out,2048,"%s/%s/%s",base,dir,file);return out;
}
static char **Sys_ListFiles(const char *directory,const char *,void *,int *count,qboolean) {
    ++directoryScans;if(beforeEnumeration){auto action=std::move(beforeEnumeration);beforeEnumeration={};action();}
    if(throwEnumeration){throwEnumeration=false;throw int(ERR_DROP);}
    std::vector<std::string> names;std::error_code error;
    for(auto &entry:std::filesystem::directory_iterator(directory,error))if(entry.path().extension()==".pk3")names.push_back(entry.path().filename().string());
    *count=int(names.size());char **list=(char **)Z_Malloc((names.size()+1)*sizeof(char *),TAG_FILESYS);
    for(size_t i=0;i<names.size();++i){list[i]=(char *)Z_Malloc(names[i].size()+1,TAG_FILESYS);std::strcpy(list[i],names[i].c_str());}return list;
}
static void Sys_FreeFileList(char **list) {if(list){for(char **p=list;*p;++p)Z_Free(*p);Z_Free(list);}}
static int CountedInfo(unzFile file,unz_file_info *info,char *name,uLong size,void *extra,uLong extraSize,char *comment,uLong commentSize) {
    ++centralReads;return unzGetCurrentFileInfo(file,info,name,size,extra,extraSize,comment,commentSize);
}
static int CountedFirstInfo(unzFile file,unz_file_info *info,char *name,uLong size) {
    int result=unzGoToFirstFileInfo(file,info,name,size);if(result==UNZ_OK)++centralReads;return result;
}
static int CountedNextInfo(unzFile file,unz_file_info *info,char *name,uLong size) {
    int result=unzGoToNextFileInfo(file,info,name,size);if(result==UNZ_OK)++centralReads;return result;
}
static unzFile CountedOpen(const char *name) {unzFile result=unzOpen(name);if(afterOpen){auto action=std::move(afterOpen);afterOpen={};action(name);}return result;}
#define unzGetCurrentFileInfo CountedInfo
#define unzGoToFirstFileInfo CountedFirstInfo
#define unzGoToNextFileInfo CountedNextInfo
#define unzOpen CountedOpen
#include "load.h"
#undef unzGetCurrentFileInfo
#undef unzGoToFirstFileInfo
#undef unzGoToNextFileInfo
#undef unzOpen
static void FS_Path_f() {}
static void FS_Dir_f() {}
static void FS_NewDir_f() {}
static void FS_TouchFile_f() {}
static void FS_Which_f() {}
static void Cmd_AddCommand(const char *,void (*)(),const char *) {}
static void Cmd_RemoveCommand(const char *) {}
#include "mount.h"
struct fsReadFileIdentity_t {uint64_t generation,container,position,size;uint32_t provider,reserved;};
static struct Handle {struct {union {FILE *o;unzFile z;} file;int unique;} handleFiles;char name[MAX_QPATH];int zipFile,zipFilePos,zipFileLen,fileSize;} fsh[MAX_FILE_HANDLES];
static void FS_AssertInitialised() {CHECK(fs_searchpaths);}
static int FS_HandleForFile() {for(int i=1;i<MAX_FILE_HANDLES;++i)if(!fsh[i].handleFiles.file.o)return i;CHECK(false);return 0;}
static bool FS_PakIsPure(pack_t *pack) {if(!fs_numServerPaks)return true;for(int i=0;i<fs_numServerPaks;++i)if(fs_serverPaks[i]==pack->checksum)return true;return false;}
static bool FS_IsExt(const char *name,const char *extension,int length) {int n=int(std::strlen(extension));return length>=n&&!Q_stricmp(name+length-n,extension);}
static bool FS_IsDemoExt(const char *name,int length) {return FS_IsExt(name,".dm_26",length);}
static long FS_fplength(FILE *file) {long p=std::ftell(file);std::fseek(file,0,SEEK_END);long n=std::ftell(file);std::fseek(file,p,SEEK_SET);return n;}
static float Q_flrand(float,float) {return 0;}
// Network-drive copying is separate from mount/restart semantics. Keep the
// production archive/pure/identity/reference path and native file stamps.
#ifdef _WIN32
#define FIXTURE_WINDOWS
#undef _WIN32
#endif
#include "read.h"
#ifdef FIXTURE_WINDOWS
#define _WIN32
#undef FIXTURE_WINDOWS
#endif
static void FS_FCloseFile(int handle) {
    Handle &f=fsh[handle];++closedFiles;
    if(f.zipFile){unzCloseCurrentFile(f.handleFiles.file.z);if(f.handleFiles.unique)unzClose(f.handleFiles.file.z);}
    else if(f.handleFiles.file.o)std::fclose(f.handleFiles.file.o);
    f={};
}
static long FS_ReadFile(const char *name,void *) {if(missingCfgOnce){missingCfgOnce=false;return -1;}return FS_FOpenFileRead(name,nullptr,qfalse);}
static void FS_PureServerSetLoadedPaks(const char *,const char *) {fs_numServerPaks=0;fs_reordered=false;}
static qboolean Com_SafeMode() {return qfalse;}
static void Cbuf_AddText(const char *) {}
void FS_Restart(int);
#include "restart.h"

static pack_t *pack(const char *basename) {for(searchpath_t *p=fs_searchpaths;p;p=p->next)if(p->pack&&!std::strcmp(p->pack->pakBasename,basename))return p->pack;return nullptr;}
static std::vector<std::string> order() {std::vector<std::string> result;for(searchpath_t *p=fs_searchpaths;p;p=p->next)result.push_back(p->pack?p->pack->pakFilename:p->dir->fullpath);return result;}
static std::string read(const char *name,qboolean unique=qtrue) {
    int handle=0;long length=FS_FOpenFileRead(name,&handle,unique);CHECK(length>=0&&handle);
    std::string text(size_t(length),'\0');if(length)CHECK(unzReadCurrentFile(fsh[handle].handleFiles.file.z,&text[0],unsigned(length))==length);
    FS_FCloseFile(handle);return text;
}
static void checksum(pack_t *value) {
    CHECK(value);unzFile file=unzOpen(value->pakFilename);CHECK(file);
    unz_global_info info;CHECK(unzGetGlobalInfo(file,&info)==UNZ_OK);std::vector<int> words(1,LittleLong(fs_checksumFeed));
    int status=unzGoToFirstFile(file);for(unsigned long i=0;i<info.number_entry;++i){CHECK(status==UNZ_OK);unz_file_info entry;
        CHECK(unzGetCurrentFileInfo(file,&entry,nullptr,0,nullptr,0,nullptr,0)==UNZ_OK);if(entry.uncompressed_size)words.push_back(LittleLong(entry.crc));status=unzGoToNextFile(file);}
    CHECK(value->checksum==LittleLong(Com_BlockChecksum(words.data()+1,(words.size()-1)*sizeof(int))));
    CHECK(value->pure_checksum==LittleLong(Com_BlockChecksum(words.data(),words.size()*sizeof(int))));unzClose(file);
}
static void start(int setting=1) {
    if(fs_searchpaths)FS_Shutdown(qtrue);CHECK(liveAllocations==0);cvars.clear();fs_numServerPaks=0;fs_reordered=false;
    fs_cmdlineGame[0]=lastValidBase[0]=lastValidGame[0]='\0';beforeEnumeration={};afterOpen={};throwEnumeration=missingCfgOnce=false;
    const auto root=std::filesystem::canonical(fixtureRoot);CHECK(std::filesystem::exists(root/"seed/base/pak0.pk3"));
    for(const auto &directory:{baseDirectory,homeDirectory}){
        const auto checked=std::filesystem::weakly_canonical(directory);
        CHECK(checked.parent_path()==root&&(checked.filename()=="live"||checked.filename()=="home"));
        std::filesystem::remove_all(checked);
    }
    std::filesystem::copy(std::filesystem::path(fixtureRoot)/"seed",baseDirectory,std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(homeDirectory);
    Cvar_Set("fs_basepath",baseDirectory.c_str());Cvar_Set("fs_homepath",homeDirectory.c_str());
    if(setting>=0)Cvar_Set("fs_restartCache",setting?"1":"0");
    Cvar_Set("fs_zipCatalogFast",catalogMode?"1":"0");
    FS_Startup(BASEGAME);Q_strncpyz(lastValidBase,baseDirectory.c_str(),sizeof(lastValidBase));Q_strncpyz(lastValidGame,fs_gamedirvar->string,sizeof(lastValidGame));
}
static int run(int argc,char **argv) {
    CHECK(argc==2);fixtureRoot=argv[1];baseDirectory=fixtureRoot+"/live";homeDirectory=fixtureRoot+"/home";
    start();CHECK(pack("pak0")&&pack("pak9")&&pack("client"));
    pack_t *old=pack("pak9");void *metadata=old->buildBuffer;int originalChecksum=old->checksum;
    const int members=fs_packFiles;auto mounted=order();checksum(old);CHECK(read("dup.bin")=="last duplicate");CHECK(read("same.bin")=="override");
    fsReadFileIdentity_t identity={};CHECK(FS_GetReadFileIdentity("same.bin",&identity,qtrue));CHECK(old->referenced&FS_GENERAL_REF);
    const auto originalIdentity=identity;
    auto builds=fs_pakLookupIndex.buildCount();int handle=0;long length=FS_FOpenFileRead("same.bin",&handle,qtrue);CHECK(length>0);fsh[handle].fileSize=int(length);
    int beforeClosed=closedFiles,beforeScans=directoryScans;centralReads=0;
    CHECK(FS_ConditionalRestart(fs_checksumFeed+77));
    CHECK(centralReads==0&&directoryScans>beforeScans&&closedFiles>beforeClosed&&!fsh[handle].handleFiles.file.o);
    CHECK(pack("pak9")==old&&old->buildBuffer==metadata&&old->referenced==0&&old->checksum==originalChecksum);
    CHECK(fs_packFiles==members&&order()==mounted);checksum(old);CHECK(read("dup.bin")=="last duplicate");
    CHECK(FS_GetReadFileIdentity("same.bin",&identity,qtrue)&&fs_pakLookupIndex.buildCount()>builds);
    CHECK(identity.generation==originalIdentity.generation&&identity.position==originalIdentity.position);
    CHECK(old->referenced==FS_GENERAL_REF);CHECK(read("cgamex86.dll")=="game"&&(old->referenced&FS_CGAME_REF));
    CHECK(read("uix86.dll")=="ui"&&(old->referenced&FS_UI_REF));
    CHECK(!FS_ConditionalRestart(fs_checksumFeed));
    centralReads=0;FS_Restart(fs_checksumFeed);CHECK(centralReads>1200); // Explicit restarts remain full scans.
    Cvar_Set("fs_restartCache","0");centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>1200&&!pack("pak9")->checksumHeaders);
    start(-1);CHECK(fs_restartCache->integer==1&&pack("pak9")->checksumHeaders);
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==0);
    start(0);CHECK(!fs_restartCache->integer&&!pack("pak9")->checksumHeaders);
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>1200);

    start();old=pack("pak9");Cvar_Set("fs_game","ignored");centralReads=0;
    CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==0&&pack("pak9")==old&&!pack("mod"));
    Cvar_Set("fs_globalcfg","0");Cvar_Set("fs_game","modA");centralReads=0;
    CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>1200&&read("mod.bin")=="alpha");
    Cvar_Set("fs_game","modB");centralReads=0;CHECK(fs_gamedirvar->modified&&FS_ConditionalRestart(fs_checksumFeed+1));
    CHECK(centralReads>1200&&read("mod.bin")=="bravo");
    start();Q_strncpyz(fs_cmdlineGame,"cmdmod",sizeof(fs_cmdlineGame));centralReads=0;
    CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>1200&&pack("command"));
    start();Cvar_Set("fs_homepath",(fixtureRoot+"/other-home").c_str());centralReads=0;
    CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>1200);

    start();const auto replacement=std::filesystem::path(fixtureRoot)/"replacement.pk3";
    beforeEnumeration=[&](){std::filesystem::copy_file(replacement,std::filesystem::path(baseDirectory)/"base/pak9.pk3",std::filesystem::copy_options::overwrite_existing);};
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==2&&read("same.bin")=="replaced");checksum(pack("pak9"));
    start();const auto originalPath=std::filesystem::path(baseDirectory)/"base/pak9.pk3";
    const auto originalTime=std::filesystem::last_write_time(originalPath);
    const auto originalSize=std::filesystem::file_size(originalPath);
    beforeEnumeration=[&](){
        const auto staged=std::filesystem::path(baseDirectory)/"base/staged.tmp";
        std::filesystem::copy_file(std::filesystem::path(fixtureRoot)/"same-size.pk3",staged);
        std::filesystem::last_write_time(staged,originalTime);CHECK(std::filesystem::file_size(staged)==originalSize);
        std::filesystem::remove(originalPath);std::filesystem::rename(staged,originalPath);
    };
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==1207&&read("same.bin")=="replace!");checksum(pack("pak9"));
    start();beforeEnumeration=[&](){std::filesystem::remove(std::filesystem::path(baseDirectory)/"base/pak9.pk3");std::filesystem::copy_file(std::filesystem::path(fixtureRoot)/"added.pk3",std::filesystem::path(baseDirectory)/"base/new.pk3");};
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==1&&!pack("pak9")&&pack("new")&&read("new.bin")=="added");
    start();beforeEnumeration=[&](){std::filesystem::copy_file(std::filesystem::path(fixtureRoot)/"invalid.pk3",std::filesystem::path(baseDirectory)/"base/pak9.pk3",std::filesystem::copy_options::overwrite_existing);};
    CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(!pack("pak9")&&read("same.bin")=="base");

    start();afterOpen=[&](const char *name){auto path=std::filesystem::path(name);std::filesystem::last_write_time(path,std::filesystem::last_write_time(path)+std::chrono::seconds(2));};
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads>0); // Changed between pre/post stamps.
    start();fs_serverPaks[0]=pack("pak0")->checksum;fs_serverPaks[1]=pack("pak9")->checksum;fs_numServerPaks=2;
    centralReads=0;CHECK(FS_ConditionalRestart(fs_checksumFeed+1));CHECK(centralReads==0&&fs_searchpaths->pack==pack("pak0"));
    CHECK(read("same.bin")=="base");checksum(pack("pak0"));checksum(pack("pak9"));
    start();throwEnumeration=true;try{FS_ConditionalRestart(fs_checksumFeed+1);CHECK(false);}catch(int code){CHECK(code==ERR_DROP);}
    CHECK(!FSRestartMetadataCache::active);FS_Shutdown(qtrue);CHECK(liveAllocations==0);
    start();missingCfgOnce=true;centralReads=0;try{FS_ConditionalRestart(fs_checksumFeed+1);CHECK(false);}catch(int code){CHECK(code==ERR_DROP);}
    CHECK(!FSRestartMetadataCache::active&&centralReads>1200); // Recursive cfg fallback uses the full public restart.
    FS_Shutdown(qtrue);CHECK(liveAllocations==0);
    std::puts("Actual restart/mount/lookup checks passed: checksum bytes, zero repeated catalogue reads, duplicate winners, references, strong-stamp fallback, pure order and ownership.");
    return 0;
}
int main(int argc,char **argv) {
    try {for(catalogMode=0;catalogMode<2;++catalogMode){const int result=run(argc,argv);if(result)return result;}return 0;}
    catch(const std::exception &error){std::fprintf(stderr,"Unexpected fixture exception: %s\n",error.what());return 1;}
    catch(int code){std::fprintf(stderr,"Unexpected engine error: %d\n",code);return 1;}
}
