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
struct CardState { GFxMovieView* view=nullptr; float x=0,y=0,sx=100,sy=100,lastX=0,lastY=0,lastSx=100,lastSy=100; bool valid=false; };
struct ManagerState { void* manager=nullptr; float base[8]={},last[8]={}; bool valid=false; };
struct PreviewTransformLE { float x,depth,z,scale,xCopy,depthCopy,zCopy,scaleCopy; };
static_assert(sizeof(PreviewTransformLE)==0x20,"LE Inventory3DManager preview block size changed");
SettingState settings[12];
CardState card;
ManagerState managerState;

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
bool GetCard(GFxMovieView* view,GFxValue& clip){
 GFxValue itemCard;if(view->GetVariable(&itemCard,"_root.Menu_mc.itemCard")&&itemCard.IsDisplayObject()&&itemCard.GetMember("_parent",&clip)&&clip.IsDisplayObject())return true;
 for(const char* path:{"_root.Menu_mc.ItemCard_mc","_root.Menu.ItemInfoHolder"})if(view->GetVariable(&clip,path)&&clip.IsDisplayObject())return true;
 return false;
}
bool Cropped(GFxMovieView* view){GFxValue mode;return view->GetVariable(&mode,"Stage.scaleMode")&&mode.GetType()==GFxValue::kType_String&&mode.GetString()&&strcmp(mode.GetString(),"noBorder")==0;}
bool ExactFit(GFxMovieView* view){GFxValue mode;return view->GetVariable(&mode,"Stage.scaleMode")&&mode.GetType()==GFxValue::kType_String&&mode.GetString()&&strcmp(mode.GetString(),"exactFit")==0;}
void MoveMoreHUD(GFxMovieView* view,float dx,float dy,float ratio){
 GFxValue clip;if(!view->GetVariable(&clip,"_root.AHZItemCardContainer")||!clip.IsDisplayObject())return;
 float x=0,y=0,sx=0,sy=0;if(Number(clip,"_x",x)&&Number(clip,"_y",y)&&Number(clip,"_xscale",sx)&&Number(clip,"_yscale",sy)){SetNumber(clip,"_x",x+dx);SetNumber(clip,"_y",y+dy);SetNumber(clip,"_xscale",sx*ratio);SetNumber(clip,"_yscale",sy*ratio);}
}
void UpdateCard(GFxMovieView* view,const Rect& visible,float aspect,const Settings& cfg){
 GFxValue clip;if(!GetCard(view,clip))return;
 float x=0,y=0,sx=0,sy=0;if(!Number(clip,"_x",x)||!Number(clip,"_y",y)||!Number(clip,"_xscale",sx)||!Number(clip,"_yscale",sy)||sx==0||sy==0)return;
 if(!card.valid||card.view!=view){card={view,x,y,sx,sy,x,y,sx,sy,true};}
 else if(x!=card.lastX||y!=card.lastY||sx!=card.lastSx||sy!=card.lastSy){card.x+=x-card.lastX;card.y+=y-card.lastY;card.sx*=sx/card.lastSx;card.sy*=sy/card.lastSy;}
 const bool autoPlace=cfg.autoAdjustItemDisplay&&!Cropped(view);
 float targetX=card.x, targetY=card.y, targetSx=card.sx,targetSy=card.sy;
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
 for(unsigned i=0;i<n;++i){auto* node=*reinterpret_cast<NiAVObject**>(data+i*0x14+8);if(!Readable(node,sizeof(NiAVObject)))continue;
  bool duplicate=false;for(unsigned j=0;j<count;++j)duplicate|=out[j]==node;if(!duplicate)out[count++]=node;
 }
}
void TransformModels(void* manager,const float delta[4],bool move){
 NiAVObject* nodes[16]={};unsigned count=0;LoadedNodes(manager,nodes,count);if(!count)return;
 for(unsigned i=0;i<count;++i){auto* node=nodes[i];if(!node)continue;
  if(move){node->m_localTransform.pos.x+=delta[0];node->m_localTransform.pos.y+=delta[1];node->m_localTransform.pos.z+=delta[2];if(delta[3]>0)node->m_localTransform.scale*=delta[3];}
  alignas(16) NiAVObject::ControllerUpdateContext ctx{};using Fn=void(__thiscall*)(NiAVObject*,void*);reinterpret_cast<Fn>(0x00AAF320)(node,&ctx);
 }
}
void UpdatePreview(const Rect& visible,float aspect,const Settings& cfg){
 const Layout layout=AutoLayout(aspect);const float autoX=-layout.previewX/(previewShare*(visible.right-visible.left));
 for(unsigned i=0;i<12;++i){const bool x=i<4,z=i>=4&&i<8;const float forced=x?autoX-cfg.itemPreviewX/pixelsPerUnit:z?layout.previewZ+cfg.itemPreviewY/pixelsPerUnit:layout.previewScale*cfg.itemPreviewScale;ApplySetting(i,x?-cfg.itemPreviewX/pixelsPerUnit:z?cfg.itemPreviewY/pixelsPerUnit:0.0f,cfg.itemPreviewScale,cfg.autoAdjustItemDisplay,forced);}
 auto* m=Inventory3DManager::GetSingleton();if(!Readable(m,0xE4))return;auto* fields=reinterpret_cast<float*>(reinterpret_cast<char*>(m)+0x10);
 if(!managerState.valid||managerState.manager!=m){managerState.manager=m;for(unsigned i=0;i<8;++i)managerState.base[i]=managerState.last[i]=fields[i];managerState.valid=true;}
 else for(unsigned i=0;i<8;++i)if(fields[i]!=managerState.last[i])managerState.base[i]+=fields[i]-managerState.last[i];
 const Layout l=AutoLayout(aspect);float target[8];memcpy(target,managerState.base,sizeof(target));
 if(cfg.autoAdjustItemDisplay){target[0]=-l.previewX/(previewShare*(visible.right-visible.left))-cfg.itemPreviewX/pixelsPerUnit;target[2]=l.previewZ+cfg.itemPreviewY/pixelsPerUnit;target[3]=l.previewScale*cfg.itemPreviewScale;target[4]=target[0];target[6]=target[2];target[7]=target[3];}
 else{target[0]-=cfg.itemPreviewX/pixelsPerUnit;target[2]+=cfg.itemPreviewY/pixelsPerUnit;target[3]*=cfg.itemPreviewScale;target[4]-=cfg.itemPreviewX/pixelsPerUnit;target[6]+=cfg.itemPreviewY/pixelsPerUnit;target[7]*=cfg.itemPreviewScale;}
 target[1]=managerState.base[1];target[5]=managerState.base[5];
 float delta[4]={target[0]-fields[0],target[1]-fields[1],target[2]-fields[2],fields[3]>0?target[3]/fields[3]:1};
 if(std::fabs(delta[0])+std::fabs(delta[1])+std::fabs(delta[2])>0.001f||std::fabs(delta[3]-1)>0.001f)TransformModels(m,delta,true);
 fields[0]=target[0];fields[1]=target[1];fields[2]=target[2];fields[3]=target[3];fields[4]=target[4];fields[5]=target[5];fields[6]=target[6];fields[7]=target[7];
 memcpy(managerState.last,target,sizeof(target));
}
void UpdateRotationRect(GFxMovieView* view,const Rect& visible){
 GFxValue clip;if(!view->GetVariable(&clip,"_root.Menu_mc.mouseRotationRect")&&!view->GetVariable(&clip,"_root.Menu.MouseRotationRect"))return;if(!clip.IsDisplayObject())return;
 auto* m=Inventory3DManager::GetSingleton();if(!Readable(m,0xE4))return;auto* f=reinterpret_cast<float*>(reinterpret_cast<char*>(m)+0x10);float width=0,height=0,x=0,y=0;if(!Number(clip,"_width",width)||!Number(clip,"_height",height)||!Number(clip,"_x",x)||!Number(clip,"_y",y))return;
 const float unit=previewShare*(visible.right-visible.left),half=125.0f*(visible.bottom-visible.top)/720.0f*f[3]/1.5f;
 const float targetX=(visible.left+visible.right)*0.5f-f[0]*unit-half;
 const float targetY=visible.top+275.0f*(visible.bottom-visible.top)/720.0f-(f[2]-10.625f)*unit-half;
 if(std::fabs(x-targetX)>=0.5f)SetNumber(clip,"_x",targetX);if(std::fabs(y-targetY)>=0.5f)SetNumber(clip,"_y",targetY);
 if(std::fabs(width-half*2)>=0.5f)SetNumber(clip,"_width",half*2);if(std::fabs(height-half*2)>=0.5f)SetNumber(clip,"_height",half*2);
}
}
void Update(IMenu* menu,bool magicMenu,const Settings& cfg){
 if(!menu||!menu->view)return;Rect visible;if(!Visible(menu->view,visible))return;const float aspect=(visible.right-visible.left)/(visible.bottom-visible.top);Settings effective=cfg;
 if(effective.autoAdjustItemDisplay){GFxValue clip;float x=0,w=0;if(Cropped(menu->view)||ExactFit(menu->view)||!GetCard(menu->view,clip)||!Number(clip,"_x",x)||!Number(clip,"_width",w)||w<257.0f||x+w*0.5f<(visible.left+visible.right)*0.5f)effective.autoAdjustItemDisplay=false;}
 UpdateCard(menu->view,visible,aspect,effective);UpdatePreview(visible,aspect,effective);
 if(effective.autoAdjustItemDisplay||cfg.itemCardX!=0||cfg.itemCardY!=0||cfg.itemPreviewX!=0||cfg.itemPreviewY!=0||cfg.itemPreviewScale!=1.0f)UpdateRotationRect(menu->view,visible);
 (void)magicMenu;
}
void Reset(IMenu* closingMenu){
 for(auto& s:settings)if(s.valid&&Readable(s.ptr,sizeof(Setting))&&s.ptr->data.f32==s.last)s.ptr->data.f32=s.base;
 if(closingMenu&&closingMenu->view&&closingMenu->view==card.view){GFxValue clip;float x=0,y=0,sx=0,sy=0;if(GetCard(closingMenu->view,clip)&&Number(clip,"_x",x)&&Number(clip,"_y",y)&&Number(clip,"_xscale",sx)&&Number(clip,"_yscale",sy)&&x==card.lastX&&y==card.lastY&&sx==card.lastSx&&sy==card.lastSy){SetNumber(clip,"_x",card.x);SetNumber(clip,"_y",card.y);SetNumber(clip,"_xscale",card.sx);SetNumber(clip,"_yscale",card.sy);MoveMoreHUD(closingMenu->view,card.x-x,card.y-y,sy?card.sy/sy:1);}}
 if(managerState.valid&&managerState.manager&&managerState.manager==Inventory3DManager::GetSingleton()&&Readable(managerState.manager,0xE4)){auto* m=managerState.manager;auto* fields=reinterpret_cast<float*>(reinterpret_cast<char*>(m)+0x10);float delta[4]={managerState.base[0]-fields[0],managerState.base[1]-fields[1],managerState.base[2]-fields[2],fields[3]>0?managerState.base[3]/fields[3]:1};TransformModels(m,delta,true);for(unsigned i=0;i<8;++i)fields[i]=managerState.base[i];}
 card={};managerState={};for(auto& s:settings)s={};
}
}
