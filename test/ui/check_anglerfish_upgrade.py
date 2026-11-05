#!/usr/bin/env python3
"""Execute old-registry SPC1 bytes through current production boot serializers.
Preserved pre-Air Water registry is an independent 14-choice fixture.
"""
from pathlib import Path
import subprocess,tempfile
import check_fish_lifecycle as f

code=chr(10).join(f.parts[:-1])+f.parts[-1].split("int main() {",1)[0]
code+=f.block("static void ensureLoRaFishMysterySeed(")
old=(f.ROOT/"test/ui/pre_air_species.inc").read_text().replace("#pragma once","")
code+=chr(10)+"#define LoRaFishSpecies PreAnglerSpecies"+chr(10)+old+chr(10)+"#undef LoRaFishSpecies"+chr(10)
code+=r'''
static void oldSave(uint16_t species,uint16_t egg,uint32_t seed) {
 resetAt(5000);saveLoRaFish();saveLoRaFishSettings();
 assert(files["/lorafish.cfg"].size()==78);
 PreAnglerSpecies::encode(files["/lorafish.cfg"].data()+60,species,egg,seed,true);
 reboot();
}
int main() {
 oldSave(14,14,123);
 assert(loRaFishSpecies==14 && loRaFishEggSpecies==14 && fish.ageMinutes==5000);
 assert(PreAnglerSpecies::renderId(14)==0 && LoRaFishSpecies::renderId(14)==14);
 saveLoRaFishSettings();reboot();assert(loRaFishSpecies==14 && loRaFishEggSpecies==14);
 oldSave(15,65535,123);
 assert(loRaFishSpecies==15 && loRaFishEggSpecies==65535);
 assert(LoRaFishSpecies::renderId(15)==0 && LoRaFishSpecies::renderId(65535)==0);
 uint32_t seed=1;
 while(seed<10000 && PreAnglerSpecies::mystery(seed)==LoRaFishSpecies::mystery(seed))++seed;
 assert(seed<10000);
 const uint16_t previous=PreAnglerSpecies::mystery(seed),expanded=LoRaFishSpecies::mystery(seed);
 assert(previous!=expanded);
 oldSave(previous,13,seed);
 assert(loRaFishSpecies==previous && loRaFishEggSpecies==13 && loRaFishMysterySeed==seed);
 ensureLoRaFishMysterySeed();assert(loRaFishSpecies==previous && loRaFishMysterySeed==seed);
 // Deliberate Mystery reselection uses the expanded catalogue, not load-time migration.
 loRaFishSpecies=LoRaFishSpecies::mystery(loRaFishMysterySeed);
 assert(loRaFishSpecies==expanded);saveLoRaFishSettings();reboot();assert(loRaFishSpecies==expanded);
 // An unresolved old seed is retained and resolves only when explicitly selected.
 oldSave(0,0,seed);assert(loRaFishSpecies==0 && loRaFishMysterySeed==seed);
 assert(LoRaFishSpecies::mystery(loRaFishMysterySeed)==expanded);
 for(unsigned role=0;role<3;++role)
  assert(PreAnglerSpecies::mysteryColour(seed,role)==LoRaFishSpecies::mysteryColour(seed,role));
 pass("old SPC1 resolved/unresolved Mystery and opaque14 upgrade; other unknown IDs and colour mapping preserved");
}
'''
with tempfile.TemporaryDirectory(prefix='angler-upgrade-') as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(code)
    for platform in ('ESP32','NRF52_PLATFORM'):
        exe=p/platform
        subprocess.run(['g++','-std=c++17','-DLORAGOTCHI_WATER_EDITION=1','-I',str(f.ROOT),'-D'+platform,str(p/'test.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
