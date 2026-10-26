#!/usr/bin/env python3
"""Compile production DX receive and host-return handlers with only radio mocks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'examples/companion_radio/MyMesh.cpp').read_text()
header=(root/'examples/companion_radio/MyMesh.h').read_text()
def block(marker, source=source):
    start=source.index(marker);opening=source.index('{',start);end=opening+1;depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
structs=header[header.index('struct LoRaGotchiDXHomeState'):header.index('extern LoRaGotchiDXHomeState')]
queue=block('void MyMesh::queueMessage(')
a=queue.index('#ifdef LORAFISH_UI')+len('#ifdef LORAFISH_UI');b=queue.index('\n#endif',a)
receive='void MyMesh::receive(const ContactInfo& from,const char* text){'+queue[a:b]+'\n}'
pre=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include "examples/companion_radio/LoRaFishGuestReturn.h"
#include "examples/companion_radio/LoRaFishCommit.h"
#include "examples/companion_radio/LoRaFishVisit.h"
#include "examples/companion_radio/LoRaFishArrival.h"
#define LORAFISH_MONO_PIXEL_TINT_RGB565 65535
#define MSG_SEND_FAILED -1
static uint32_t now=0;unsigned long millis(){return now;}
struct {template<typename... A> void printf(const char*,A...){}void println(const char*){}} Serial;
struct ContactInfo {struct {uint8_t pub_key[32]={};} id;char name[32]="Guest";};
'''
globals=r'''
LoRaGotchiDXHomeState loragotchiDXHome;LoRaGotchiDXGuestState loragotchiDXGuest;
static LoRaFishCommitFrame loRaFishCommitFrame;
static LoRaFishArrival loRaFishArrival;
LoRaFishGuestReturn loragotchiDXGuestReturn;
LoRaFishVisit loragotchiDXVisit;LoRaFishReservation loragotchiDXReservation;
bool loragotchiDXLocalColorCapable=false,loragotchiDXLocalHasColorGenes=true,loragotchiDXReturnedFromMonochrome=false;
uint8_t loragotchiDXLocalColorIndex=1,loragotchiDXLocalScalesColorIndex=2,loragotchiDXLocalEyesColorIndex=3,loragotchiDXLocalMonoPanel=2;
uint16_t loragotchiDXLocalMonoPixelTint=31;
bool loragotchiEggMessage(const uint8_t*,const char*,char*,size_t){return false;}
static unsigned saves=0;
void loragotchiDXSave(){++saves;}
class MyMesh {
public:
 std::vector<std::string> sent;std::vector<uint8_t> destinations;std::vector<ContactInfo> contacts;bool failure=false;uint32_t hash=100,estimate=10000;
 struct RTC{uint32_t getCurrentTime(){return now/1000;}}rtc;
 RTC* getRTCClock(){return &rtc;}
 int sendMessage(const ContactInfo& c,uint32_t,int,const char* msg,uint32_t& ack,uint32_t& timeout){sent.emplace_back(msg);destinations.push_back(c.id.pub_key[0]);ack=++hash;timeout=estimate;return failure?-1:0;}
 ContactInfo* lookupContactByPubKey(const uint8_t* key,int size){for(auto& c:contacts)if(!memcmp(c.id.pub_key,key,size))return &c;return nullptr;}
 void receive(const ContactInfo&,const char*);
 int sendLoRaFishCommit(const ContactInfo&);
 bool queueReady=true,txPending=false;uint32_t random=0;
 bool loRaFishQueueReady() const{return queueReady;}
 bool loRaFishTxPending() const{return txPending;}
 uint32_t loRaFishRandom(){return random;}
 void trackLoRaFishRequest(const char*,uint32_t);
 bool recallLoRaFish();void serviceLoRaFishVisit();void expireLoRaFishReservation();
 bool returnLoRaFishGuest();void serviceLoRaFishGuestReturn();
 bool acknowledgeLoRaFishGuestReturn(uint32_t);
 bool acknowledgeLoRaFishArrival(uint32_t);
 void serviceLoRaFishArrival();
};
'''
ui_source=(root/'examples/companion_radio/ui-new/UITask.cpp').read_text()
ui_globals=r'''
struct LoRaFishAppearance {};
#include "examples/companion_radio/ui-new/LoRaFishHappiness.h"
static LoRaFishHappiness loRaFishHappiness;
static bool loRaFishNeedsAppearanceSetup=false;
static constexpr uint32_t LORAFISH_EGG_MINUTES=5,LORAFISH_LIFESPAN_MINUTES=10080;
struct LoRaFishTravelAnimation {int queues=0;};
static LoRaFishTravelAnimation loRaFishHomeTravelAnimation,loRaFishGuestTravelAnimation;
static bool loRaFishTravelStateInitialized=false,loRaFishLastHomeAway=false,loRaFishLastGuestPresent=false;
static uint8_t loRaFishLastGuestStage=0;
static LoRaFishAppearance loRaFishLastGuestAppearance;
static struct {uint32_t dxVisits=0;uint8_t evolution=2;uint32_t ageMinutes=60;uint8_t happiness=80;} fish;
static int petSaves=0;
void saveLoRaFish(){++petSaves;}
LoRaFishAppearance loRaFishGuestRenderAppearance(){return {};}
LoRaFishAppearance loRaFishLocalAppearance(){return {};}
void loRaFishQueueTravelAnimation(LoRaFishTravelAnimation& animation,bool,uint8_t,const LoRaFishAppearance&){++animation.queues;}
'''
main=r'''
int main(){
 MyMesh m;ContactInfo owner,impostor;owner.id.pub_key[0]=1;impostor.id.pub_key[0]=2;m.contacts={owner,impostor};
 m.receive(owner,"LFG1|REQ|7|8|2|Fish");m.receive(owner,"LFG1|COMMIT|7|8|C|1|2|3|2016");
 assert(loragotchiDXGuest.present && loragotchiDXGuest.ownerKey[0]==1);
 m.receive(owner,"LFG1|COMMIT|7|8|C|1|2|3|31|P1|2");assert(loragotchiDXGuest.monoPanel==2 && loragotchiDXGuest.monoPixelTint==31);
 m.receive(owner,"LFG1|COMMIT|7|8");assert(loragotchiDXGuest.color==1 && loragotchiDXGuest.scalesColor==2);
 m.receive(impostor,"LFG1|COMMIT|7|8|C|4|4|4|65535");m.receive(impostor,"LFG1|RECALL|7|8");
 assert(loragotchiDXGuest.color==1 && loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Idle);
 puts("PASS production receive binds guest identity, preserves colour and panel profile on short COMMIT");
 assert(m.returnLoRaFishGuest());m.serviceLoRaFishGuestReturn();assert(m.sent.back()=="LFG1|RETURN|7|8|M");assert(m.destinations.back()==1);
 auto sends=m.sent.size();
 m.receive(owner,"LFG1|COMMIT|7|8");m.receive(owner,"LFG1|REQ|7|8|2|Fish");
 assert(m.sent.size()==sends); // duplicate traffic does not bypass retry timing
 for(int i=1;i<=4;i++){now=i*10000;m.serviceLoRaFishGuestReturn();}
 assert(loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Failed && loragotchiDXGuest.present);
 loragotchiDXLocalColorCapable=true;assert(m.returnLoRaFishGuest());m.serviceLoRaFishGuestReturn();assert(m.sent.back()=="LFG1|RETURN|7|8|C");
 puts("PASS actual host return handler uses full owner key, bounded retries, preserves mono versus colour policy");
 loragotchiDXHome.txid=90;loragotchiDXHome.fishid=91;loragotchiDXHome.pending=true;
 loragotchiDXHome.destinationKnown=true;memcpy(loragotchiDXHome.destinationKey,owner.id.pub_key,32);
 m.receive(impostor,"LFG1|ACK|90|91");assert(loragotchiDXHome.pending);
 m.receive(impostor,"LFG1|BUSY|90|91");assert(loragotchiDXHome.pending);
 m.receive(owner,"LFG1|ACK|90|91");assert(loragotchiDXHome.committing && !loragotchiDXHome.pending);
 m.receive(impostor,"LFG1|RETURN|90|91|C");assert(loragotchiDXHome.committing);
 m.receive(owner,"LFG1|RETURN|90|91|C");assert(loragotchiDXHome.returned && loragotchiDXHome.completedVisit && !loragotchiDXHome.committing && !loragotchiDXHome.away);
 m.receive(owner,"LFG1|ARRIVED|90|91");assert(!loragotchiDXHome.away);
 loragotchiDXHome.returned=false;loragotchiDXHome.completedVisit=false;
 auto writes=saves;m.receive(owner,"LFG1|RETURN|90|91|C");
 assert(!loragotchiDXHome.returned && !loragotchiDXHome.completedVisit && saves==writes);
 puts("PASS existing unsolicited return works while committing, late ARRIVED cannot hide returned fish, duplicate RETURN is inert");
 m.contacts={impostor};loragotchiDXGuestReturn.start(now);m.serviceLoRaFishGuestReturn();assert(loragotchiDXGuestReturn.hashes[0]==0);
 puts("PASS deleted owner contact never reroutes return to a different device");

 m.contacts={impostor,owner};m.estimate=45000;
 loragotchiDXGuestReturn.start(now);m.serviceLoRaFishGuestReturn();
 auto firstHash=loragotchiDXGuestReturn.hashes[0];sends=m.sent.size();
 now+=44999;m.serviceLoRaFishGuestReturn();assert(m.sent.size()==sends);
 now++;m.serviceLoRaFishGuestReturn();assert(m.sent.size()==sends+1);
 assert(!m.acknowledgeLoRaFishGuestReturn(firstHash+1000));
 assert(m.acknowledgeLoRaFishGuestReturn(firstHash));
 assert(!loragotchiDXGuest.present && !loragotchiDXGuest.reserved);
 writes=saves;assert(!m.acknowledgeLoRaFishGuestReturn(firstHash));assert(saves==writes);
 puts("PASS estimated airtime timeout is honored, a delayed ACK for an earlier retry completes once");

 m.receive(owner,"LFG1|COMMIT|7|8|C|5|5|5|65535");
 assert(!loragotchiDXGuest.present && m.sent.back()=="LFG1|RETURN|7|8|C");
 m.receive(owner,"LFG1|REQ|7|8|2|Fish");
 assert(!loragotchiDXGuest.reserved && m.sent.back()=="LFG1|RETURN|7|8|C");
 puts("PASS completed transaction replays RETURN without resurrection or colour mutation");

 m.receive(impostor,"LFG1|REQ|8|9|2|Next");m.receive(impostor,"LFG1|COMMIT|8|9|C|4|3|2|2016");
 assert(loragotchiDXGuest.ownerKey[0]==2 && loragotchiDXGuest.present);
 assert(!m.acknowledgeLoRaFishGuestReturn(firstHash));
 m.receive(owner,"LFG1|RECALL|7|8");assert(loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Idle);
 ContactInfo samePrefix=impostor;samePrefix.id.pub_key[31]=99;
 m.receive(samePrefix,"LFG1|RECALL|8|9");assert(loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Idle);
 m.receive(impostor,"LFG1|RECALL|99|9");assert(loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Idle);
 puts("PASS full 32-byte sender identity, transaction and stale ACK guards protect a newer visitor");

 m.failure=true;m.estimate=10000;m.receive(impostor,"LFG1|RECALL|8|9");
 for(int i=0;i<4;i++){m.serviceLoRaFishGuestReturn();assert(!m.acknowledgeLoRaFishGuestReturn(m.hash));now+=10000;}
 m.serviceLoRaFishGuestReturn();assert(loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Failed && loragotchiDXGuest.present);
 sends=m.sent.size();now+=600000;m.serviceLoRaFishGuestReturn();assert(m.sent.size()==sends);
 m.failure=false;assert(m.returnLoRaFishGuest());m.serviceLoRaFishGuestReturn();
 assert(m.acknowledgeLoRaFishGuestReturn(m.hash) && !loragotchiDXGuest.present);
 puts("PASS failed sends never confirm, retry budget stops, explicit retry can complete");

 LoRaFishGuestReturn timer;now=0xFFFFFF00U;timer.start(now);assert(timer.ready(now));
 timer.sent(0,now,10000,true);assert(!timer.ready(uint32_t(now+9999)));
 assert(timer.ready(uint32_t(now+10000)));assert(timer.acknowledge(0));
 timer.start(0);for(int i=0;i<4;++i)timer.sent(i+10,i*10000,10000,true);
 assert(!timer.ready(40000) && timer.status==LoRaFishGuestReturn::Failed);
 assert(timer.acknowledge(10));
 puts("PASS retry deadlines survive millis wrap, hash zero is valid, late failed-state ACK completes");

 loragotchiDXHome.pending=true;loragotchiDXHome.destinationKey[31]=0;
 ContactInfo wrongHost=owner;wrongHost.id.pub_key[31]=1;
 m.receive(wrongHost,"LFG1|RETURN|90|91|C");assert(loragotchiDXHome.pending);
 m.receive(owner,"LFG1|BUSY|91|91");assert(loragotchiDXHome.pending);
 m.receive(owner,"LFG1|RETURN|90|91|C");assert(!loragotchiDXHome.pending && !loragotchiDXHome.completedVisit);
 puts("PASS full destination key required; cancellation before COMMIT does not count a visit");

 loragotchiDXHome.completedVisit=true;loRaFishObserveTravelTransitions();
 assert(fish.dxVisits==1 && petSaves==1 && !loragotchiDXHome.completedVisit);
 loRaFishObserveTravelTransitions();assert(fish.dxVisits==1 && petSaves==1);
 loragotchiDXHome.away=true;loRaFishObserveTravelTransitions();
 loragotchiDXHome.away=false;loragotchiDXHome.completedVisit=true;loRaFishObserveTravelTransitions();
 assert(fish.dxVisits==2 && petSaves==2 && loRaFishHomeTravelAnimation.queues==2);
 loragotchiDXHome.away=true;loRaFishObserveTravelTransitions();
 loragotchiDXHome.away=false;loRaFishObserveTravelTransitions();
 assert(fish.dxVisits==2); // no invented success on a reset/timeout edge
 puts("PASS production care observer counts rapid and normal returns once, not visual/reset edges");

 loragotchiDXHome.committing=true;
 m.receive(wrongHost,"LFG1|RETURN|90|91|C|AQUARIUM1");
 assert(loragotchiDXHome.committing && !loragotchiDXHome.communityVisit);
 m.receive(owner,"LFG1|RETURN|90|91|C|AQUARIUM1");
 assert(loragotchiDXHome.communityVisit && loragotchiDXHome.completedVisit);
 loRaFishObserveTravelTransitions();assert(fish.happiness==85 && loRaFishHappiness.aquariumCooldownMs==21600000);
 m.receive(owner,"LFG1|RETURN|90|91|C|AQUARIUM1");loRaFishObserveTravelTransitions();assert(fish.happiness==85);
 loragotchiDXHome.committing=true;m.receive(owner,"LFG1|RETURN|90|91|C|AQUARIUM1");
 loRaFishObserveTravelTransitions();assert(fish.happiness==85);
 puts("PASS explicit community return from the active destination grants +5 only once within the cap");
 for(const char* suffix:{"C","C|AQUARIUM1x","C|AQUARIUM1|junk","M|AQUARIUM1"}) {
   loRaFishHappiness.aquariumCooldownMs=0;loragotchiDXHome.committing=true;
   m.receive(owner,(std::string("LFG1|RETURN|90|91|")+suffix).c_str());
   loRaFishObserveTravelTransitions();assert(fish.happiness==85 && loRaFishHappiness.aquariumCooldownMs==0);
 }
 puts("PASS ordinary and malformed community markers return the fish without awarding a bonus");
 loragotchiDXGuest={};loragotchiDXGuestReturn={};loragotchiDXReservation={};
 m.receive(owner,"LFG1|REQ|100|101|2|Hue fish");
 m.receive(owner,"LFG1|COMMIT|100|101|C|5|4|6|65535|H1|220|120|6");
 assert(loragotchiDXGuest.color==220 && loragotchiDXGuest.scalesColor==120 && loragotchiDXGuest.eyesColor==6);
 m.receive(owner,"LFG1|COMMIT|100|101");assert(loragotchiDXGuest.color==220);
 puts("PASS production receiver applies H1 genes and short legacy retry preserves them");
}
'''
methods='\n'.join(block(name) for name in [
    'int MyMesh::sendLoRaFishCommit(', 'void MyMesh::trackLoRaFishRequest(',
    'bool MyMesh::recallLoRaFish()', 'void MyMesh::expireLoRaFishReservation()',
    'void MyMesh::serviceLoRaFishVisit()', 'bool MyMesh::returnLoRaFishGuest()',
    'bool MyMesh::acknowledgeLoRaFishGuestReturn(', 'void MyMesh::serviceLoRaFishGuestReturn()'])
methods += '\n' + '\n'.join(block(name) for name in [
    'bool MyMesh::acknowledgeLoRaFishArrival(', 'void MyMesh::serviceLoRaFishArrival()'])

def run_checks():
    with tempfile.TemporaryDirectory(prefix='lrx-dx-test-') as tmp:
        path=Path(tmp);cpp=path/'dx.cpp';exe=path/'dx'
        cpp.write_text(pre+structs+globals+receive+methods+ui_globals+block('static void loRaFishObserveTravelTransitions()',ui_source)+main)
        subprocess.run(['g++','-std=c++17','-I',str(root),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__ == "__main__":
    run_checks()
