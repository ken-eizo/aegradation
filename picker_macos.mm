// Native AppKit color editor; CPU rendering, no Vulkan or global color panel.
#import <AppKit/AppKit.h>
#include "stop_rgba_dialog_embedded.hpp"
#include "picker_slugia/picker_axes.hpp"
#include <cstring>
using namespace gradation::picker;
using gradation::stopui::Depth;
@interface GradationMacColorView : NSView
@property(nonatomic) RGBA value;
@property(nonatomic) HSV hsv;
@property(nonatomic) RGBA original;
@property(nonatomic) Depth depth;
@property(nonatomic,strong) NSArray<NSTextField*>* fields;
@property(nonatomic,strong) NSTextField* hex;
@property(nonatomic,strong) NSPopUpButton* axis;
- (void)syncFields;
- (BOOL)readFields;
@end
@implementation GradationMacColorView
- (BOOL)isFlipped{return YES;}
- (AxisMode)mode{return (AxisMode)self.axis.indexOfSelectedItem;}
- (void)syncFields{
 float c[]={self.value.r,self.value.g,self.value.b,self.value.a};
 for(int i=0;i<4;i++){
  auto s=gradation::stopui::formatChannel(c[i],self.depth);
  self.fields[i].stringValue=[[NSString alloc] initWithBytes:s.data() length:s.size()*sizeof(wchar_t) encoding:NSUTF32LittleEndianStringEncoding];
 }
 auto s=gradation::stopui::hexRGB(self.value);
 self.hex.stringValue=[[NSString alloc] initWithBytes:s.data() length:s.size()*sizeof(wchar_t) encoding:NSUTF32LittleEndianStringEncoding];
 [self setNeedsDisplay:YES];
}
- (BOOL)readFields{
 RGBA c=self.value;
 auto wide=[](NSString* str){
  const char* u=str.UTF8String;
  std::wstring w;
  if(u)while(*u)w.push_back(static_cast<unsigned char>(*u++));
  return w;
 };
 auto h=wide(self.hex.stringValue);
 if(h!=gradation::stopui::hexRGB(c)&&!gradation::stopui::parseHexRGB(h.c_str(),c))return NO;
 float* p[]={&c.r,&c.g,&c.b,&c.a};
 const float prev[]={self.value.r,self.value.g,self.value.b,self.value.a};
 for(int i=0;i<4;i++){
  auto t=wide(self.fields[i].stringValue);
  if(t!=gradation::stopui::formatChannel(prev[i],self.depth)&&!gradation::stopui::parseChannel(t.c_str(),self.depth,*p[i]))return NO;
 }
 const auto prior=self.hsv;self.value=c;self.hsv=gradation::stopui::toHSV(c);
 if(self.hsv.s==0||self.hsv.v==0){HSV h=self.hsv;h.h=prior.h;self.hsv=h;}
 return YES;
}
- (void)axisChanged:(id)sender{if(![self readFields]){NSBeep();return;}[self syncFields];}
- (void)drawRect:(NSRect)dirty{
 [super drawRect:dirty];
 const auto c=self.value;const auto hsv=self.hsv;const auto mode=[self mode];
 auto draw=[&](NSRect rect,int width,int kind){
  constexpr int h=256;
  NSBitmapImageRep* img=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:h
   bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES colorSpaceName:NSDeviceRGBColorSpace
   bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:width*4 bitsPerPixel:32];
  if(!img||!img.bitmapData)return;
  for(int y=0;y<h;y++)for(int x=0;x<width;x++){
   float u=float(x)/float(std::max(1,width-1)),v=float(y)/float(h-1);
   RGBA q=kind==0?samplePlane(mode,c,hsv,u,v):kind==1?sampleBar(mode,c,hsv,v):RGBA{c.r,c.g,c.b,1.f-v};
   float a=kind==2?q.a:1.f,checker=((x/8+y/8)&1)?.65f:1.f;
   auto* p=img.bitmapData+(y*width+x)*4;
   p[0]=gradation::stopui::rgb8(q.r*a+checker*(1-a));p[1]=gradation::stopui::rgb8(q.g*a+checker*(1-a));p[2]=gradation::stopui::rgb8(q.b*a+checker*(1-a));p[3]=255;
  }
  [img drawInRect:rect fromRect:NSMakeRect(0,0,width,h) operation:NSCompositingOperationCopy fraction:1 respectFlipped:YES hints:nil];
  [[NSColor colorWithWhite:.294 alpha:1] setStroke];NSFrameRectWithWidth(rect,1);
 };
 draw(NSMakeRect(0,0,256,256),256,0);draw(NSMakeRect(264,0,20,256),20,1);draw(NSMakeRect(292,0,20,256),20,2);
 const auto m=planePosition(mode,c,hsv);
 [[NSColor whiteColor] setStroke];NSBezierPath* marker=[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(m.x*255-4,m.y*255-4,8,8)];marker.lineWidth=2;[marker stroke];
 NSFrameRectWithWidth(NSMakeRect(264,barY(mode,c,hsv)*255-2,20,4),2);
 NSFrameRectWithWidth(NSMakeRect(292,(1-c.a)*255-2,20,4),2);
 auto swatch=[&](NSRect r,RGBA v){
  [[NSColor whiteColor] setFill];NSRectFill(r);
  [[NSColor colorWithSRGBRed:v.r green:v.g blue:v.b alpha:v.a] setFill];NSRectFillUsingOperation(r,NSCompositingOperationSourceOver);
 };
 swatch(NSMakeRect(326,0,110,25),c);swatch(NSMakeRect(326,25,110,25),self.original);
}
- (void)applyMouse:(NSEvent*)event{
 NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];
 if(p.y<0||p.y>256||p.x<0||p.x>312)return;
 RGBA c=self.value;HSV h=self.hsv;
 if(p.x<=256)c=samplePlane([self mode],c,h,p.x/255.f,p.y/255.f);
 else if(p.x>=264&&p.x<=284)c=sampleBar([self mode],c,h,p.y/255.f);
 else if(p.x>=292)c.a=unit(1.f-p.y/255.f);else return;
 self.value=c;self.hsv=gradation::stopui::toHSV(c);
 if(self.hsv.s==0||self.hsv.v==0){HSV next=self.hsv;next.h=([self mode]==AxisMode::Hue&&p.x>=264&&p.x<=284)?360.f*unit(1.f-p.y/255.f):h.h;self.hsv=next;}
 [self syncFields];
}
- (void)mouseDown:(NSEvent*)event{if(![self readFields]){NSBeep();return;}[self applyMouse:event];}
- (void)mouseDragged:(NSEvent*)event{[self applyMouse:event];}
@end
namespace gradation::stopui {
NativeOwner nativeOwner(const char** failure) noexcept{
 if(failure)*failure=nullptr;
 if(![NSThread isMainThread]||!NSApp||![NSApp keyWindow]){
  if(failure)*failure="Color Picker requires the AE main UI thread and active window";return nullptr;
 }
 return (__bridge void*)[NSApp keyWindow];
}
bool editRGBA(NativeOwner owner,RGBA& value,Depth depth,std::string* failure,ColorPreviewCallback,void*,int) noexcept{
 if(failure)failure->clear();
 if(!owner||![NSThread isMainThread]){if(failure)*failure="Invalid Mac modal owner";return false;}
 try{@autoreleasepool{@try{
  NSAlert* alert=[[NSAlert alloc] init];alert.messageText=@"Color";
  [alert addButtonWithTitle:@"OK"];[alert addButtonWithTitle:@"Cancel"];
  GradationMacColorView* view=[[GradationMacColorView alloc] initWithFrame:NSMakeRect(0,0,440,260)];
  view.value=value;view.original=value;view.hsv=toHSV(value);view.depth=depth;
  view.axis=[[NSPopUpButton alloc] initWithFrame:NSMakeRect(326,60,110,24) pullsDown:NO];
  [view.axis addItemsWithTitles:@[@"Hue",@"Saturation",@"Brightness",@"Red",@"Green",@"Blue",@"Alpha"]];
  view.axis.target=view;view.axis.action=@selector(axisChanged:);[view addSubview:view.axis];
  NSMutableArray* fields=[NSMutableArray arrayWithCapacity:4];NSArray* labels=@[@"R",@"G",@"B",@"A"];
  for(int i=0;i<4;i++){
   NSTextField* label=[NSTextField labelWithString:labels[i]];label.frame=NSMakeRect(326,96+i*29,18,24);[view addSubview:label];
   NSTextField* field=[[NSTextField alloc] initWithFrame:NSMakeRect(348,96+i*29,88,24)];[view addSubview:field];[fields addObject:field];
  }
  view.fields=fields;view.hex=[[NSTextField alloc] initWithFrame:NSMakeRect(348,212,88,24)];[view addSubview:view.hex];
  NSTextField* hash=[NSTextField labelWithString:@"#"];hash.frame=NSMakeRect(326,212,18,24);[view addSubview:hash];
  [view syncFields];alert.accessoryView=view;
  for(;;){
   if([alert runModal]!=NSAlertFirstButtonReturn)return false;
   if([view readFields]){value=view.value;return true;}
   NSAlert* invalid=[[NSAlert alloc] init];invalid.messageText=@"Invalid RGBA value";invalid.informativeText=@"Enter a valid channel value for the current project bit depth.";[invalid runModal];
  }
 }@catch(NSException* e){if(failure)*failure="AppKit color picker failed";return false;}
 }}catch(...){if(failure)*failure="Color picker allocation failed";return false;}
}
}
