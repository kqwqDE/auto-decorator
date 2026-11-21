import sys, math
from PIL import Image, ImageDraw
S=14
def render(path,out):
    L=open(path).read().split("\n"); ox,oy,w,h=map(int,L[0].split())
    grid=L[1:1+h]; pal=[tuple(map(int,L[1+h+i].split())) for i in range(5)]
    items=[list(map(float,l.split())) for l in L[6+h:] if l.strip()]
    W,H=w*S,h*S
    img=Image.new("RGBA",(W,H),(8,9,20,255)); 
    def P(x,y): return ((x-ox)*S,(h-(y-oy))*S)
    def layer_draw(layer_id, solid_first=False):
        ov=Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(ov)
        for it in sorted([i for i in items if int(i[1])==layer_id], key=lambda i:i[8]):
            role,_,x,y,rot,sx,sy,slot,z=it; role=int(role); col=pal[int(slot)]
            a=90 if role==4 else 235
            if role==4: col=col; a=70
            ang=math.radians(-rot)  # GD clockwise -> screen
            hx,hy=sx*S/2,sy*S/2; cx,cy=P(x,y)
            def tr(px,py): # local->screen
                return (cx+px*math.cos(ang)+py*math.sin(ang), cy-(-px*math.sin(ang)*-1*-1) if False else cy+(px*math.sin(ang)*-1)*-1*-1+py*math.cos(ang)*-1*-1) if False else (cx+px*math.cos(-math.radians(rot)*-1)-py*math.sin(-math.radians(rot)*-1)*-1*-1, cy-(px*math.sin(math.radians(-rot))+py*math.cos(math.radians(-rot))))
            r=math.radians(rot)  # clockwise on screen with y down: x'=x cos r - y sin r ; y'= x sin r + y cos r (y down)
            def T(px,py): return (cx+px*math.cos(r)-py*math.sin(r), cy+px*math.sin(r)+py*math.cos(r))
            if role in (2,10):  # dot, spark
                d.ellipse([cx-hx,cy-hy,cx+hx,cy+hy],fill=col+(a,))
            elif role==3:       # triangle: right angle bottom-left at rot 0
                pts=[T(-hx,hy),T(hx,hy),T(-hx,-hy)]; d.polygon(pts,fill=col+(a,))
            elif role in (7,8,9): # crystal/plant/rock
                pts=[T(-hx,hy),T(hx,hy),T(0,-hy)]; d.polygon(pts,fill=col+(a,))
            else:
                pts=[T(-hx,-hy),T(hx,-hy),T(hx,hy),T(-hx,hy)]; d.polygon(pts,fill=col+(a if role!=5 else 150,))
        return ov
    img=Image.alpha_composite(img,layer_draw(0))
    d=ImageDraw.Draw(img)
    for y in range(h):
        for x in range(w):
            if grid[y][x]=='#':
                px,py=P(ox+x,oy+y); d.rectangle([px,py-S,px+S-1,py-1],fill=(38,40,58,255),outline=(55,58,80,255))
    img=Image.alpha_composite(img,layer_draw(1)); img=Image.alpha_composite(img,layer_draw(2))
    img.convert("RGB").save(out)
for i in range(4):
    try: render(f"/tmp/var{i}.txt",f"/tmp/var{i}.png")
    except FileNotFoundError: pass
