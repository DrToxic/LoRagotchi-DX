#!/usr/bin/env python3
"""Exercise production UI handlers with mock peripherals: python3 test/ui/check_menu_navigation.py.

Requires Python 3 and g++. No PlatformIO or attached radio needed. Extracts the
actual function bodies, so regressions in handler dispatch are exercised, not a
parallel reimplementation of the navigation rules. Builds with and without sound.
"""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[2]; source=(root/'examples/companion_radio/ui-new/UITask.cpp').read_text()
def block(marker, text=source):
    start=text.index(marker);op=text.index('{',start);end=op+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def declaration(name):
    return re.search(r'^static [^\n]*\b'+name+r'\b.*?;',source,re.M|re.S).group(0)
pre=r'''
#include <cassert>
#include "examples/companion_radio/LoRaFishColour.h"
#include "examples/companion_radio/LoRaFishEgg.h"
static bool loRaFishAdoptionEligible=false,loRaFishEggApplying=false;
static bool loRaFishMonoDisplayNotice=false,loRaFishMonoDisplayColourKnown=false;
static uint8_t loRaFishMonoDisplayColour=0;
static constexpr uint8_t LORAFISH_MONO_DISPLAY_COLOUR_COUNT=3;
static const char* loRaFishMonoDisplayColourNames[]={"Blue OLED","White OLED","Yellow/Blue OLED"};
static LoRaFishEgg loRaFishEggSession;
static void closeLoRaFishAdoption(){loRaFishAdoptionEligible=false;loRaFishEggSession.close();}
static bool selectLoRaFishEggTank(bool){return false;}
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>
#include "examples/companion_radio/ui-new/LoRaFishMenu.h"
#include "examples/companion_radio/LoRaFishGuestReturn.h"
static LoRaFishGuestReturn loragotchiDXGuestReturn;
#include "examples/companion_radio/LoRaFishVisit.h"
static LoRaFishVisit loragotchiDXVisit;
#include "examples/companion_radio/ui-new/AdvertMenu.h"
#include "examples/companion_radio/ui-new/PetResetConfirmation.h"
#include "examples/companion_radio/ui-new/LoRaFishText.h"
#include "src/helpers/ui/DisplayDriver.h"
#include "examples/companion_radio/LoRagotchiChannelStatus.h"
LoRagotchiChannel::Result LoRagotchiChannel::startupResult = LoRagotchiChannel::Result::AlreadyJoined;
using std::min; using std::max;
#define LORAFISH_UI 1
#define MESH_DEBUG_PRINTLN(...) ((void)0)
static const char KEY_NEXT='n',KEY_PREV='p',KEY_LEFT='l',KEY_RIGHT='r',KEY_ENTER='e';
ColorVal UIColor::primary_txt=1, UIColor::secondary_txt=1, UIColor::window_bkg=0;
static uint32_t tick=100000;
unsigned long millis(){return tick;}
static int settingsWrites=0,petWrites=0,cleans=0,resets=0,guesses=0;
static bool canBreed=false;
static bool loRaFishCanBreed(){return canBreed;}
static void saveLoRaFishSettings(){++settingsWrites;}
static void saveLoRaFish(){++petWrites;}
static void tickLoRaFish(){}
static void loRaFishSyncDXAppearance(){}
static void applyLoRaFishTheme(){}
static void startLoRaFishCleaning(){++cleans;}
static void resetLoRaFishPet(){++resets;}
static void startLoRaFishGuessGame(){}
static void submitLoRaFishGuess(){++guesses;}
static bool breedLoRaFishWithGuest(){canBreed=false;return true;}
static int loRaFishPoopCount=1;
static bool loRaFishCleaning=false;
static bool loRaFishHatchMelodyPlayed=false;
static uint32_t loRaFishDXContactIndex=0;
struct {
  bool away=false,pending=false,committing=false;
  uint32_t destinationIndex=0,txid=1,fishid=2;
  uint8_t destinationKey[32]={};bool destinationKnown=false;char destinationName[32]="Peer0";
} loragotchiDXHome;
struct {bool present=false;} loragotchiDXGuest;
static int dxWrites=0;
static void loragotchiDXSave(){++dxWrites;}
static bool fishSleeping=false;
static bool loRaFishIsSleeping(){return fishSleeping;}
static const char* loRaFishMeshNodeName="Fish";
struct {uint32_t ageMinutes=60,rfEvents=1;uint8_t evolution=1;} fish;
struct {template<typename... Args> void printf(const char*,Args...){} void println(const char*){}} Serial;
static void loRaFishClock(char* time,size_t tsize,char* date,size_t dsize){
  snprintf(time,tsize,"12:00");snprintf(date,dsize,"Monday");
}
static constexpr uint32_t MAX_ANON_CONTACTS=2;
static constexpr size_t PUB_KEY_SIZE=32;
static constexpr uint8_t ADV_TYPE_CHAT=1;
struct ContactInfo {char name[32];uint32_t index;uint8_t type=ADV_TYPE_CHAT;
  struct {uint8_t pub_key[PUB_KEY_SIZE]={};} id;
};
struct {bool returnLoRaFishGuest(){++returnCalls;return true;}int returnCalls=0;int sends=0,rescues=0;bool fail=false,lastFlood=false;
  bool useCustom=false; std::vector<ContactInfo> custom;
  int contacts=3; int getNumContacts(){return useCustom ? (int)custom.size() : contacts;}
  bool advert(bool flood=false){if(fail)return false;++sends;lastFlood=flood;return true;}
  void enterCLIRescue(){++rescues;}
  int dxSends=0; uint32_t lastDestination=0;std::string lastMessage;
  bool getContactByIdx(uint32_t raw,ContactInfo& c){
    if(raw<MAX_ANON_CONTACTS || raw>=MAX_ANON_CONTACTS+getNumContacts())return false;
    if(useCustom){c=custom[raw-MAX_ANON_CONTACTS];return true;}
    c.index=raw-MAX_ANON_CONTACTS;snprintf(c.name,sizeof(c.name),"Peer%u 🐟",c.index);
    c.id.pub_key[0]=c.index+1;return true;
  }
  ContactInfo* lookupContactByPubKey(const uint8_t* key,int size){
    static ContactInfo c;
    for(int i=0;i<getNumContacts();++i)
      if(getContactByIdx(MAX_ANON_CONTACTS+i,c) && !memcmp(c.id.pub_key,key,size))return &c;
    return nullptr;
  }
  struct RTC {uint32_t getCurrentTime(){return 1;}} rtc;
  RTC* getRTCClock(){return &rtc;}
  int sendMessage(ContactInfo& c,uint32_t,int,const char* msg,uint32_t&,uint32_t&){
    ++dxSends;lastDestination=c.index;lastMessage=msg;return 0;
  }
  void trackLoRaFishRequest(const char*,uint32_t){}
  bool recallLoRaFish(){
    auto* c=lookupContactByPubKey(loragotchiDXHome.destinationKey,32);
    if(!c)return false;uint32_t ack=0,to=0;
    sendMessage(*c,1,0,"LFG1|RECALL|mock",ack,to);return true;
  }
  int sendLoRaFishCommit(ContactInfo& c){
    ++dxSends;lastDestination=c.index;lastMessage="LFG1|COMMIT|mock";
    return 0; // packet contents are covered by check_dx_colour_retries.py
  }
} the_mesh;
class BoundsDisplay : public DisplayDriver {
  int x=0,y=0,size=1;
  struct Box{int x,y,w,h;}; std::vector<Box> boxes;
public:
  bool on=true;
  std::vector<std::string> printed;
  BoundsDisplay():DisplayDriver(128,64){}
  void clearBounds(){boxes.clear();printed.clear();x=y=0;size=1;}
  bool isOn() override {return on;}
  void turnOn() override {on=true;} void turnOff() override {on=false;}
  void clear() override {} void startFrame(ColorVal) override {} void endFrame() override {}
  void setColor(ColorVal) override {} void setTextSize(int s) override {size=s;}
  void setCursor(int a,int b) override {x=a;y=b;}
  uint16_t getTextWidth(const char* t) override {return strlen(t)*6*size;}
  void print(const char* t) override {
    if(!*t)return;
    printed.emplace_back(t);
    for(const unsigned char* p=(const unsigned char*)t;*p;++p)assert(*p>=32 && *p<=126);
    int w=getTextWidth(t),h=8*size;
    if(!(x>=0 && x+w<=width() && y>=0 && y+h<=height())) {
      fprintf(stderr,"BOUNDS (%d,%d) %dx%d %s\n",x,y,w,h,t);abort();
    }
    for(auto b:boxes) if(x<b.x+b.w && x+w>b.x && y<b.y+b.h && y+h>b.y){
      fprintf(stderr,"OVERLAP (%d,%d) %s\n",x,y,t);abort();
    }
    boxes.push_back({x,y,w,h});x+=w;
  }
  void fillRect(int x,int y,int w,int h) override {assert(x>=0 && y>=0 && x+w<=width() && y+h<=height());}
  void drawRect(int,int,int,int) override {}
  void drawXbm(int,int,const uint8_t*,int,int) override {}
};
static void loRaFishRegisterDisplayActivity(DisplayDriver&){}
static uint32_t loRaFishScreenTimeoutMs(){return 15000;}
class UITask;
'''
names='loRaFishChannelNotice loRaFishNameNotice loRaFishDXDestinationKey loRaFishDXDestinationKnown LORAFISH_DX_CONTACT_PAGE loRaFishPage loRaFishOption loRaFishValuePicker loRaFishDisplayOption loRaFishTimeField loRaFishTimeoutField loRaFishSoundField LORAFISH_PAGE_COUNT LORAFISH_RESET_CONFIRM_PAGE loRaFishResetConfirmation LORAFISH_ADVERT_PAGE loRaFishAdvertMenu loRaFishTheme loRaFishColor loRaFishScaleColor loRaFishEyeColor loRaFishColorField loRaFishNeedsAppearanceSetup loRaFishColorDisplay loRaFishSimulationMode loRaFishGuessNumber loRaFishGuessPrevious loRaFishGuessRounds loRaFishGuessScore loRaFishGuessHigher loRaFishGuessResultShown loRaFishGuessCorrect loRaFishGuessComplete loRaFishTimeoutAwakeSetting loRaFishTimeoutAsleepSetting loRaFishTimeoutShortNames loRaFishBrightnessSetting loRaFishBrightnessNames loRaFishBrightnessNeedsApply loRaFishAlwaysOnDimSetting loRaFishAlwaysOnDimNames loRaFishBuzzerVolume loRaFishWakeSound loRaFishSleepSound loRaFishMessageSound loRaFishVolumeNames loRaFishWakeSoundNames loRaFishSleepSoundNames loRaFishMessageSoundNames loRaFishWakeMelodies loRaFishSleepMelodies loRaFishMessageMelodies loRaFishSleepHour loRaFishWakeHour loRaFishUtcOffsetQuarters loRaFishColorNames'.split()
parts=[pre]+[declaration(name) for name in names]
parts += [block('struct LoRaFishThemePalette')+';',declaration('loRaFishThemes'),
 'static constexpr uint8_t LORAFISH_COLOR_THEME_COUNT=4,LORAFISH_COLOR_ALLELE_COUNT=7;',
 source[source.index('static constexpr bool loRaFishHasSound'):source.index('#endif',source.index('static constexpr bool loRaFishHasSound'))+len('#endif')]]
