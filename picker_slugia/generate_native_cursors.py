from pathlib import Path
import math,re,struct
from PIL import Image,ImageDraw
root=Path(r'D:\Developer\Gradation\picker_slugia')
source=(root/'ColorPicker.slugui').read_text(encoding='utf-8')
# Flatten authored cubic curves only for rasterization. Use nonzero fill
# rules exactly, including overlapping stroke outlines, not polygon XOR.
def contours(d,ox=0,oy=0):
 ts=re.findall(r'[MLCZ]|[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?',d)
 i=0;paths=[];poly=[];current=(0.,0.)
 def pt():
  nonlocal i
  a,b=float(ts[i])+ox,float(ts[i+1])+oy;i+=2;return a,b
 while i<len(ts):
  c=ts[i];i+=1
  if c=='M':
   if poly:paths.append(poly)
   current=pt();poly=[current]
  elif c=='L':
   current=pt();poly.append(current)
  elif c=='C':
   a,b,e=pt(),pt(),pt();st=current
   for j in range(1,17):
    t=j/16;u=1-t
    poly.append(tuple(u**3*st[k]+3*u*u*t*a[k]+3*u*t*t*b[k]+t**3*e[k] for k in (0,1)))
   current=e
  elif c=='Z':
   if poly:paths.append(poly);poly=[]
  else:raise ValueError(c)
 if poly:paths.append(poly)
 return paths
paths=[]
for name,x,y in [('Vector_2_1000',0,7.162),('Vector_2_1001',8.736,0)]:
 block=source.split('Shape '+name+' {',1)[1].split('\n        }',1)[0]
 for match in re.finditer(r'(?m)^\s*(?:stroke-)?path-data: "([^"]+)";',block):
  paths.append(contours(match.group(1),x,y))
assert len(paths)==3
def render_svg(n):
 # Icon in SlugUI is 20x20. Cursor canvas is 32 units; keep identical
 # SVG geometry scale as the onscreen icon and preserve sampling tip.
 ss=8;sc=n/32*ss;pad=6
 im=Image.new('RGBA',(n*ss,n*ss))
 for ps in paths:
  mask=Image.new('L',im.size);draw=ImageDraw.Draw(mask)
  edges=[]
  for poly in ps:
   for a,b in zip(poly,poly[1:]+poly[:1]):
    x0,y0=(a[0]+pad)*sc,(a[1]+pad)*sc;x1,y1=(b[0]+pad)*sc,(b[1]+pad)*sc
    if y0!=y1:edges.append((x0,y0,x1,y1))
  for y in range(n*ss):
   yy=y+.5;hits=[]
   for x0,y0,x1,y1 in edges:
    if min(y0,y1)<=yy<max(y0,y1):
     hits.append((x0+(yy-y0)*(x1-x0)/(y1-y0),1 if y1>y0 else -1))
   hits.sort();w=0;left=None
   for x,d in hits:
    old=w;w+=d
    if old==0 and w!=0:left=x
    if old!=0 and w==0 and left is not None:
     lo=math.ceil(left-.5);hi=math.ceil(x-.5)-1
     if hi>=lo:draw.line((lo,y,hi,y),fill=255)
  layer=Image.new('RGBA',im.size,(176,176,176,255));layer.putalpha(mask)
  im=Image.alpha_composite(im,layer)
 return im.resize((n,n),Image.Resampling.LANCZOS),(round(6*n/32),round(26*n/32)-1)
def render_scrub(n):
 # symmetric horizontal scrub arrows, central vertical divider; clean
 # antialiased high-contrast black silhouette with subtle white keyline.
 ss=8;sc=n/32*ss
 mask=Image.new('L',(n*ss,n*ss));d=ImageDraw.Draw(mask)
 def poly(points):d.polygon([(round(x*sc),round(y*sc)) for x,y in points],fill=255)
 poly([(3,16),(10,9),(10,13),(13,13),(13,19),(10,19),(10,23)])
 poly([(29,16),(22,9),(22,13),(19,13),(19,19),(22,19),(22,23)])
 d.rounded_rectangle(tuple(round(x*sc) for x in (14.5,6,17.5,26)),radius=max(1,round(sc*.8)),fill=255)
 from PIL import ImageFilter
 edge=mask.filter(ImageFilter.MaxFilter(2*max(1,round(.7*sc))+1))
 im=Image.new('RGBA',mask.size,(255,255,255,255));im.putalpha(edge)
 inner=Image.new('RGBA',mask.size,(0,0,0,255));inner.putalpha(mask)
 im=Image.alpha_composite(im,inner)
 return im.resize((n,n),Image.Resampling.LANCZOS),(n//2,n//2)
def make_cur(path,fn):
 sizes=[16,20,24,28,30,32,40,48,64]
 header=struct.pack('<HHH',0,2,len(sizes));entries=[];blobs=[];offset=6+16*len(sizes)
 for n in sizes:
  im,(hx,hy)=fn(n)
  raw=im.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes('raw','BGRA')
  rowbytes=((n+31)//32)*4
  mask=bytearray(rowbytes*n)
  for y in range(n):
   for x in range(n):
    if im.getpixel((x,n-1-y))[3]==0:mask[y*rowbytes+x//8]|=0x80>>(x%8)
  blob=struct.pack('<IiiHHIIiiII',40,n,2*n,1,32,0,len(raw),0,0,0,0)+raw+mask
  entries.append(struct.pack('<BBBBHHII',n,n,0,0,hx,hy,len(blob),offset))
  blobs.append(blob);offset+=len(blob)
 path.write_bytes(header+b''.join(entries)+b''.join(blobs))
 print(path.name,'sizes',sizes,'bytes',offset)
make_cur(root/'eyedropper.cur',render_svg)
make_cur(root/'scrub.cur',render_scrub)
