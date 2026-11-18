#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
struct vec3 {float x,y,z;};
static vec3 operator-(vec3 a,vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
static float length(vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
static vec3 normalize(vec3 v){const float n=length(v);return n?vec3{v.x/n,v.y/n,v.z/n}:vec3{};}
static float clamp(float v,float lo,float hi){return std::min(hi,std::max(lo,v));}
enum {AGEN_PORTAL=8};
static float portal(vec3 position,vec3 eye,float range){
    struct {struct {vec3 xyz;} localViewOrigin;} u_entity={{eye}};
    struct {float portalRange;} u_global={range};
    struct {float a;} color={-1};
    // Evaluate the actual GLSL case body using equivalent scalar/vector types.
    switch(AGEN_PORTAL){
#include "portal.h"
    }
    return color.a;
}
int main(){
    for(float range : {64.0f,256.0f,1024.0f}) for(float factor : {0.0f,0.25f,0.5f,1.0f,2.0f}){
        const float distance=range*factor;
        CHECK(std::fabs(portal({0,0,0},{0.6f*distance,0.8f*distance,0},range)-std::min(1.0f,factor))<0.00001f);
        CHECK(std::fabs(portal({distance,0,0},{0,0,0},range)-std::min(1.0f,factor))<0.00001f);
    }
    std::puts("VBO portal alpha follows distance.");
}
