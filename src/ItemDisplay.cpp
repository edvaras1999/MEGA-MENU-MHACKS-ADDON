#include "ItemDisplay.h"
#include "Runtime.h"
#include "skse/GameSettings.h"
#include "skse/NiNodes.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace SPII::ItemDisplay {
namespace {
struct Rect { float left=0,top=0,right=0,bottom=0; };
struct Layout { float aspect,cardX,previewX,previewZ,previewScale; };
constexpr Layout layouts[]={{16.0f/9.0f,290,290,14,1.80f},{43.0f/18.0f,570,565,12,1.50f},{32.0f/9.0f,720,720,8,0.85f}};
constexpr float previewShare=0.00623f, pixelsPerUnit=8.0f;
struct SettingState { Setting* ptr=nullptr; float base=0,last=0; bool valid=false; };
struct CardState { GFxMovieView* view=nullptr; float x=0,y=0,sx=100,sy=100,lastX=0,lastY=0,lastSx=100,lastSy=100; bool valid=false,raised=false; };
struct PreviewNodeState { NiAVObject* node=nullptr; NiPoint3 base{}; float baseScale=1,lastScale=1; NiPoint3 last{}; };
SettingState settings[12];
CardState card;
PreviewNodeState previewNodes[16];unsigned previewNodeCount=0;
GFxMovieView* loggedView=nullptr;

float Lerp(float a,float b,float t){return a+(b-a)*t;}
Layout AutoLayout(float aspect){
 if(aspect<=layouts[0].aspect)return layouts[0];
 for(unsigned i=1;i<sizeof(layouts)/sizeof(layouts[0]);++i)if(aspect<=layouts[i].aspect){const auto& a=layouts[i-1];const auto& b=layouts[i];const float t=(aspect-a.aspect)/(b.aspect-a.aspect);return {aspect,Lerp(a.cardX,b.cardX,t),Lerp(a.previewX,b.previewX,t),Lerp(a.previewZ,b.previewZ,t),Lerp(a.previewScale,b.previewScale,t)};}
 return layouts[2];
}
bool Number(GFxValue& obj,const char* member,float& out){GFxValue v;if(!obj.GetMember(member,&v)||v.GetType()!=GFxValue::kType_Number)return false;out=static_cast<float>(v.GetNumber());return std::isfinite(out);}
bool SetNumber(GFxValue& obj,const char* member,float value){GFxValue v;v.SetNumber(value);return obj.SetMember(member,&v);}
bool BoundsIn(GFxValue& clip,GFxValue& space,Rect& out){
 GFxValue result;if(!clip.Invoke("getBounds",&result,&space,1)||!result.IsObject())return false;
 float w=0,h=0;if(!Number(result,"xMin",out.left)||!Number(result,"yMin",out.top)||!Number(result,"xMax",out.right)||!Number(result,"yMax",out.bottom))return false;
 w=out.right-out.left;h=out.bottom-out.top;return w>0&&h>0;
}
bool Bounds(GFxMovieView* view,GFxValue& clip,Rect& out){GFxValue root;return view&&view->GetVariable(&root,"_root")&&BoundsIn(clip,root,out);}
bool Visible(GFxMovieView* view,Rect& out){
 GFxValue rect;float w=0,h=0;if(!view||!view->GetVariable(&rect,"Stage.visibleRect")||!rect.IsObject()||!Number(rect,"x",out.left)||!Number(rect,"y",out.top)||!Number(rect,"width",w)||!Number(rect,"height",h)||w<=0||h<=0)return false;
 out.right=out.left+w;out.bottom=out.top+h;return true;
}
bool GetCard(GFxMovieView* view,GFxValue& clip,const char** found=nullptr){
 GFxValue itemCard;if(view->GetVariable(&itemCard,"_root.Menu_mc.itemCard")&&itemCard.IsDisplayObject()&&itemCard.GetMember("_parent",&clip)&&clip.IsDisplayObject()){if(found)*found="_root.Menu_mc.itemCard._parent";return true;}
 for(const char* path:{"_root.Menu_mc.ItemCard_mc","_root.Menu.ItemInfoHolder"})if(view->GetVariable(&clip,path)&&clip.IsDisplayObject()){if(found)*found=path;return true;}
 return false;
}
bool Cropped(GFxMovieView* view){GFxValue mode;return view->GetVariable(&mode,"Stage.scaleMode")&&mode.GetType()==GFxValue::kType_String&&mode.GetString()&&strcmp(mode.GetString(),"noBorder")==0;}
bool ExactFit(GFxMovieView* view){GFxValue mode;return view->GetVariable(&mode,"Stage.scaleMode")&&mode.GetType()==GFxValue::kType_String&&mode.GetString()&&strcmp(mode.GetString(),"exactFit")==0;}
void MoveMoreHUD(GFxMovieView* view,float dx,float dy,float ratio){
 GFxValue clip;if(!view->GetVariable(&clip,"_root.AHZItemCardContainer")||!clip.IsDisplayObject())return;
 float x=0,y=0,sx=0,sy=0;if(Number(clip,"_x",x)&&Number(clip,"_y",y)&&Number(clip,"_xscale",sx)&&Number(clip,"_yscale",sy)){SetNumber(clip,"_x",x+dx);SetNumber(clip,"_y",y+dy);SetNumber(clip,"_xscale",sx*ratio);SetNumber(clip,"_yscale",sy*ratio);}
}
bool RaiseCard(GFxValue& clip){
 GFxValue parent,depth;if(!clip.GetMember("_parent",&parent)||!parent.IsDisplayObject())return false;
 if(!parent.Invoke("getNextHighestDepth",&depth,nullptr,0)||depth.GetType()!=GFxValue::kType_Number)return false;
 return clip.Invoke("swapDepths",nullptr,&depth,1);
}
void UpdateCard(GFxMovieView* view,const Rect& visible,float aspect,const Settings& cfg){
 GFxValue clip;if(!GetCard(view,clip))return;
 float x=0,y=0,sx=0,sy=0;if(!Number(clip,"_x",x)||!Number(clip,"_y",y)||!Number(clip,"_xscale",sx)||!Number(clip,"_yscale",sy)||sx==0||sy==0)return;
 if(!card.valid||card.view!=view){card={view,x,y,sx,sy,x,y,sx,sy,true};}
 else if(x!=card.lastX||y!=card.lastY||sx!=card.lastSx||sy!=card.lastSy){card.x+=x-card.lastX;card.y+=y-card.lastY;card.sx*=sx/card.lastSx;card.sy*=sy/card.lastSy;}
 const bool autoPlace=cfg.autoAdjustItemDisplay&&!Cropped(view);
 float targetX=card.x, targetY=card.y, targetSx=card.sx*cfg.itemCardScale,targetSy=card.sy*cfg.itemCardScale;
 if(autoPlace){
  Rect screen,local;GFxValue parent;float currentWidth=0;
  const bool stock=Number(clip,"_width",currentWidth)&&currentWidth>=257.0f&&x+currentWidth*0.5f>=(visible.left+visible.right)*0.5f;
  if(stock&&clip.GetMember("_parent",&parent)&&parent.IsDisplayObject()&&BoundsIn(clip,parent,local)&&Bounds(view,clip,screen)){
   Rect parentRect;if(Bounds(view,parent,parentRect)){
    const float parentShift=screen.left-parentRect.left;
    const float centreOffset=((local.left+local.right)*0.5f-x)/sy;
    const float desired=(visible.left+visible.right)*0.5f+AutoLayout(aspect).cardX;
    targetX=desired-parentShift-centreOffset*targetSy;
   }
  }
 }
 targetX+=cfg.itemCardX;
 targetY-=cfg.itemCardY;
 if(!card.raised)card.raised=RaiseCard(clip);
 if(std::fabs(targetX-x)>0.05f||std::fabs(targetY-y)>0.05f||std::fabs(targetSx-sx)>0.05f||std::fabs(targetSy-sy)>0.05f){
  SetNumber(clip,"_x",targetX);SetNumber(clip,"_y",targetY);SetNumber(clip,"_xscale",targetSx);SetNumber(clip,"_yscale",targetSy);
  float actualX=targetX,actualY=targetY,actualS=targetSy;Number(clip,"_x",actualX);Number(clip,"_y",actualY);Number(clip,"_yscale",actualS);
  MoveMoreHUD(view,actualX-x,actualY-y,actualS/sy);card.lastX=actualX;card.lastY=actualY;card.lastSx=targetSx;card.lastSy=actualS;
 }else{card.lastX=x;card.lastY=y;card.lastSx=sx;card.lastSy=sy;}
}
const char* settingNames[12]={"fInventory3DItemPosX:Interface","fInventory3DItemPosXWide:Interface","fMagic3DItemPosX:Interface","fMagic3DItemPosXWide:Interface","fInventory3DItemPosZ:Interface","fInventory3DItemPosZWide:Interface","fMagic3DItemPosZ:Interface","fMagic3DItemPosZWide:Interface","fInventory3DItemPosScale:Interface","fInventory3DItemPosScaleWide:Interface","fMagic3DItemPosScale:Interface","fMagic3DItemPosScaleWide:Interface"};
void ApplySetting(unsigned i,float offset,float multiplier,bool force,float forced){
 Setting* s=GetINISetting(settingNames[i]);if(!s||s->GetType()!=Setting::kType_Float)return;
 auto& st=settings[i];const float current=s->data.f32;
 if(st.ptr!=s||!st.valid){st={s,current,current,true};}
 else if(current!=st.last)st.base=current;
 const bool isX=(i<4),isZ=(i>=4&&i<8),isScale=i>=8;
 float target=st.base;
 if(force)target=forced;
 else if(isX)target=st.base+offset;
 else if(isZ)target=st.base+offset;
 else if(isScale)target=st.base*multiplier;
 s->data.f32=target;st.last=target;
}
void LoadedNodes(void* manager,NiAVObject** out,unsigned& count){
 count=0;if(!Readable(manager,0xE4))return;const unsigned n=*reinterpret_cast<unsigned*>(static_cast<char*>(manager)+0xD0),flags=*reinterpret_cast<unsigned*>(static_cast<char*>(manager)+0x40);const bool local=(flags&0x80000000u)!=0;
 if(!n||n>16||(local&&n>7))return;auto* data=local?static_cast<char*>(manager)+0x44:*reinterpret_cast<char**>(static_cast<char*>(manager)+0x44);if(!Readable(data,n*0x14))return;
 for(unsigned i=0;i<n;++i){auto* node=*reinterpret_cast<NiAVObject**>(data+i*0x14+8);if(!Readable(node,sizeof(NiAVObject)))continue;bool duplicate=false;for(unsigned j=0;j<count;++j)duplicate|=out[j]==node;if(!duplicate)out[count++]=node;}
}
PreviewNodeState* StateFor(NiAVObject* node){for(unsigned i=0;i<previewNodeCount;++i)if(previewNodes[i].node==node)return &previewNodes[i];if(previewNodeCount>=16)return nullptr;auto& state=previewNodes[previewNodeCount++];state={};state.node=node;state.base=node->m_localTransform.pos;state.last=state.base;state.baseScale=state.lastScale=node->m_localTransform.scale;return &state;}
void ApplyModels(float dx,float dz,float scale){
 auto* manager=Inventory3DManager::GetSingleton();NiAVObject* nodes[16]={};unsigned count=0;LoadedNodes(manager,nodes,count);
 for(unsigned i=0;i<count;++i){auto* node=nodes[i];auto* state=StateFor(node);if(!state)continue;auto& transform=node->m_localTransform;
  if(transform.pos.x!=state->last.x||transform.pos.y!=state->last.y||transform.pos.z!=state->last.z){state->base.x+=transform.pos.x-state->last.x;state->base.y+=transform.pos.y-state->last.y;state->base.z+=transform.pos.z-state->last.z;}
  if(transform.scale!=state->lastScale&&state->lastScale!=0){state->baseScale*=transform.scale/state->lastScale;}
  const NiPoint3 target={state->base.x+dx,state->base.y,state->base.z+dz};const float targetScale=state->baseScale*scale;
  const float change=std::fabs(target.x-transform.pos.x)+std::fabs(target.y-transform.pos.y)+std::fabs(target.z-transform.pos.z)+std::fabs(targetScale-transform.scale);
  if(change>0.0001f){transform.pos=target;transform.scale=targetScale;alignas(16) NiAVObject::ControllerUpdateContext ctx{};using Fn=void(__thiscall*)(NiAVObject*,void*);reinterpret_cast<Fn>(0x00AAF320)(node,&ctx);}
  state->last=target;state->lastScale=targetScale;
 }
}
void UpdatePreview(const Rect& visible,float aspect,bool magicMenu,const Settings& cfg){
 const Layout layout=AutoLayout(aspect);const float autoX=-layout.previewX/(previewShare*(visible.right-visible.left));
 for(unsigned i=0;i<12;++i){const bool x=i<4,z=i>=4&&i<8;const float forced=x?autoX-cfg.itemPreviewX/pixelsPerUnit:z?layout.previewZ+cfg.itemPreviewY/pixelsPerUnit:layout.previewScale*cfg.itemPreviewScale;ApplySetting(i,x?-cfg.itemPreviewX/pixelsPerUnit:z?cfg.itemPreviewY/pixelsPerUnit:0.0f,cfg.itemPreviewScale,cfg.autoAdjustItemDisplay,forced);}
 const unsigned xi=magicMenu?2:0,zi=magicMenu?6:4,si=magicMenu?10:8;const unsigned wide=(aspect>16.0f/9.0f)?1:0;const unsigned xIndex=xi+wide,zIndex=zi+wide,sIndex=si+wide;
 const float baseX=settings[xIndex].valid?settings[xIndex].base:0,baseZ=settings[zIndex].valid?settings[zIndex].base:0,baseScale=settings[sIndex].valid?settings[sIndex].base:1;
 const float targetX=cfg.autoAdjustItemDisplay?autoX-cfg.itemPreviewX/pixelsPerUnit:baseX-cfg.itemPreviewX/pixelsPerUnit;
 const float targetZ=cfg.autoAdjustItemDisplay?layout.previewZ+cfg.itemPreviewY/pixelsPerUnit:baseZ+cfg.itemPreviewY/pixelsPerUnit;
 const float targetScale=(cfg.autoAdjustItemDisplay?layout.previewScale:baseScale)*cfg.itemPreviewScale;
 ApplyModels(targetX-baseX,targetZ-baseZ,baseScale!=0?targetScale/baseScale:1.0f);
}
}
void Update(IMenu* menu,bool magicMenu,const Settings& cfg){
 if(!menu||!menu->view)return;Rect visible;if(!Visible(menu->view,visible))return;const float aspect=(visible.right-visible.left)/(visible.bottom-visible.top);Settings effective=cfg;
 GFxValue detectedCard;const char* cardPath="none";float cardX=0,cardWidth=0;const bool hasCard=GetCard(menu->view,detectedCard,&cardPath);const bool hasGeometry=hasCard&&Number(detectedCard,"_x",cardX)&&Number(detectedCard,"_width",cardWidth);
 if(effective.autoAdjustItemDisplay&&(Cropped(menu->view)||ExactFit(menu->view)||!hasGeometry||cardWidth<257.0f))effective.autoAdjustItemDisplay=false;
 UpdateCard(menu->view,visible,aspect,effective);UpdatePreview(visible,aspect,magicMenu,effective);
 if(loggedView!=menu->view){loggedView=menu->view;const char* reason="active";if(!cfg.autoAdjustItemDisplay)reason="disabled-by-user";else if(Cropped(menu->view))reason="noBorder-scale-mode";else if(ExactFit(menu->view))reason="exactFit-scale-mode";else if(!hasGeometry)reason="card-geometry-unavailable";else if(cardWidth<257.0f)reason="card-width-below-stock";Log("ItemDisplay menu=%s view=%p viewport=%.0fx%.0f card=%s found=%u x=%.1f width=%.1f auto-config=%u auto-active=%u auto-reason=%s card-offset=%.0f,%.0f card-scale=%.2f preview-offset=%.0f,%.0f preview-scale=%.2f nodes=%u",magicMenu?"MagicMenu":"item menu",static_cast<void*>(menu->view),visible.right-visible.left,visible.bottom-visible.top,cardPath,hasGeometry?1u:0u,cardX,cardWidth,cfg.autoAdjustItemDisplay?1u:0u,effective.autoAdjustItemDisplay?1u:0u,reason,cfg.itemCardX,cfg.itemCardY,cfg.itemCardScale,cfg.itemPreviewX,cfg.itemPreviewY,cfg.itemPreviewScale,previewNodeCount);}
 (void)magicMenu;
}
void Reset(IMenu* closingMenu){
 for(auto& s:settings)if(s.valid&&Readable(s.ptr,sizeof(Setting))&&s.ptr->data.f32==s.last)s.ptr->data.f32=s.base;
 for(unsigned i=0;i<previewNodeCount;++i){auto& state=previewNodes[i];auto* node=state.node;if(!Readable(node,sizeof(NiAVObject)))continue;auto& transform=node->m_localTransform;if(transform.pos.x==state.last.x&&transform.pos.y==state.last.y&&transform.pos.z==state.last.z&&transform.scale==state.lastScale){transform.pos=state.base;transform.scale=state.baseScale;alignas(16) NiAVObject::ControllerUpdateContext ctx{};using Fn=void(__thiscall*)(NiAVObject*,void*);reinterpret_cast<Fn>(0x00AAF320)(node,&ctx);}}
 previewNodeCount=0;memset(previewNodes,0,sizeof(previewNodes));
 if(closingMenu&&closingMenu->view&&closingMenu->view==card.view){GFxValue clip;float x=0,y=0,sx=0,sy=0;if(GetCard(closingMenu->view,clip)&&Number(clip,"_x",x)&&Number(clip,"_y",y)&&Number(clip,"_xscale",sx)&&Number(clip,"_yscale",sy)&&x==card.lastX&&y==card.lastY&&sx==card.lastSx&&sy==card.lastSy){SetNumber(clip,"_x",card.x);SetNumber(clip,"_y",card.y);SetNumber(clip,"_xscale",card.sx);SetNumber(clip,"_yscale",card.sy);MoveMoreHUD(closingMenu->view,card.x-x,card.y-y,sy?card.sy/sy:1);}}
 card={};loggedView=nullptr;for(auto& s:settings)s={};
}
}
