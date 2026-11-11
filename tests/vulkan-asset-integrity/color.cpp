#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); std::exit(1); } } while(0)
struct vec4 {float r,g,b,a;};
static vec4 operator+(vec4 a,vec4 b){return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a};}
static vec4 &operator+=(vec4 &a,vec4 b){a=a+b;return a;}
enum {CGEN_VERTEX=6};
static vec4 evaluate(float baseAlpha,float vertexAlpha){
    struct {struct {vec4 baseColor,vertColor;} bundle[1];} u_global={{{{0,0,0,baseAlpha},{0.5f,0.5f,0.5f,vertexAlpha}}}};
    const int index=0;vec4 color=u_global.bundle[0].baseColor;
    switch(CGEN_VERTEX){
#include "vertex_color.h"
    }
    return color;
}
int main(){
    // CPU preparation stores explicit alpha in baseColor, while the default
    // white model vertex alpha contributes through vertColor only when needed.
    for(float alpha : {0.0f,0.25f,0.5f,1.0f}){
        const vec4 color=evaluate(alpha,0);
        CHECK(std::fabs(color.a-alpha)<0.00001f);
        CHECK(color.r==0.5f && color.g==0.5f && color.b==0.5f);
    }
    CHECK(evaluate(0,1).a==1);CHECK(evaluate(1,-1).a==0);
    std::puts("Model vertex RGB preserves separately prepared alpha.");
}