for name in ['static void beginLoRaFishChannelNotice(', 'static bool dismissLoRaFishNotice(', 'static void drawLoRaFishChannelNotice(', 'static void beginLoRaFishNameNotice(', 'static const char* loRaFishDisplayName(', 'static const char* loRaFishProtocolName(', 'static uint8_t loRaFishThemeCount(', 'static uint8_t loRaFishDisplayOptionCount(',
 'static uint8_t loRaFishOptionCount(', 'static void returnToLoRaFishOptions(',
 'static void cancelLoRaFishValue(', 'static void openLoRaFishValue(', 'static void selectLoRaFishValue(',
 'static void loRaFishFormatUtcOffset(', 'static void drawLoRaFishHeader(',
 'static const char* loRaFishOptionLabel(', 'static void drawLoRaFishMenuHint(',
 'static bool loRaFishDXDestination(', 'static bool loRaFishDXEligible(', 'static uint32_t loRaFishDXContactCount(', 'static bool loRaFishDXGetContact(', 'static void moveLoRaFishDXContact(', 'static void drawLoRaFishName(', 'static void drawLoRaFishNameNotice(', 'static void drawLoRaFishDX(',
 'static void drawLoRaFishValuePicker(', 'static void drawLoRaFishOptions(',
 'static void closeLoRaFishAdvertMenu(', 'static void drawLoRaFishAdvertMenu(',
 'static void cancelLoRaFishReset(', 'static void drawLoRaFishResetConfirmation(',
 'static void drawLoRaFishDisplayMenu(', 'static void drawLoRaFishColorMenu(',
 'static void drawLoRaFishTimeMenu(', 'static void drawLoRaFishTimeoutMenu(',
 'static void drawLoRaFishGuessGame(']:
    parts.append(block(name))
