#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
enum {qfalse=0};
struct shaderStage_t {void *ss;};
static struct {struct {int num_stages;} surface_sprites;} shader;
static std::vector<std::string> input;
static size_t cursor;
static std::string observedParameter,observedArguments;
static bool parseResult,createsStage;
static char *COM_ParseExt(const char**,int){static char token[1024];if(cursor==input.size()){token[0]=0;return token;}CHECK(input[cursor].size()<sizeof(token));std::strcpy(token,input[cursor++].c_str());return token;}
static void Q_strcat(char *dest,size_t capacity,const char *source){CHECK(std::strlen(dest)+std::strlen(source)<capacity);std::strcat(dest,source);}
static void Q_strncpyz(char *dest,const char *source,size_t capacity){CHECK(capacity>0);std::strncpy(dest,source,capacity-1);dest[capacity-1]=0;}
static bool ParseSurfaceSpritesOptional(const char *parameter,const char *arguments,shaderStage_t *stage){observedParameter=parameter;observedArguments=arguments;if(createsStage)stage->ss=&shader;return parseResult;}
#include "shader-token.h"
static void check(const std::string &parameter,bool existing,bool created,bool accepted){
  char token[1024];CHECK(parameter.size()<sizeof(token));std::strcpy(token,parameter.c_str());
  shader.surface_sprites.num_stages=7;shaderStage_t stage={existing?&shader:nullptr};input={"12","0.5"};cursor=0;parseResult=accepted;createsStage=created;const char *text="";
  parseOptional(&stage,&text,token);CHECK(observedParameter==parameter.substr(0,127));CHECK(observedArguments=="12 0.5 ");CHECK(cursor==2);CHECK(shader.surface_sprites.num_stages==7+int(accepted&&!existing));CHECK(bool(stage.ss)==(existing||created));
}
int main(){
  for(size_t length: {size_t(2),size_t(126),size_t(127),size_t(128),size_t(255),size_t(1023)}){
    std::string parameter="ss"+std::string(length-2,'x');check(parameter,false,false,false);
  }
  check("ssWind",false,true,true);check("ssWind",true,true,true);check("ssUnknown",false,false,false);
  std::puts("Actual ParseStage optional surface-sprite branch bounds long tokens and preserves valid argument/stage behavior.");
}
