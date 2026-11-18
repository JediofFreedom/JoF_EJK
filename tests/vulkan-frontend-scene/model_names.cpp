#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
typedef int qhandle_t;
static const int MAX_QPATH=64;
struct animation_t {int numBones;};
static animation_t animation={3};
struct CGhoul2Info {
  char mFileName[MAX_QPATH]={};int mModelindex=-1;animation_t *aHeader=nullptr;
  std::vector<int> mBlist,mBltlist;int mCustomShader=0,mCustomSkin=0,mLodBias=0,mAnimFrameDefault=0,mFlags=0,mModelBoltLink=0;
};
typedef std::vector<CGhoul2Info> CGhoul2Info_v;
static bool G2_TestModelPointers(CGhoul2Info *info){info->aHeader=&animation;return true;}
static void G2_Init_Bone_List(std::vector<int>&bones,int count){bones.resize(count);}
static void G2_Init_Bolt_List(std::vector<int>&bolts){bolts.clear();}
#include "model-functions.h"
int main(){
  for(size_t size: {size_t(MAX_QPATH),size_t(MAX_QPATH+1),size_t(1024)}){
    CGhoul2Info_v *info=nullptr;std::string name(size,'x');CHECK(G2API_InitGhoul2Model(&info,name.c_str(),0,1,2,0,3)==-1);CHECK(info==nullptr);
  }
  CGhoul2Info_v *info=nullptr;std::string valid(MAX_QPATH-1,'x');CHECK(G2API_InitGhoul2Model(&info,valid.c_str(),0,1,2,0,3)==0);CHECK(info&&info->size()==1);CHECK(!std::strcmp((*info)[0].mFileName,valid.c_str()));CHECK((*info)[0].mCustomSkin==1&&(*info)[0].mCustomShader==2&&(*info)[0].mLodBias==3&&(*info)[0].mBlist.size()==3&&(*info)[0].mModelBoltLink==-1);
  std::string invalid(64,'z');CHECK(G2API_InitGhoul2Model(&info,invalid.c_str(),0,4,5,0,6)==-1);CHECK(info->size()==1&&!std::strcmp((*info)[0].mFileName,valid.c_str()));
  (*info)[0].mModelindex=-1;CHECK(G2API_InitGhoul2Model(&info,"models/players/kyle/model.glm",0,4,5,0,6)==0);CHECK(info->size()==1&&(*info)[0].mCustomSkin==4);delete info;
  std::puts("Production Ghoul2 initialization rejects overlong paths before mutation and preserves valid model initialization.");
}