parts += ['#ifdef PIN_BUZZER', block('static void drawLoRaFishSoundMenu('),'#endif']
parts.append(r'''
enum class UIEventType{ack};
struct UIScreen{};
class UITask {
public:
  BoundsDisplay display; DisplayDriver* _display=&display;
  UIScreen screen,other; UIScreen* home=&screen; UIScreen* curr=home;
  unsigned long _next_refresh=0,_auto_off=0,_alert_expiry=0,ui_started_at=0;
  int gpsToggles=0,buzzerToggles=0;std::string alert;
  char _alert[80]={};
  void toggleBuzzer(){++buzzerToggles;}
  char handleTripleClick(char);
  void showAlertImpl(const char*,int);
  struct {int previews=0,volume=4;void setVolume(int v){volume=v;} void play(const char*){++previews;}} buzzer;
  void showAlert(const char* s,int t){showAlertImpl(s,t);alert=_alert;}
  void notify(UIEventType){}
  void toggleGPS(){++gpsToggles;}
  void gotoHomeScreen(){setCurrScreen(home);}
  void setCurrScreen(UIScreen*);
  char checkDisplayOn(char);
  char handleLongPress(char);
  char handleDoubleClick(char);
  void previewLoRaFishSoundSetting(uint8_t);
};
''')
for name in ['void UITask::setCurrScreen(', 'char UITask::checkDisplayOn(', 'char UITask::handleDoubleClick(', 'void UITask::previewLoRaFishSoundSetting(']:parts.append(block(name))
parts.append(block('char UITask::handleLongPress('))
parts.append(block('char UITask::handleTripleClick('))
parts.append(block('void UITask::showAlert(').replace('UITask::showAlert(', 'UITask::showAlertImpl('))
home=source[source.index('class HomeScreen'):]
short=block('bool handleInput(char c) override',home).replace('bool handleInput(char c) override','bool shortPress(char c)')
parts.append(short)
parts.append(r'''
static uint8_t pageFor(LoRaFishSetting key){
  const uint8_t pages[]={8,13,15,9,9,9,10,10,10,11,11,11,14,14,14,14,15};
  return pages[static_cast<uint8_t>(key)];
}
int main(){
  UITask task;
  // Start at the aquarium, not an injected Options page: cover the complete entry route.
  for(int contacts:{0,1,3}) for(int state=0;state<4;++state) {
    the_mesh.contacts=contacts;
    loragotchiDXHome.away=state==3;loragotchiDXHome.pending=state==1;
    loragotchiDXHome.committing=state==2;loragotchiDXVisit=LoRaFishVisit{};
    loRaFishDXContactIndex=0;loRaFishPage=0;
    const int sent=the_mesh.dxSends;
    for(int press=0;press<5;++press) shortPress(KEY_NEXT);
    if(loRaFishPage!=5) fprintf(stderr,"OPTIONS UNREACHABLE: contacts=%d state=%d page=%u\n",contacts,state,loRaFishPage);
    assert(loRaFishPage==5 && the_mesh.dxSends==sent && loRaFishDXContactIndex==0);
    task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==0);
    for(int press=0;press<4;++press) shortPress(KEY_NEXT);
    assert(loRaFishPage==4);
    task.display.clearBounds();drawLoRaFishDX(task.display);
    // Select opens the picker but never sends or recalls by itself.
    task.handleLongPress(KEY_ENTER);
    assert(loRaFishPage==LORAFISH_DX_CONTACT_PAGE && the_mesh.dxSends==sent);
    task.display.clearBounds();drawLoRaFishDX(task.display);
    shortPress(KEY_NEXT);
    assert(loRaFishPage==LORAFISH_DX_CONTACT_PAGE && the_mesh.dxSends==sent);
    assert(loRaFishDXContactIndex==(state==0 && contacts>1?1:0));
    shortPress(KEY_PREV);assert(loRaFishDXContactIndex==0);
    task.handleDoubleClick(KEY_PREV);
    assert(loRaFishPage==4 && the_mesh.dxSends==sent);
    shortPress(KEY_NEXT);assert(loRaFishPage==5);
    // Keyboard previous on the overview also leaves it instead of browsing contacts.
    loRaFishPage=4;shortPress(KEY_PREV);assert(loRaFishPage==3);
    loRaFishPage=4;task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==0);
  }
  // Actual DX action dispatch: mocks only at the radio/storage boundary.
  for(int state=0;state<4;++state) {
    loRaFishDXDestinationKnown=false;
    the_mesh.contacts=3;loragotchiDXHome.away=state==3;
    loragotchiDXHome.pending=state==1;loragotchiDXHome.committing=state==2;
    loragotchiDXHome.destinationIndex=2;loRaFishDXContactIndex=0;
    memset(loragotchiDXHome.destinationKey,0,32);loragotchiDXHome.destinationKey[0]=3;loragotchiDXHome.destinationKnown=true;
    task._alert_expiry=0;loRaFishPage=4;
    const int sent=the_mesh.dxSends;
    task.handleLongPress(KEY_ENTER);assert(the_mesh.dxSends==sent);
    if(state==0)shortPress(KEY_NEXT);
    task.handleLongPress(KEY_ENTER);
    assert(the_mesh.dxSends==sent+(state==1?0:1));
    if(state==0) {
      assert(the_mesh.lastMessage.find("LFG1|REQ|")==0 && the_mesh.lastDestination==1);
      assert(loragotchiDXHome.pending);
    } else if(state!=1) {
      assert(the_mesh.lastMessage.find("LFG1|RECALL|")==0 && the_mesh.lastDestination==2);
    }
    task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==4);
  }
  loragotchiDXHome.away=loragotchiDXHome.pending=loragotchiDXHome.committing=false;
  // No-contact, sleep, alert, and wake guards must survive moving DX actions.
  task._alert_expiry=0;the_mesh.contacts=0;loRaFishPage=4;
  const int sent=the_mesh.dxSends;
  task.handleLongPress(KEY_ENTER);task.handleLongPress(KEY_ENTER);
  assert(task.alert=="No fish tanks" && the_mesh.dxSends==sent);
  the_mesh.contacts=3;task._alert_expiry=0;fishSleeping=true;
  task.handleLongPress(KEY_ENTER);assert(task.alert=="Fish is sleeping" && the_mesh.dxSends==sent);
  fishSleeping=false;task._alert_expiry=tick+500;
  task.handleLongPress(KEY_ENTER);assert(the_mesh.dxSends==sent);
  task._alert_expiry=0;task.display.turnOff();task.handleLongPress(KEY_ENTER);
  assert(task.display.isOn() && the_mesh.dxSends==sent);
  task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==4);
  task.display.turnOff();task.handleLongPress(KEY_ENTER);assert(loRaFishPage==4);
  task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==0);
  // Production menu handlers with mocked peripherals.
  for(bool color:{false,true}){
    loRaFishColorDisplay=color;
    for(bool breed:{false,true}){
      canBreed=breed;
      for(uint8_t row=0;row<loRaFishOptionCount();++row){
        loRaFishOption=LoRaFishOptions::at(row,loRaFishHasSound,breed);loRaFishPage=5;
        task.display.clearBounds();drawLoRaFishOptions(task.display,&task);
        const auto selected=loRaFishOption;
        if(selected==LoRaFishOption::Breed)continue;
        task._alert_expiry=0;task.handleLongPress(KEY_ENTER);
        assert(loRaFishPage!=5);
        task.handleDoubleClick(KEY_PREV);
        assert(loRaFishPage==5 && loRaFishOption==selected);
      }
    }
    canBreed=false;
    for(uint8_t row=0;row<loRaFishDisplayOptionCount();++row){
      loRaFishPage=12;loRaFishDisplayOption=row;
      task.display.clearBounds();drawLoRaFishDisplayMenu(task.display);
      task.handleLongPress(KEY_ENTER);task.handleDoubleClick(KEY_PREV);
      assert(loRaFishPage==12 && loRaFishDisplayOption==row);
    }
    // Every value of every setting must fit, cancel without writes, and save only on hold.
    for(uint8_t key=0;key<16;++key){
#ifndef PIN_BUZZER
      if(key>=12)continue;
#endif
      const auto setting=static_cast<LoRaFishSetting>(key);
      loRaFishPage=pageFor(setting);openLoRaFishValue(setting);
      const uint8_t before=loRaFishValuePicker.value(),count=loRaFishValuePicker.count();
      for(uint8_t v=0;v<count;++v){
        task.display.clearBounds();drawLoRaFishValuePicker(task.display);shortPress(KEY_NEXT);
      }
      assert(loRaFishValuePicker.value()==before);
      const int writes=settingsWrites;
      shortPress(KEY_NEXT);task.handleDoubleClick(KEY_PREV);
      assert(settingsWrites==writes && !loRaFishValuePicker.active());
      loRaFishPage=pageFor(setting);openLoRaFishValue(setting);
      assert(loRaFishValuePicker.value()==before);
      shortPress(KEY_NEXT);task.handleLongPress(KEY_ENTER);
      assert(settingsWrites==writes+1 && !loRaFishValuePicker.active());
      openLoRaFishValue(setting);assert(loRaFishValuePicker.value()==(before+1)%count);
      loRaFishValuePicker.cancel();
    }
  }
  // Field navigation never edits or persists a setting.
  for(uint8_t page:{9,10,11,14}){
#ifndef PIN_BUZZER
    if(page==14)continue;
#endif
    loRaFishPage=page;loRaFishNeedsAppearanceSetup=false;
    loRaFishColorField=loRaFishTimeField=loRaFishTimeoutField=loRaFishSoundField=0;
    int writes=settingsWrites;shortPress(KEY_NEXT);assert(settingsWrites==writes);
    task.handleLongPress(KEY_ENTER);assert(loRaFishValuePicker.active());
    const LoRaFishSetting expected=page==9?LoRaFishSetting::Body:page==10?LoRaFishSetting::WakeHour:
        page==11?LoRaFishSetting::TimeoutAsleep:LoRaFishSetting::WakeSound;
    assert(loRaFishValuePicker.key()==expected);
    task.handleDoubleClick(KEY_PREV);assert(loRaFishPage==page && settingsWrites==writes);
  }
  // Aquarium single is always next, never cleaning. Hold retains cleaning.
  loRaFishPage=0;shortPress(KEY_NEXT);assert(loRaFishPage==1 && cleans==0);
  loRaFishPage=0;task.handleLongPress(KEY_ENTER);assert(cleans==1);
  // Global sleeping and alert guards protect edits/actions.
  loRaFishPage=10;openLoRaFishValue(LoRaFishSetting::SleepHour);shortPress(KEY_NEXT);
  int writes=settingsWrites;task.display.turnOff();task.handleLongPress(KEY_ENTER);
  assert(task.display.isOn() && settingsWrites==writes && loRaFishValuePicker.active());
  task._alert_expiry=tick+500;task.handleLongPress(KEY_ENTER);assert(settingsWrites==writes);
  task._alert_expiry=0;task.handleLongPress(KEY_ENTER);assert(settingsWrites==writes+1);
  // Screen interruption discards an editor; Back on a message returns to the visible home screen.
  loRaFishPage=13;openLoRaFishValue(LoRaFishSetting::Brightness);shortPress(KEY_NEXT);
  writes=settingsWrites;task.setCurrScreen(&task.other);
  assert(!loRaFishValuePicker.active() && loRaFishPage==12 && settingsWrites==writes);
  task.handleDoubleClick(KEY_PREV);assert(task.curr==task.home);
  // Appearance completion is explicit Select, never Back.
  loRaFishColorDisplay=true;loRaFishNeedsAppearanceSetup=true;loRaFishPage=9;loRaFishColorField=3;
  task.handleDoubleClick(KEY_PREV);assert(loRaFishNeedsAppearanceSetup && loRaFishPage==12);
  task.handleLongPress(KEY_ENTER);assert(loRaFishPage==9);
  loRaFishColorField=3;task.handleLongPress(KEY_ENTER);assert(!loRaFishNeedsAppearanceSetup && loRaFishPage==0);
  task._alert_expiry=0;
  // Previously accepted reset/advert behaviors survive the reordered parent menu.
  returnToLoRaFishOptions(LoRaFishOption::Reset);task.handleLongPress(KEY_ENTER);
  assert(!loRaFishResetConfirmation.resetSelected());task.handleLongPress(KEY_ENTER);
  assert(resets==0 && loRaFishPage==5 && loRaFishOption==LoRaFishOption::Reset);
  returnToLoRaFishOptions(LoRaFishOption::Advert);task.handleLongPress(KEY_ENTER);
  task.handleLongPress(KEY_ENTER);assert(the_mesh.sends==1 && loRaFishPage==5 && loRaFishOption==LoRaFishOption::Advert);
  task._alert_expiry=0;task.handleLongPress(KEY_ENTER);task.handleDoubleClick(KEY_PREV);
  assert(the_mesh.sends==1 && loRaFishOption==LoRaFishOption::Advert);
  // Field lists and accepted dialogs: detect both clipping and overlapping glyph cells.
  loRaFishScaleColor=loRaFishColor=loRaFishEyeColor=1;loRaFishNeedsAppearanceSetup=true;
  loRaFishTimeoutAwakeSetting=loRaFishTimeoutAsleepSetting=5;
  loRaFishWakeSound=2;loRaFishSleepSound=2;loRaFishMessageSound=3;
  for(auto draw:{drawLoRaFishColorMenu,drawLoRaFishTimeMenu,drawLoRaFishTimeoutMenu,
                drawLoRaFishResetConfirmation,drawLoRaFishAdvertMenu,drawLoRaFishGuessGame}){
    task.display.clearBounds();draw(task.display);
  }
#ifdef PIN_BUZZER
  task.display.clearBounds();drawLoRaFishSoundMenu(task.display);
#endif
  printf("PASS: aquarium-to-Options across 12 contact/travel combinations, DX picker/send/recall guards, production menu handlers/settings bindings, parent returns, optional rows, save/cancel, wake/alert guards, reset/advert regression, text bounds/overlap; sound=%d\n",loRaFishHasSound);
}
''')
if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='lorafish-menu-check-') as temporary:
        cpp=Path(temporary)/'menu-host-check.cpp';cpp.write_text('\n'.join(parts))
        for sound in [False,True]:
            out=Path(temporary)/f'menu-host-check-{int(sound)}'
            cmd=['g++','-std=c++17','-I',str(root),'-I',str(root/'src'),str(cpp),'-o',str(out)]
            if sound:cmd+=['-DPIN_BUZZER=6']
            subprocess.run(cmd,check=True)
            subprocess.run([str(out)],check=True)
