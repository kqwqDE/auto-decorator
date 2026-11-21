#include "../core/Core.hpp"
#include <chrono>
#include <cstdio>
using namespace ad;
int main(){
    Rng r(5); int fails=0; size_t total=0;
    for(int style=0;style<int(Style::Count);style++) for(int seed=1;seed<=12;seed++){
        Grid g=Grid::make(0,0,70,30,3);
        for(int k=0;k<9;k++){ int x=r.i(0,60),y=r.i(0,22),w=r.i(1,14),h=r.i(1,9); for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)g.set(xx,yy);}
        Analysis a=analyse(g,1); Settings s; s.style=Style(style); s.seed=seed; Rng rr(seed); if(seed%3==0) s.randomise(rr); s.style=Style(style);
        auto v=generateFull(a,s,4,seed); if(v.size()!=4) fails++;
        for(auto&x:v){ total+=x.items.size(); for(auto&it:x.items) if(!(it.sx>0&&it.sy>0)||it.x!=it.x||it.y!=it.y) fails++; }
    }
    printf("styles x seeds done, items=%zu failures=%d\n",total,fails);
    // big level: 3000 x 60 cells, lots of platforms
    Grid g=Grid::make(0,0,3000,60,3); Rng q(9);
    for(int k=0;k<900;k++){int x=q.i(0,2980),y=q.i(0,50),w=q.i(2,20),h=q.i(1,8);for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)g.set(xx,yy);}
    auto t0=std::chrono::steady_clock::now(); Analysis a=analyse(g,4); auto t1=std::chrono::steady_clock::now();
    Settings s; auto v=generateFull(a,s,4,0); auto t2=std::chrono::steady_clock::now();
    auto ms=[](auto a,auto b){return (int)std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();};
    printf("big level: %zu structures, analyse %d ms, generate 4 variations %d ms, objects/var %d\n",a.structures.size(),ms(t0,t1),ms(t1,t2),v[0].objectCount());
}
