#include "../core/Core.hpp"
#include <cstdio>
#include <cmath>
#include <fstream>
using namespace ad;
static void rect(Grid& g,int x0,int y0,int x1,int y1){for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)g.set(x,y);}
int main(int argc,char**argv){
    Style st = argc>1 ? Style(atoi(argv[1])) : Style::Cyberpunk;
    Grid g = Grid::make(0,0,90,24,3);
    rect(g,2,2,22,9);                                   // rectangle platform
    for(int y=0;y<10;y++) for(int x=0;x<=y;x++) g.set(27+x,2+y);   // right triangle
    for(int y=0;y<13;y++) for(int x=0;x<13;x++){ float dx=x-6,dy=y-6; if(dx*dx+dy*dy<=6.6f*6.6f) g.set(44+x,2+y);} // circle
    rect(g,62,2,80,4); rect(g,62,5,64,14); rect(g,78,5,80,14); rect(g,62,15,80,17); // frame with hole
    rect(g,84,2,86,3); g.set(30,18); g.set(33,19);                                   // noise
    Analysis a = analyse(g,4);
    for(auto&s:a.structures) printf("structure %d: %zu cells, %dx%d shape=%s holes=%zu thin=%d runs=%zu convex=%zu score=%.1f\n",s.id,s.cells.size(),s.width(),s.height(),shapeName(s.shape),s.holes.size(),s.thin,s.runs.size(),s.convex.size(),s.score);
    Settings s; s.style=st; s.seed = argc>2?atoi(argv[2]):7;
    auto vars = generateFull(a,s,4,0);
    for(size_t i=0;i<vars.size();i++){
        auto&v=vars[i]; printf("variation %zu [%s]: %d objects\n",i+1,v.profile.label.c_str(),v.objectCount());
        std::ofstream f("/tmp/var"+std::to_string(i)+".txt");
        f<<g.ox<<" "<<g.oy<<" "<<g.w<<" "<<g.h<<"\n";
        for(int y=g.oy;y<g.oy+g.h;y++){for(int x=g.ox;x<g.ox+g.w;x++)f<<(g.at(x,y)?'#':'.');f<<"\n";}
        for(int k=0;k<5;k++)f<<int(v.palette.c[k].r)<<" "<<int(v.palette.c[k].g)<<" "<<int(v.palette.c[k].b)<<"\n";
        for(auto&it:v.items)f<<int(it.role)<<" "<<int(it.layer)<<" "<<it.x<<" "<<it.y<<" "<<it.rot<<" "<<it.sx<<" "<<it.sy<<" "<<int(it.slot)<<" "<<it.z<<"\n";
    }
}
