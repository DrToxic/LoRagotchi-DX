#include "UITask.h"
#include "PetResetConfirmation.h"
#include "AdvertMenu.h"
#include "LoRaFishMenu.h"
#include "LoRaFishHappiness.h"
#include "LoRaFishText.h"
#include "../LoRaFishColour.h"
#include "../MyMesh.h"
#include "../LoRagotchiChannelStatus.h"
#ifdef ESP32
#include <SPIFFS.h>
#endif
#if defined(LORAFISH_TDECK_NVS) && defined(ESP32)
#include <Preferences.h>
#include <vector>
// File-compatible, bounded RAM buffer backed by a dedicated NVS namespace.
// Only the two tiny fish records use this; MeshCore storage is unchanged.
class LoRaFishNVSFile {
  Preferences prefs;
  std::vector<uint8_t> bytes;
  size_t cursor=0;
  bool writable=false;
  bool opened=false;
  const char* key;
  bool saved = true;
public:
  LoRaFishNVSFile(const char* record, bool write):writable(write),key(record) {
    opened=prefs.begin("lfgfish", !write);
    if (!opened) { Serial.println("[LFG NVS] namespace open failed"); return; }
    if (!write) {
      const size_t len=prefs.getBytesLength(key);
      if (len>0 && len<=512) {
        bytes.resize(len);
        if (prefs.getBytes(key,bytes.data(),len)!=len) bytes.clear();
      }
    }
  }
  ~LoRaFishNVSFile() { close(); }
  explicit operator bool() const { return opened && (writable || !bytes.empty()); }
  bool recordExists() { return !opened || prefs.isKey(key); }
  size_t size() const { return bytes.size(); }
  size_t write(uint8_t v) { return write(&v,1); }
  size_t write(const uint8_t* data,size_t len) {
    if (!opened || !writable || bytes.size()+len>512) return 0;
    bytes.insert(bytes.end(),data,data+len);
    return len;
  }
  int read(uint8_t* dest,size_t len) {
    const size_t available=cursor<bytes.size()?bytes.size()-cursor:0;
    const size_t count=len<available?len:available;
    if(count) memcpy(dest,bytes.data()+cursor,count);
    cursor+=count;
    return (int)count;
  }
  bool close() {
    if (!opened) return saved;
    if (writable) {
      size_t written=prefs.putBytes(key,bytes.data(),bytes.size());
      saved = written == bytes.size();
      Serial.printf("[LFG NVS] save %s %u/%u bytes %s\n",key,(unsigned)written,(unsigned)bytes.size(),written==bytes.size()?"OK":"FAILED");
    } else {
      Serial.printf("[LFG NVS] read %s %u bytes\n",key,(unsigned)bytes.size());
    }
    prefs.end(); opened=false;
    return saved;
  }
};
#endif
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#include <time.h>
#ifdef ENABLE_WIFI_INTERFACE
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK || UI_HAS_ROTARY_INPUT
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"


static mesh::RTCClock* loRaFishRTC = nullptr;

static const char* loRaFishMeshNodeName = nullptr;
static bool loRaFishNameNotice = false;
static bool loRaFishChannelNotice = false;
static bool loRaFishMonoDisplayNotice = false;
// OLED colour is physical hardware. This persistent panel profile lets colour
// peers render a monochrome visitor faithfully; Blue is the common default.
static uint8_t loRaFishMonoDisplayColour = 0;
static bool loRaFishMonoDisplayColourKnown = false;
static void saveLoRaFishSettings();

static void beginLoRaFishChannelNotice() {
  loRaFishChannelNotice = LoRagotchiChannel::startupResult == LoRagotchiChannel::Result::Full;
}

static bool dismissLoRaFishNotice() {
  // One gesture dismisses one notice, without navigating, saving or sending.
  if (loRaFishChannelNotice) { loRaFishChannelNotice = false; return true; }
  if (loRaFishNameNotice) { loRaFishNameNotice = false; return true; }
  if (loRaFishMonoDisplayNotice) {
    loRaFishMonoDisplayNotice = false;
    loRaFishMonoDisplayColourKnown = true;
    saveLoRaFishSettings();
    return true;
  }
  return false;
}

static void beginLoRaFishNameNotice() {
  loRaFishNameNotice = !LoRaFishText::hasFish(loRaFishMeshNodeName, 32);
}

static const char* loRaFishProtocolName() {
  static char name[16];
  LoRaFishText::protocolName(name, sizeof(name), loRaFishMeshNodeName, 32);
  return name[0] ? name : "LoRaFish";
}

static const char* loRaFishDisplayName() {
  if (!loRaFishMeshNodeName || !loRaFishMeshNodeName[0]) return "LoRaFish";

  // Keep the left header clear of the clock/right-side status on 128px displays.
  static char shortName[16];
  LoRaFishText::display(shortName, sizeof(shortName), loRaFishMeshNodeName, 32);
  return shortName;
}

// ============================================================
// LoRaFish MeshCore Edition v0.3.2 - DisplayDriver compatibility
// MeshCore owns radio + Companion BLE. This pet layer only observes UI events.
// ============================================================
struct LoRaFishStateV1 { // layout shipped before the DX and uptime counters
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t rfEvents;
  uint32_t messages;
  uint32_t txAcks;
  uint8_t happiness;
  uint8_t evolution;
};

struct LoRaFishState {
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t rfEvents;
  uint32_t messages;
  uint32_t txAcks;
  uint8_t happiness;
  uint8_t evolution;
  uint32_t dxVisits;
  uint32_t longestUptimeMinutes;
};

static constexpr uint32_t LORAFISH_MAGIC = 0x4C464D32; // "LFM2"
static constexpr uint32_t LORAFISH_MAGIC_V1 = 0x4C464D31; // "LFM1"
static void saveLoRaFish();
static void saveLoRaFishSettings();
static void scheduleLoRaFishPoop();
static LoRaFishState fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
static uint32_t fishLastMinute = 0;
static uint32_t fishLastSave = 0;
static LoRaFishHappiness loRaFishHappiness;
static LoRaFishHappiness lastWrittenLoRaFishHappiness;
static bool haveLastWrittenLoRaFishHappiness = false;
static uint32_t loRaFishHappyLastMs = 0;
static uint32_t loRaFishHappyLastSavedMs = 0;
static constexpr uint32_t LORAFISH_HAPPY_SETTINGS_MAGIC = 0x48415031; // "HAP1"
static void advanceLoRaFishHappiness(uint32_t until);
static void tickLoRaFish();
static unsigned long fishReactionUntil = 0;
static uint8_t fishReaction = 0; // 0 normal, 1 RX, 2 TX/ACK, 3 new contact

static uint8_t loRaFishPage = 0; // 0 aquarium, 1 stats, 2 clock, 3 pairing, 4 DX, 5 options, 6 RF, 7 GNSS, 8 theme, 9 fish, 10 schedule, 11 timeout, 12 display, 13 brightness
static LoRaFishOption loRaFishOption = LoRaFishOption::Advert;
static LoRaFishValuePicker loRaFishValuePicker;
static uint8_t loRaFishDisplayOption = 0;
static uint8_t loRaFishGuessNumber=0;
static uint8_t loRaFishGuessPrevious=0;
static uint8_t loRaFishGuessRounds=0;
static uint8_t loRaFishGuessScore=0;
static bool loRaFishGuessHigher=true;
static bool loRaFishGuessResultShown=false;
static bool loRaFishGuessCorrect=false;
static bool loRaFishGuessComplete=false;
static bool loRaFishSimulationMode = false;
static uint8_t loRaFishTimeField = 0;
static uint8_t loRaFishTimeoutField = 0;
static uint8_t loRaFishSoundField = 0;
static constexpr uint8_t LORAFISH_PAGE_COUNT = 8;
static constexpr uint8_t LORAFISH_RESET_CONFIRM_PAGE = 17;
static PetResetConfirmation loRaFishResetConfirmation;
static constexpr uint8_t LORAFISH_ADVERT_PAGE = 18;
static AdvertMenu loRaFishAdvertMenu;
static constexpr uint8_t LORAFISH_DX_CONTACT_PAGE = 19;
static int8_t loRaFishRfHistory[48] = {0};
static uint8_t loRaFishRfHistoryCount = 0;
static uint32_t loRaFishRfSeenSeq = 0;
static uint8_t loRaFishTheme = 0;
static uint8_t loRaFishColor = 0;
static constexpr uint8_t LORAFISH_MONOCHROME_COLOR = 6;
static constexpr uint8_t LORAFISH_MONOCHROME_RENDER_TINT = 7;
static uint8_t loRaFishScaleColor = 0;
static uint8_t loRaFishEyeColor = 0;
static uint8_t loRaFishColorField = 0;  // Scales, Body, Eyes, then Start fish during setup
static bool loRaFishAppearanceLoaded = false;
static bool loRaFishNeedsAppearanceSetup = false;
static bool loRaFishAdoptionEligible = false;
static bool loRaFishColorDisplay = false;

struct LoRaFishAppearance {
  uint8_t scales;
  uint8_t body;
  uint8_t eyes;
};
struct LoRaFishTravelAnimation {
  bool pending=false;
  bool active=false;
  bool entering=false;
  uint8_t stage=2;
  LoRaFishAppearance appearance={0,0,0};
  unsigned long started=0;
  int startX=0;
  int endX=0;
  int y=38;
};
static constexpr unsigned long LORAFISH_TRAVEL_ANIMATION_MS = 1000UL;
static LoRaFishTravelAnimation loRaFishHomeTravelAnimation;
static LoRaFishTravelAnimation loRaFishGuestTravelAnimation;
static bool loRaFishTravelStateInitialized=false;
static bool loRaFishReturnNavigationHandled=false;
static bool loRaFishLastHomeAway=false;
static bool loRaFishLastGuestPresent=false;
static uint8_t loRaFishLastGuestStage=2;
static LoRaFishAppearance loRaFishLastGuestAppearance={0,0,0};
static uint16_t loRaFishGeneration = 1;
static bool loRaFishPendingEgg = false;
static LoRaFishAppearance loRaFishEggAppearance = {0,0,0};
static int8_t loRaFishMemorialEggSide = 0;

struct LoRaFishThemePalette {
  const char* name;
  ColorVal background;
  ColorVal titleBackground;
  ColorVal titleText;
  ColorVal primaryText;
  ColorVal secondaryText;
  ColorVal warningText;
  ColorVal popupBackground;
  ColorVal popupText;
  ColorVal accent;
};

static const LoRaFishThemePalette loRaFishThemes[] = {
  {"Midnight", 0x0861, 0x1193, 0xFFFF, 0xEF7D, 0x9CF3, 0xFFE0, 0x1082, 0xFFFF, 0x07FF},
  {"Paper",    0xFFFF, 0x001F, 0xFFFF, 0x0000, 0x7BEF, 0xF800, 0xCE79, 0x0000, 0x041F},
  {"Lagoon",   0x0128, 0x03B0, 0xFFFF, 0xDFFF, 0x7DDF, 0xFEA0, 0x032A, 0xFFFF, 0x4DFF},
  {"Sunset",   0x3006, 0xA205, 0xFFFF, 0xFFF1, 0xFD20, 0xF800, 0x5805, 0xFFFF, 0xF9A6},
};
static constexpr uint8_t LORAFISH_COLOR_THEME_COUNT = sizeof(loRaFishThemes) / sizeof(loRaFishThemes[0]);
static const char* loRaFishColorNames[] = {"Coral", "Tangerine", "Gold", "Mint", "Lagoon", "Orchid", "Mono"};
static const char* loRaFishMonoDisplayColourNames[] = {"Blue OLED", "White OLED", "Yellow/Blue OLED"};
static constexpr uint8_t LORAFISH_MONO_DISPLAY_COLOUR_COUNT = 3;
// Blue and yellow/blue SSD1306 panels emit cyan where the fish lives (bottom 3/4).
static constexpr uint16_t loRaFishMonoDisplayTints[] = {0x03B0, 0xFFFF, 0x03B0};
static uint16_t loRaFishMonoDisplayTint() {
  return loRaFishMonoDisplayTints[min(loRaFishMonoDisplayColour,
                                      (uint8_t)(LORAFISH_MONO_DISPLAY_COLOUR_COUNT - 1))];
}
static const ColorVal loRaFishColors[] = {0xF986, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0xF81F};
static constexpr uint8_t LORAFISH_FISH_COLOR_COUNT = sizeof(loRaFishColors) / sizeof(loRaFishColors[0]);
static constexpr uint8_t LORAFISH_COLOR_ALLELE_COUNT = LORAFISH_FISH_COLOR_COUNT + 1;

static uint8_t loRaFishThemeCount() {
  return loRaFishColorDisplay ? LORAFISH_COLOR_THEME_COUNT : 2;
}

static void applyLoRaFishTheme() {
  const LoRaFishThemePalette& theme=loRaFishThemes[loRaFishTheme % loRaFishThemeCount()];
  if (!loRaFishColorDisplay) {
    const bool light=(loRaFishTheme == 1);
    const ColorVal background=light ? 1 : 0;
    const ColorVal foreground=light ? 0 : 1;
    UIColor::window_bkg=background;
    UIColor::title_bkg=background;
    UIColor::title_txt=foreground;
    UIColor::primary_txt=foreground;
    UIColor::secondary_txt=foreground;
    UIColor::warning_txt=foreground;
    UIColor::popup_bkg=background;
    UIColor::popup_txt=foreground;
    UIColor::corp_blue=foreground;
    return;
  }
  UIColor::window_bkg=theme.background;
  UIColor::title_bkg=theme.titleBackground;
  UIColor::title_txt=theme.titleText;
  UIColor::primary_txt=theme.primaryText;
  UIColor::secondary_txt=theme.secondaryText;
  UIColor::warning_txt=theme.warningText;
  UIColor::popup_bkg=theme.popupBackground;
  UIColor::popup_txt=theme.popupText;
  UIColor::corp_blue=theme.accent;
}

static ColorVal loRaFishColorValue(uint8_t color) {
  if (color==0xFF) return 0xFFFF;
  if (color==LORAFISH_MONOCHROME_RENDER_TINT && loRaFishColorDisplay)
    return loragotchiDXGuest.monoPixelTint;
  return loRaFishColorDisplay && LoRaFishColour::valid(color) && color != 6
      ? LoRaFishColour::rgb565(color) : UIColor::primary_txt;
}

static LoRaFishAppearance loRaFishLocalAppearance() {
  return {loRaFishScaleColor,loRaFishColor,loRaFishEyeColor};
}
#ifdef LORAFISH_UI
static bool loRaFishAppearanceHasColor(const LoRaFishAppearance& appearance) {
  return appearance.scales != 6 || appearance.body != 6 || appearance.eyes != 6;
}
static void loRaFishSyncDXAppearance() {
  const LoRaFishAppearance appearance=loRaFishLocalAppearance();
  loragotchiDXLocalColorCapable=loRaFishColorDisplay;
  loragotchiDXLocalHasColorGenes=loRaFishAppearanceHasColor(appearance);
  loragotchiDXLocalColorIndex=appearance.body;
  loragotchiDXLocalScalesColorIndex=appearance.scales;
  loragotchiDXLocalEyesColorIndex=appearance.eyes;
  loragotchiDXLocalMonoPanel=loRaFishMonoDisplayColour;
  loragotchiDXLocalMonoPixelTint=loRaFishMonoDisplayTint();
}
#endif
static unsigned long loRaFishReturnShakeUntil = 0;

static LoRaFishAppearance loRaFishGuestAppearance() {
  if (!loragotchiDXGuest.colorCapable)
    return {LORAFISH_MONOCHROME_COLOR,LORAFISH_MONOCHROME_COLOR,LORAFISH_MONOCHROME_COLOR};
  return {loragotchiDXGuest.scalesColor,loragotchiDXGuest.color,loragotchiDXGuest.eyesColor};
}

static LoRaFishAppearance loRaFishGuestRenderAppearance() {
  if (loRaFishColorDisplay && !loragotchiDXGuest.colorCapable)
    return {LORAFISH_MONOCHROME_RENDER_TINT,LORAFISH_MONOCHROME_RENDER_TINT,
            LORAFISH_MONOCHROME_RENDER_TINT};
  return loRaFishGuestAppearance();
}

// Lifecycle presentation: egg for first 5 minutes, then normal fish stages.
// At 7 days the pet reaches the end of its life and shows a memorial screen.
static constexpr uint32_t LORAFISH_EGG_MINUTES = 5;
static constexpr uint32_t LORAFISH_LIFESPAN_MINUTES = 7UL*24UL*60UL;
static bool loRaFishHatchMelodyPlayed = false;
static unsigned long loRaFishEvolutionFlashUntil = 0;

// User-selectable screen timeout. 5 means always on.
static uint8_t loRaFishTimeoutAwakeSetting = 0;
static uint8_t loRaFishTimeoutAsleepSetting = 0;
static const uint32_t loRaFishTimeoutValues[] = {15000UL,30000UL,60000UL,120000UL,180000UL,0UL};
static const char* loRaFishTimeoutShortNames[] = {"15s","30s","1m","2m","3m","Always"};
static uint8_t loRaFishBrightnessSetting = 3;
static const uint8_t loRaFishBrightnessValues[] = {64,128,192,255};
static const char* loRaFishBrightnessNames[] = {"25%","50%","75%","100%"};
static uint8_t loRaFishAlwaysOnDimSetting = 0;
static const uint32_t loRaFishAlwaysOnDimValues[] = {15000UL,30000UL,60000UL};
static const char* loRaFishAlwaysOnDimNames[] = {"15s","30s","60s"};
static unsigned long loRaFishLastActivityMs = 0;
static bool loRaFishScreenDimmed = false;
static bool loRaFishBrightnessNeedsApply = false;
static uint8_t loRaFishBuzzerVolume = 4;
static uint8_t loRaFishWakeSound = 0;
static uint8_t loRaFishSleepSound = 0;
static uint8_t loRaFishMessageSound = 1;
static bool loRaFishSoundSleepStateKnown = false;
static bool loRaFishSoundWasSleeping = false;
static const char* loRaFishVolumeNames[] = {"Off","25%","50%","75%","100%"};
static const char* loRaFishWakeSoundNames[] = {"Off","Chirp","Rising","Alarm"};
static const char* loRaFishSleepSoundNames[] = {"Off","Drift","Soft bell"};
static const char* loRaFishMessageSoundNames[] = {"Off","Ping","Double","Arpeggio"};
static const char* loRaFishWakeMelodies[] = {
  "",
  "WakeChirp:d=16,o=6,b=165:c,e,g",
  "WakeRise:d=16,o=6,b=180:g,b,d7,g7",
  "WakeAlarm:d=8,o=6,b=140:d7,a6,d7,a6",
};
static const char* loRaFishSleepMelodies[] = {
  "",
  "SleepDrift:d=8,o=5,b=100:g,e,c4",
  "SleepBell:d=16,o=5,b=110:e,g,c6",
};
static const char* loRaFishMessageMelodies[] = {
  "",
  "MessagePing:d=16,o=6,b=170:e6",
  "MessageDouble:d=16,o=6,b=170:e6,g6",
  "MessageArpeggio:d=16,o=6,b=165:c6,e6,g6",
};
static uint8_t loRaFishSleepHour = 21;
static uint8_t loRaFishWakeHour = 7;
static int8_t loRaFishUtcOffsetQuarters = 0;
static bool loRaFishIsSleeping();
static bool loRaFishTimeoutSleepState = false;
static bool loRaFishTimeoutSleepStateKnown = false;
static uint32_t loRaFishScreenTimeoutMs() {
  const uint8_t setting=loRaFishIsSleeping() ? loRaFishTimeoutAsleepSetting : loRaFishTimeoutAwakeSetting;
  return loRaFishTimeoutValues[min((uint8_t)5,setting)];
}
static uint8_t loRaFishDisplayOptionCount() {
  return 5;
}
static bool loRaFishCanBreed() {
#ifdef LORAFISH_UI
  return !loRaFishPendingEgg && !loragotchiDXHome.away &&
      !loragotchiDXHome.pending && !loragotchiDXHome.committing &&
      fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES &&
      loragotchiDXGuest.present && loragotchiDXGuestReturn.status == LoRaFishGuestReturn::Idle &&
      fish.evolution>=2 && loragotchiDXGuest.stage>=2;
#else
  return false;
#endif
}
static constexpr bool loRaFishHasSound =
#ifdef PIN_BUZZER
    true;
#else
    false;
#endif
static uint8_t loRaFishOptionCount() {
  return LoRaFishOptions::count(loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present);
}
static void returnToLoRaFishOptions(LoRaFishOption option) {
  loRaFishPage = 5;
  loRaFishOption = LoRaFishOptions::available(option, loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present)
      ? option : LoRaFishOption::Advert;
}
static void cancelLoRaFishValue() {
  loRaFishValuePicker.cancel();
  // Standalone pickers return to Display; field pickers return to their field list.
  if (loRaFishPage == 8 || loRaFishPage == 13 || loRaFishPage == 15) loRaFishPage = 12;
}
static void openLoRaFishValue(LoRaFishSetting setting) {
  uint8_t value = 0, count = 0;
  switch (setting) {
    case LoRaFishSetting::Theme: value = loRaFishTheme; count = loRaFishThemeCount(); break;
    case LoRaFishSetting::Brightness: value = loRaFishBrightnessSetting; count = 4; break;
    case LoRaFishSetting::Rendering: value = loRaFishSimulationMode; count = 2; break;
    case LoRaFishSetting::Scales: value = LoRaFishColour::fallback(loRaFishScaleColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::Body: value = LoRaFishColour::fallback(loRaFishColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::Eyes: value = LoRaFishColour::fallback(loRaFishEyeColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::SleepHour: value = loRaFishSleepHour; count = 24; break;
    case LoRaFishSetting::WakeHour: value = loRaFishWakeHour; count = 24; break;
    case LoRaFishSetting::UtcOffset: value = loRaFishUtcOffsetQuarters + 48; count = 105; break;
    case LoRaFishSetting::TimeoutAwake: value = loRaFishTimeoutAwakeSetting; count = 6; break;
    case LoRaFishSetting::TimeoutAsleep: value = loRaFishTimeoutAsleepSetting; count = 6; break;
    case LoRaFishSetting::DimAfter: value = loRaFishAlwaysOnDimSetting; count = 3; break;
    case LoRaFishSetting::Volume: value = loRaFishBuzzerVolume; count = 5; break;
    case LoRaFishSetting::WakeSound: value = loRaFishWakeSound; count = 4; break;
    case LoRaFishSetting::SleepSound: value = loRaFishSleepSound; count = 3; break;
    case LoRaFishSetting::MessageSound: value = loRaFishMessageSound; count = 4; break;
    case LoRaFishSetting::MonoDisplayColour: value = loRaFishMonoDisplayColour; count = LORAFISH_MONO_DISPLAY_COLOUR_COUNT; break;
  }
  loRaFishValuePicker.open(setting, value, count);
}
static void selectLoRaFishValue() {
  uint8_t value;
  if (!loRaFishValuePicker.select(value)) return;
  tickLoRaFish();
  switch (loRaFishValuePicker.key()) {
    case LoRaFishSetting::Theme: loRaFishTheme = value; applyLoRaFishTheme(); break;
    case LoRaFishSetting::Brightness:
      loRaFishBrightnessSetting = value; loRaFishBrightnessNeedsApply = true; break;
    case LoRaFishSetting::Rendering: loRaFishSimulationMode = value; break;
    case LoRaFishSetting::Scales: loRaFishScaleColor = value; break;
    case LoRaFishSetting::Body: loRaFishColor = value; break;
    case LoRaFishSetting::Eyes: loRaFishEyeColor = value; break;
    case LoRaFishSetting::SleepHour: loRaFishSleepHour = value; break;
    case LoRaFishSetting::WakeHour: loRaFishWakeHour = value; break;
    case LoRaFishSetting::UtcOffset: loRaFishUtcOffsetQuarters = int(value) - 48; break;
    case LoRaFishSetting::TimeoutAwake: loRaFishTimeoutAwakeSetting = value; break;
    case LoRaFishSetting::TimeoutAsleep: loRaFishTimeoutAsleepSetting = value; break;
    case LoRaFishSetting::DimAfter: loRaFishAlwaysOnDimSetting = value; break;
    case LoRaFishSetting::Volume: loRaFishBuzzerVolume = value; break;
    case LoRaFishSetting::WakeSound: loRaFishWakeSound = value; break;
    case LoRaFishSetting::SleepSound: loRaFishSleepSound = value; break;
    case LoRaFishSetting::MessageSound: loRaFishMessageSound = value; break;
    case LoRaFishSetting::MonoDisplayColour: loRaFishMonoDisplayColour = value; loRaFishMonoDisplayColourKnown = true; break;
  }
#ifdef LORAFISH_UI
  loRaFishSyncDXAppearance();
#endif
  saveLoRaFishSettings();
  cancelLoRaFishValue();
}
static void applyLoRaFishBrightness(DisplayDriver& display, bool dimmed=false) {
  uint8_t brightness=loRaFishBrightnessValues[min((uint8_t)3,loRaFishBrightnessSetting)];
  if (dimmed) brightness=brightness/4 < 12 ? 12 : brightness/4;
  display.setBrightness(brightness);
}
static void loRaFishRegisterDisplayActivity(DisplayDriver& display) {
  loRaFishLastActivityMs=millis();
  loRaFishScreenDimmed=false;
  applyLoRaFishBrightness(display);
}
static unsigned long loRaFishBootMs = 0;

#define LORAFISH_TIMEOUT_MS() loRaFishScreenTimeoutMs()
// Tamagotchi-style automatic feeding and sleep cycle.
static bool loRaFishFeeding = false;
static bool loRaFishFeedChasing = false;
static unsigned long loRaFishFeedStarted = 0;
static int loRaFishFeedStartX = 22;
static int loRaFishFeedStartY = 35;
static int loRaFishFeedLastX = 0;
static bool loRaFishFeedLastXValid = false;
static constexpr unsigned long LORAFISH_FEED_DROP_MS = 1300UL;
static constexpr unsigned long LORAFISH_FEED_NOTICE_MS = 250UL;
static constexpr unsigned long LORAFISH_FEED_RUSH_MS = 350UL;
static constexpr unsigned long LORAFISH_FEED_EAT_MS = 150UL;
static constexpr unsigned long LORAFISH_FEED_RETURN_MS = 300UL;
static constexpr unsigned long LORAFISH_FEED_DONE_MS =
  LORAFISH_FEED_DROP_MS + LORAFISH_FEED_EAT_MS + LORAFISH_FEED_RETURN_MS;
static constexpr uint32_t LORAFISH_PACKETS_PER_FEED = 10;

static void loRaFishNaturalPositionForStage(int w, uint8_t stage, unsigned long phaseOffset,
                                             int& x, int& y, bool& right) {
  // Keep every fish inside the actual aquarium area.  Visitors use the same
  // smooth triangular swim path as the home fish, just phase-shifted so two
  // fish do not sit on top of each other.
  unsigned long swimPeriod=8000UL;
  if(stage==0) swimPeriod=5200UL;
  else if(stage==1) swimPeriod=6500UL;
  else if(stage>=3) swimPeriod=11000UL;

  unsigned long t=(millis()+phaseOffset)%swimPeriod;
  float phase=t/(float)swimPeriod;
  right=phase<0.5f;

  const int margin=20;                 // sprite-safe left/right tank margin
  const int span=max(1,w-(margin*2));
  x=right ? margin+(int)(span*phase*2.0f)
          : margin+(int)(span*(1.0f-phase)*2.0f);
  y=38+(int)(2.0f*sinf((millis()+phaseOffset)/650.0f));
}

static void loRaFishNaturalPosition(int w, int& x, int& y, bool& right) {
  loRaFishNaturalPositionForStage(w,fish.evolution,0,x,y,right);
}

// Gameplay and presentation are deliberately separate:
// a meal is credited immediately when RF earns it; this animation is optional.
static bool creditLoRaFishMeal(bool animate) {
  const bool eligible = !loRaFishNeedsAppearanceSetup &&
      fish.ageMinutes >= LORAFISH_EGG_MINUTES &&
      fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES && !loRaFishIsSleeping();
  if (!loRaFishHappiness.meal(eligible, fish.happiness)) return false;
  scheduleLoRaFishPoop();
  saveLoRaFish();

  if (animate && !loRaFishFeeding) {
    loRaFishFeeding = true;
    loRaFishFeedChasing = false;
    loRaFishFeedStarted = millis();
  }
  return true;
}
static uint32_t loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;

static bool consumeLoRaFishMeals(bool animate) {
  bool fed = false;
  while (fish.rfEvents >= loRaFishNextAutoFeed) {
    if (creditLoRaFishMeal(animate)) fed = true;
    // Sleeping, egg/memorial and cooldown thresholds are consumed, not queued.
    loRaFishNextAutoFeed += LORAFISH_PACKETS_PER_FEED;
  }
  return fed;
}

// Tamagotchi-style tank hygiene.
static uint8_t loRaFishPoopCount = 0;
static unsigned long loRaFishNextPoopAt = 0;
static bool loRaFishCleaning = false;
static unsigned long loRaFishCleaningStarted = 0;
static constexpr uint8_t LORAFISH_MAX_POOP = 3;
static constexpr unsigned long LORAFISH_CLEAN_MS = 1400UL;

static void scheduleLoRaFishPoop() {
  // 30-90 seconds after a meal. A pending poop is not rescheduled by another meal.
  if (loRaFishNextPoopAt == 0 && loRaFishPoopCount < LORAFISH_MAX_POOP)
    loRaFishNextPoopAt = millis() + 30000UL + (random(60001L));
}

static bool tickLoRaFishPoop() {
  bool cleaned = false;
  if (loRaFishNextPoopAt && (long)(millis() - loRaFishNextPoopAt) >= 0) {
    if (loRaFishPoopCount < LORAFISH_MAX_POOP) loRaFishPoopCount++;
    loRaFishNextPoopAt = 0;
    saveLoRaFishSettings();
  }
  if (loRaFishCleaning && millis() - loRaFishCleaningStarted >= LORAFISH_CLEAN_MS) {
    loRaFishCleaning = false;
    loRaFishPoopCount = 0;
    loRaFishNextPoopAt = 0;
    fish.happiness = min((int)100, (int)fish.happiness + 3);
    saveLoRaFishSettings();
    saveLoRaFish();
    cleaned = true;
  }
  return cleaned;
}

static void startLoRaFishCleaning() {
  if (!loRaFishCleaning && loRaFishPoopCount > 0) {
    loRaFishCleaning = true;
    loRaFishCleaningStarted = millis();
  }
}

static void startLoRaFishFeeding() {
  // Presentation only. Meal state is credited by creditLoRaFishMeal().
  if (!loRaFishFeeding) {
    loRaFishFeeding = true;
    loRaFishFeedChasing = false;
    loRaFishFeedLastXValid = false;
    loRaFishFeedStarted = millis();
  }
}

static void resetLoRaFishPet() {
  // Recreate the initial persisted pet state.
  loRaFishAdoptionEligible = true;
  fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
  loRaFishHappiness.resetCare();
  loRaFishHappyLastMs = millis();
  fishReaction = 0;
  fishReactionUntil = 0;
  loRaFishFeeding = false;
  loRaFishFeedChasing = false;
  loRaFishFeedLastXValid = false;
  loRaFishFeedStarted = 0;
  loRaFishEvolutionFlashUntil = 0;
  loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;
  loRaFishPoopCount = 0;
  loRaFishNextPoopAt = 0;
  loRaFishCleaning = false;
  loRaFishGeneration = 1;
  loRaFishPendingEgg = false;
  loRaFishMemorialEggSide = 0;
  loRaFishNeedsAppearanceSetup=loRaFishColorDisplay;
  if (!loRaFishColorDisplay) {
    loRaFishColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishScaleColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishEyeColor=LORAFISH_MONOCHROME_COLOR;
  }
#ifdef LORAFISH_UI
  loRaFishSyncDXAppearance();
#endif
  if (loRaFishNeedsAppearanceSetup) {
    loRaFishColorField=0;
    loRaFishPage=9;
  }
  saveLoRaFishSettings();
  fishLastMinute = millis();
  fishLastSave = millis();
  saveLoRaFish();
}

static uint8_t inheritLoRaFishColor(uint8_t localColor,uint8_t visitorColor) {
  return LoRaFishColour::inherit(localColor, visitorColor, (unsigned)random(0,241));
}

static bool breedLoRaFishWithGuest() {
  if (!loRaFishCanBreed()) return false;
  const LoRaFishAppearance local=loRaFishLocalAppearance();
  const LoRaFishAppearance visitor=loRaFishGuestAppearance();
  loRaFishEggAppearance.scales=inheritLoRaFishColor(local.scales,visitor.scales);
  loRaFishEggAppearance.body=inheritLoRaFishColor(local.body,visitor.body);
  loRaFishEggAppearance.eyes=inheritLoRaFishColor(local.eyes,visitor.eyes);
  loRaFishPendingEgg=true;
  loRaFishMemorialEggSide=0;
  saveLoRaFishSettings();
  return true;
}

// LoRaFish uses MeshCore's RTC as its source of truth. If MeshCore has not
// acquired a plausible epoch yet, retain the old build-time + uptime fallback.
static uint32_t buildSecondsOfDay() {
  int hh=0, mm=0, ss=0;
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  return (uint32_t)hh*3600UL + (uint32_t)mm*60UL + (uint32_t)ss;
}

static bool loRaFishMeshTimeValid(uint32_t epoch) {
  // 2024-01-01 UTC. Avoid treating an unset/near-zero RTC as real time.
  return epoch >= 1704067200UL;
}

static int64_t loRaFishLocalEpoch() {
  const uint32_t epoch = loRaFishRTC ? loRaFishRTC->getCurrentTime() : 0;
  const int64_t baseEpoch = loRaFishMeshTimeValid(epoch) ? epoch
      : (int64_t)DateTime(__DATE__,__TIME__).unixtime() + millis()/1000UL;
  const int64_t localEpoch = baseEpoch + (int32_t)loRaFishUtcOffsetQuarters*900L;
  return localEpoch < 0 ? 0 : localEpoch;
}

static bool loRaFishClock(char* timeBuf, size_t timeLen,
                          char* dateBuf, size_t dateLen) {
  const bool meshTimeValid = loRaFishMeshTimeValid(loRaFishRTC ? loRaFishRTC->getCurrentTime() : 0);
  const int64_t localEpoch = loRaFishLocalEpoch();
  DateTime dt((uint32_t)localEpoch);
  static const char* mon[]={"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(timeBuf,timeLen,"%02u:%02u",dt.hour(),dt.minute());
  snprintf(dateBuf,dateLen,"%02u %s %04u",dt.day(),mon[dt.month()-1],dt.year());
  return meshTimeValid;
}

static void loRaFishFormatUtcOffset(char* text,size_t textLen,bool includeUtc=true) {
  const int minutes=(int)loRaFishUtcOffsetQuarters*15;
  const char sign=minutes<0 ? '-' : '+';
  const int absoluteMinutes=abs(minutes);
  if (includeUtc)
    snprintf(text,textLen,"UTC%c%02d:%02d",sign,absoluteMinutes/60,absoluteMinutes%60);
  else
    snprintf(text,textLen,"%c%02d:%02d",sign,absoluteMinutes/60,absoluteMinutes%60);
}

static uint8_t fishEvolutionForAge(uint32_t mins) {
  // evolution still describes the fish after hatching; egg is age-driven.
  if (mins < 60) return 0;             // Fry
  if (mins < 6UL*60UL) return 1;       // Young
  if (mins < 3UL*24UL*60UL) return 2;  // Adult
  return 3;                            // Elder
}

static const char* fishStageName() {
  switch (fish.evolution) {
    case 0: return "FRY";
    case 1: return "YOUNG";
    case 2: return "ADULT";
    default: return "ELDER";
  }
}

// Preserve the 24-byte settings prefix and 12-byte HAP1 tail. AQU1 is an
// optional eight-byte extension. HUE1 appends ten bytes of finer colour genes;
// legacy readers keep the nearest-palette values in the original prefix.
static constexpr uint32_t LORAFISH_AQUARIUM_SETTINGS_MAGIC = 0x31555141UL; // "AQU1"
static constexpr size_t LORAFISH_SETTINGS_SIZE = 60;
static constexpr uint32_t LORAFISH_CHECKPOINT_MS = 5UL * 60000UL;
static constexpr uint32_t LORAFISH_SAVE_RETRY_MS = 5000UL;
static uint8_t lastWrittenLoRaFishSettings[LORAFISH_SETTINGS_SIZE];
static bool haveLastWrittenLoRaFishSettings = false;
static bool loRaFishSettingsSaveFailed = false;
static bool loRaFishSaveFailed = false;
static uint32_t loRaFishSettingsLastAttempt = 0;
static uint32_t loRaFishLastAttempt = 0;

static void encodeLoRaFishSettings(uint8_t* data) {
  const uint8_t prefix[] = {
      loRaFishTimeoutAwakeSetting, loRaFishPoopCount, loRaFishTheme, LoRaFishColour::fallback(loRaFishColor),
      loRaFishSleepHour, loRaFishWakeHour, (uint8_t)loRaFishUtcOffsetQuarters,
      loRaFishTimeoutAsleepSetting, loRaFishBrightnessSetting, loRaFishAlwaysOnDimSetting,
      loRaFishBuzzerVolume, loRaFishWakeSound, loRaFishSleepSound, loRaFishMessageSound,
      LoRaFishColour::fallback(loRaFishScaleColor), LoRaFishColour::fallback(loRaFishEyeColor)};
  memcpy(data, prefix, sizeof(prefix));
  memcpy(data + 16, &loRaFishGeneration, 2);
  data[18] = loRaFishPendingEgg ? 1 : 0;
  data[19] = LoRaFishColour::fallback(loRaFishEggAppearance.scales);
  data[20] = LoRaFishColour::fallback(loRaFishEggAppearance.body);
  data[21] = LoRaFishColour::fallback(loRaFishEggAppearance.eyes);
  data[22] = loRaFishNeedsAppearanceSetup ? 1 : 0;
  data[23] = loRaFishSimulationMode ? 1 : 0;
  memcpy(data + 24, &LORAFISH_HAPPY_SETTINGS_MAGIC, 4);
  memcpy(data + 28, &loRaFishHappiness.awakeMs, 4);
  memcpy(data + 32, &loRaFishHappiness.mealCooldownMs, 4);
  memcpy(data + 36, &LORAFISH_AQUARIUM_SETTINGS_MAGIC, 4);
  memcpy(data + 40, &loRaFishHappiness.aquariumCooldownMs, 4);
  memcpy(data + 44, "HUE1", 4);
  data[48]=loRaFishColor; data[49]=loRaFishScaleColor; data[50]=loRaFishEyeColor;
  data[51]=loRaFishEggAppearance.body; data[52]=loRaFishEggAppearance.scales;
  data[53]=loRaFishEggAppearance.eyes;
  memcpy(data + 54, "MON1", 4);
  data[58]=loRaFishMonoDisplayColour;
  data[59]=0;
}

static const char* loRaFishRecordKey(const char* path) {
  if (!strcmp(path,"/lorafish.cfg")) return "cfg";
  if (!strcmp(path,"/lfegg0.dat")) return "egg0";
  if (!strcmp(path,"/lfegg1.dat")) return "egg1";
  return "state";
}

static bool writeLoRaFishRecord(const char* path, const uint8_t* data, size_t size) {
#if defined(LORAFISH_TDECK_NVS) && defined(ESP32)
  LoRaFishNVSFile f(loRaFishRecordKey(path), true);
#elif defined(ESP32)
  File f = SPIFFS.open(path, FILE_WRITE);
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  // Adafruit FILE_O_WRITE appends: retain replacement, not append-to-old-record.
  if (InternalFS.exists(path) && !InternalFS.remove(path)) return false;
  File f = InternalFS.open(path, FILE_O_WRITE);
#else
  return false;
#endif
#if defined(ESP32) || defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (!f) return false;
  bool complete = f.write(data, size) == size;
#if defined(LORAFISH_TDECK_NVS) && defined(ESP32)
  complete = f.close() && complete;
#else
  f.close(); // These APIs return void: byte-count success is not power-loss atomicity.
#endif
  return complete;
#endif
}

static void saveLoRaFishSettings() {
  const uint32_t now = millis();
  if (loRaFishSettingsSaveFailed &&
      now - loRaFishSettingsLastAttempt < LORAFISH_SAVE_RETRY_MS) return;
  uint8_t data[LORAFISH_SETTINGS_SIZE];
  encodeLoRaFishSettings(data);
  if (!loRaFishSettingsSaveFailed && haveLastWrittenLoRaFishSettings &&
      memcmp(data, lastWrittenLoRaFishSettings, sizeof(data)) == 0) {
    loRaFishHappyLastSavedMs = now;
    return;
  }
  loRaFishSettingsLastAttempt = now;
  if (!writeLoRaFishRecord("/lorafish.cfg", data, sizeof(data))) {
    loRaFishSettingsSaveFailed = true;
    return;
  }
  memcpy(lastWrittenLoRaFishSettings, data, sizeof(data));
  haveLastWrittenLoRaFishSettings = true;
  loRaFishSettingsSaveFailed = false;
  lastWrittenLoRaFishHappiness = loRaFishHappiness;
  haveLastWrittenLoRaFishHappiness = true;
  loRaFishHappyLastSavedMs = now;
}

static void loadLoRaFishSettings() {
  haveLastWrittenLoRaFishSettings = false;
  loRaFishSettingsSaveFailed = false;
  loRaFishHappiness = {};
  haveLastWrittenLoRaFishHappiness = false;
  loRaFishHappyLastMs = loRaFishHappyLastSavedMs = millis();
#if defined(LORAFISH_TDECK_NVS) && defined(ESP32)
  LoRaFishNVSFile f("cfg", false);
#elif defined(ESP32)
  File f = SPIFFS.open("/lorafish.cfg", FILE_READ);
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = InternalFS.open("/lorafish.cfg", FILE_O_READ);
#else
  File f;
#endif
#if defined(ESP32) || defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (f) {
    uint8_t timeout=0;
    const bool hasTimeout=f.read(&timeout,1)==1;
    uint8_t poop=0;
    if (f.read(&poop,1)==1 && poop<=LORAFISH_MAX_POOP) loRaFishPoopCount=poop;
    uint8_t theme=0;
    if (f.read(&theme,1)==1 && theme<loRaFishThemeCount()) loRaFishTheme=theme;
    uint8_t color=0;
    if (f.read(&color,1)==1 && color<=LORAFISH_MONOCHROME_COLOR) loRaFishColor=color;
    loRaFishScaleColor=loRaFishColor;
    loRaFishEyeColor=loRaFishColor;
    uint8_t sleepHour=0;
    if (f.read(&sleepHour,1)==1 && sleepHour<24) loRaFishSleepHour=sleepHour;
    uint8_t wakeHour=0;
    if (f.read(&wakeHour,1)==1 && wakeHour<24) loRaFishWakeHour=wakeHour;
    int8_t utcOffset=0;
    if (f.read((uint8_t*)&utcOffset,1)==1 && utcOffset>=-48 && utcOffset<=56)
      loRaFishUtcOffsetQuarters=utcOffset;
    uint8_t asleepTimeout=0;
    if (f.read(&asleepTimeout,1)==1) {
      if (timeout<=5) loRaFishTimeoutAwakeSetting=timeout;
      if (asleepTimeout<=5) loRaFishTimeoutAsleepSetting=asleepTimeout;
    } else if (hasTimeout && timeout<=3) {
      loRaFishTimeoutAwakeSetting=timeout==3 ? 5 : timeout;
      loRaFishTimeoutAsleepSetting=loRaFishTimeoutAwakeSetting;
    }
    uint8_t brightness=0;
    if (f.read(&brightness,1)==1 && brightness<4) loRaFishBrightnessSetting=brightness;
    uint8_t dimSetting=0;
    if (f.read(&dimSetting,1)==1 && dimSetting<3) loRaFishAlwaysOnDimSetting=dimSetting;
    uint8_t volume=0;
    if (f.read(&volume,1)==1 && volume<5) loRaFishBuzzerVolume=volume;
    uint8_t wakeSound=0;
    if (f.read(&wakeSound,1)==1 && wakeSound<4) loRaFishWakeSound=wakeSound;
    uint8_t sleepSound=0;
    if (f.read(&sleepSound,1)==1 && sleepSound<3) loRaFishSleepSound=sleepSound;
    uint8_t messageSound=0;
    if (f.read(&messageSound,1)==1 && messageSound<4) loRaFishMessageSound=messageSound;
    uint8_t traitColor=loRaFishColor;
    const bool hasScaleTrait=f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR;
    if (hasScaleTrait) loRaFishScaleColor=traitColor;
    traitColor=loRaFishColor;
    const bool hasEyeTrait=f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR;
    if (hasEyeTrait) loRaFishEyeColor=traitColor;
    loRaFishAppearanceLoaded=hasScaleTrait && hasEyeTrait;
    if (!hasScaleTrait || !hasEyeTrait) {
      if (!loRaFishColorDisplay) {
        loRaFishColor=LORAFISH_MONOCHROME_COLOR;
        loRaFishScaleColor=LORAFISH_MONOCHROME_COLOR;
        loRaFishEyeColor=LORAFISH_MONOCHROME_COLOR;
      } else {
        loRaFishScaleColor=loRaFishColor;
        loRaFishEyeColor=loRaFishColor;
      }
    }
    uint16_t generation=1;
    if (f.read((uint8_t*)&generation,sizeof(generation))==sizeof(generation) && generation>0)
      loRaFishGeneration=generation;
    uint8_t hasEgg=0;
    if (f.read(&hasEgg,1)==1) loRaFishPendingEgg=hasEgg!=0;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.scales=traitColor;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.body=traitColor;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.eyes=traitColor;
    uint8_t appearanceSetup=0;
    if (f.read(&appearanceSetup,1)==1) loRaFishNeedsAppearanceSetup=appearanceSetup!=0;
    uint8_t simulationMode=0;
    if (f.read(&simulationMode,1)==1) loRaFishSimulationMode=simulationMode!=0;
    uint32_t happyMagic = 0;
    LoRaFishHappiness happy;
    const bool hasHappyMagic = f.read((uint8_t*)&happyMagic, sizeof(happyMagic)) == sizeof(happyMagic);
    const bool hasAwakeMs = f.read((uint8_t*)&happy.awakeMs, sizeof(happy.awakeMs)) == sizeof(happy.awakeMs);
    const bool hasMealMs = f.read((uint8_t*)&happy.mealCooldownMs, sizeof(happy.mealCooldownMs)) == sizeof(happy.mealCooldownMs);
    if (hasHappyMagic && hasAwakeMs && hasMealMs &&
        happyMagic == LORAFISH_HAPPY_SETTINGS_MAGIC &&
        happy.valid()) {
      loRaFishHappiness = happy;
      lastWrittenLoRaFishHappiness = happy;
      haveLastWrittenLoRaFishHappiness = true;
    }
    uint32_t aquariumMagic = 0, aquariumCooldown = 0;
    if (f.size() > 36) loRaFishHappiness.aquariumCooldownMs = LoRaFishHappiness::aquariumMs;
    if (f.read((uint8_t*)&aquariumMagic, sizeof(aquariumMagic)) == sizeof(aquariumMagic) &&
        aquariumMagic == LORAFISH_AQUARIUM_SETTINGS_MAGIC) {
      // A recognized but damaged new tail must not shorten the reward cap.
      loRaFishHappiness.aquariumCooldownMs = LoRaFishHappiness::aquariumMs;
      if (f.read((uint8_t*)&aquariumCooldown, sizeof(aquariumCooldown)) == sizeof(aquariumCooldown) &&
          aquariumCooldown <= LoRaFishHappiness::aquariumMs)
        loRaFishHappiness.aquariumCooldownMs = aquariumCooldown;
    }
    uint8_t hues[10] = {};
    if (f.read(hues, sizeof(hues)) == sizeof(hues) && !memcmp(hues, "HUE1", 4)) {
      bool valid = true;
      for (unsigned i=4; i<10; ++i) valid = valid && LoRaFishColour::valid(hues[i]);
      if (valid) {
        loRaFishColor=hues[4]; loRaFishScaleColor=hues[5]; loRaFishEyeColor=hues[6];
        loRaFishEggAppearance={hues[8],hues[7],hues[9]};
      }
    }
    uint8_t mono[6] = {};
    if (f.read(mono, sizeof(mono)) == sizeof(mono) && !memcmp(mono, "MON1", 4) &&
        mono[4] < LORAFISH_MONO_DISPLAY_COLOUR_COUNT) {
      loRaFishMonoDisplayColour = mono[4];
      loRaFishMonoDisplayColourKnown = true;
    }
    lastWrittenLoRaFishHappiness = loRaFishHappiness;
    uint8_t canonical[LORAFISH_SETTINGS_SIZE];
    encodeLoRaFishSettings(canonical);
#if !defined(LORAFISH_TDECK_NVS)
    if (f.size() == sizeof(canonical) && f.seek(0) &&
        f.read(lastWrittenLoRaFishSettings, sizeof(canonical)) == sizeof(canonical)) {
      haveLastWrittenLoRaFishSettings =
          memcmp(canonical, lastWrittenLoRaFishSettings, sizeof(canonical)) == 0;
    }
#endif
    f.close();
  }
#endif
}

static bool sameLoRaFishRecord(const LoRaFishState& a, const LoRaFishState& b) {
  return a.magic == b.magic && a.ageMinutes == b.ageMinutes &&
         a.rfEvents == b.rfEvents && a.messages == b.messages &&
         a.txAcks == b.txAcks && a.happiness == b.happiness &&
         a.evolution == b.evolution && a.dxVisits == b.dxVisits &&
         a.longestUptimeMinutes == b.longestUptimeMinutes;
}

static LoRaFishState lastWrittenLoRaFish;
static bool haveLastWrittenLoRaFish = false;

static void saveLoRaFish() {
  // Care/transitions call this immediately; routine counters use a fixed maximum
  // checkpoint age. Only elapsed Happy timing waits for the five-minute checkpoint.
  const bool timingChanged = !loRaFishHappiness.same(lastWrittenLoRaFishHappiness);
  if (!haveLastWrittenLoRaFishHappiness || (timingChanged &&
      (loRaFishHappiness.mealCooldownMs > lastWrittenLoRaFishHappiness.mealCooldownMs ||
       loRaFishHappiness.aquariumCooldownMs > lastWrittenLoRaFishHappiness.aquariumCooldownMs ||
       loRaFishHappiness.awakeMs < lastWrittenLoRaFishHappiness.awakeMs ||
       (uint32_t)(millis() - loRaFishHappyLastSavedMs) >= 5UL*60000UL)))
    saveLoRaFishSettings();
  const uint32_t now = millis();
  if (loRaFishSaveFailed && now - loRaFishLastAttempt < LORAFISH_SAVE_RETRY_MS) return;
  if (!loRaFishSaveFailed && haveLastWrittenLoRaFish &&
      sameLoRaFishRecord(lastWrittenLoRaFish, fish)) {
    fishLastSave = now;
    return;
  }
  loRaFishLastAttempt = now;
  if (!writeLoRaFishRecord("/lorafish.dat", (const uint8_t*)&fish, sizeof(fish))) {
    loRaFishSaveFailed = true;
    return;
  }
  loRaFishSaveFailed = false;
  lastWrittenLoRaFish = fish;
  haveLastWrittenLoRaFish = true;
  fishLastSave = now;
}

static void loadLoRaFish() {
  haveLastWrittenLoRaFish = false;
  loRaFishSaveFailed = false;
  bool restored=false;
#if defined(LORAFISH_TDECK_NVS) && defined(ESP32)
  LoRaFishNVSFile f("state", false);
#elif defined(ESP32)
  File f = SPIFFS.open("/lorafish.dat", FILE_READ);
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  File f = InternalFS.open("/lorafish.dat", FILE_O_READ);
#else
  File f;
#endif
#if defined(ESP32) || defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (f && f.size() == sizeof(LoRaFishStateV1)) {
    LoRaFishStateV1 old;
    if (f.read((uint8_t*)&old, sizeof(old)) == sizeof(old) &&
        old.magic == LORAFISH_MAGIC_V1) {
      fish = {LORAFISH_MAGIC, old.ageMinutes, old.rfEvents, old.messages,
              old.txAcks, old.happiness, old.evolution, 0, 0};
      restored=true;
      Serial.println("[LFG2] migrated LFM1 fish record");
    }
    f.close();
  } else if (f && f.size() == sizeof(fish)) {
    LoRaFishState tmp;
    if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) &&
        tmp.magic == LORAFISH_MAGIC) {
      fish = tmp;
      lastWrittenLoRaFish = tmp;
      haveLastWrittenLoRaFish = true;
      restored=true;
    }
    f.close();
  }
#endif
  loRaFishAdoptionEligible=!restored;
  loRaFishNeedsAppearanceSetup=!restored && loRaFishColorDisplay;
  if (loRaFishNeedsAppearanceSetup) {
    loRaFishPage=9;
    loRaFishColorField=0;
  }
  fish.evolution = fishEvolutionForAge(fish.ageMinutes);
  Serial.printf("[LFG2] loaded restored=%d age=%lu visits=%lu longestUp=%lu\n",
                restored ? 1 : 0,(unsigned long)fish.ageMinutes,
                (unsigned long)fish.dxVisits,(unsigned long)fish.longestUptimeMinutes);
  fishLastMinute = millis();
  fishLastSave = millis();
  loRaFishNextAutoFeed =
      ((fish.rfEvents / LORAFISH_PACKETS_PER_FEED) + 1) * LORAFISH_PACKETS_PER_FEED;
}

#include "LoRaFishAdoption.h"

static void ensureLoRaFishOffspring() {
  if (fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES || loRaFishPendingEgg) return;
  // Self-fertilise only when no visitor-bred egg exists. With a single parent,
  // all three inherited traits are its own, including Mono.
  loRaFishEggAppearance=loRaFishLocalAppearance();
  loRaFishPendingEgg=true;
  loRaFishMemorialEggSide=0;
  saveLoRaFishSettings();
}

static void hatchLoRaFishOffspring() {
  if (!loRaFishPendingEgg) return;
  // DX visits and uptime records belong to the device, so they survive a hatch.
  const uint32_t keepVisits = fish.dxVisits;
  const uint32_t keepUptime = fish.longestUptimeMinutes;
  fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0, keepVisits, keepUptime};
  loRaFishHappiness.resetCare();
  loRaFishHappyLastMs = millis();
  loRaFishColor=loRaFishEggAppearance.body;
  loRaFishScaleColor=loRaFishEggAppearance.scales;
  loRaFishEyeColor=loRaFishEggAppearance.eyes;
  if (loRaFishGeneration<65535) loRaFishGeneration++;
  loRaFishPendingEgg=false;
  loRaFishMemorialEggSide=0;
  loRaFishPoopCount=0;
  loRaFishNextPoopAt=0;
  loRaFishCleaning=false;
  loRaFishFeeding=false;
  loRaFishFeedChasing=false;
  loRaFishFeedLastXValid=false;
  loRaFishEvolutionFlashUntil=0;
  loRaFishHatchMelodyPlayed=false;
  loRaFishNextAutoFeed=LORAFISH_PACKETS_PER_FEED;
  fishLastMinute=millis();
  fishLastSave=millis();
#ifdef LORAFISH_UI
  loRaFishSyncDXAppearance();
#endif
  saveLoRaFishSettings();
  saveLoRaFish();
}

static void advanceLoRaFishHappiness(uint32_t until) {
  const uint32_t elapsed = until - loRaFishHappyLastMs;
  if (!elapsed) return;
  const uint32_t now = millis();
  const uint32_t localNowMs = (uint32_t)(loRaFishLocalEpoch() % 86400) * 1000UL + now % 1000UL;
  const uint32_t behind = now - until;
  const uint32_t localEndMs = (localNowMs + LoRaFishHappiness::dayMs -
      behind % LoRaFishHappiness::dayMs) % LoRaFishHappiness::dayMs;
  const bool living = !loRaFishNeedsAppearanceSetup &&
      fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES;
  loRaFishHappiness.advance(elapsed, living, localEndMs, loRaFishSleepHour,
                           loRaFishWakeHour, loRaFishPoopCount, fish.happiness);
  loRaFishHappyLastMs = until;
}

static void checkpointLoRaFish(uint32_t now) {
  if (loRaFishSettingsSaveFailed ||
      now - loRaFishHappyLastSavedMs >= LORAFISH_CHECKPOINT_MS) saveLoRaFishSettings();
  if (loRaFishSaveFailed || now - fishLastSave >= LORAFISH_CHECKPOINT_MS) saveLoRaFish();
}

// Only LoRagotchi-controlled shutdown/restart. Shared MeshCore reboot paths stay unchanged.
static void flushLoRaFish() {
  tickLoRaFish();
  // One final attempt even if a previous failure is inside the retry backoff.
  loRaFishSettingsLastAttempt = loRaFishLastAttempt = millis() - LORAFISH_SAVE_RETRY_MS;
  saveLoRaFishSettings();
  saveLoRaFish();
}

static void tickLoRaFish() {
  const uint32_t now = millis();
  const uint8_t previousHappiness = fish.happiness;
  if (loRaFishNeedsAppearanceSetup || loRaFishAdoptionEligible) {
    advanceLoRaFishHappiness(now);
    fishLastMinute=now;
    checkpointLoRaFish(now);
    return;
  }
  const uint32_t uptimeMinutes = now / 60000UL;
  if (uptimeMinutes > fish.longestUptimeMinutes) fish.longestUptimeMinutes = uptimeMinutes;
  // Also recover pets saved on the memorial by firmware without the fallback.
  ensureLoRaFishOffspring();
  while (now - fishLastMinute >= 60000UL) {
    fishLastMinute += 60000UL;
    advanceLoRaFishHappiness(fishLastMinute);
    fish.ageMinutes++;
    uint8_t previousEvolution=fish.evolution;
    fish.evolution = fishEvolutionForAge(fish.ageMinutes);
    if (fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.evolution != previousEvolution)
      loRaFishEvolutionFlashUntil=millis()+5000UL;
    if (fish.ageMinutes == LORAFISH_EGG_MINUTES) {
      loRaFishHatchMelodyPlayed = false; // UITask::loop will play it once.
    }
    ensureLoRaFishOffspring();
    if (loRaFishPendingEgg &&
        fish.ageMinutes >= LORAFISH_LIFESPAN_MINUTES + LORAFISH_EGG_MINUTES) {
      hatchLoRaFishOffspring();
      break;
    }
    // Persist real lifecycle boundaries immediately; ordinary catch-up saves
    // are coalesced after the loop, not one write per historical five minutes.
    if (fish.ageMinutes == LORAFISH_EGG_MINUTES ||
        fish.ageMinutes == LORAFISH_LIFESPAN_MINUTES ||
        fish.evolution != previousEvolution) saveLoRaFish();
  }
  advanceLoRaFishHappiness(now);
  if (fish.happiness != previousHappiness) saveLoRaFish();
  checkpointLoRaFish(now);
}

static void reactLoRaFish(uint8_t reaction) {
  fishReaction = reaction;
  fishReactionUntil = millis() + 2500UL;
  fish.rfEvents++;
}

// -----------------------------------------------------------------------------
// LoRaFish drawing compatibility helpers
//
// MeshCore's DisplayDriver intentionally exposes a smaller primitive API than
// the U8g2-style driver used by the standalone/roaming LoRaFish firmware.
// Build pixels and arbitrary lines from fillRect(), which DisplayDriver supports.
// -----------------------------------------------------------------------------
static inline void fishPixel(DisplayDriver& display, int x, int y) {
  display.fillRect(x, y, 1, 1);
}

static void fishLine(DisplayDriver& display, int x0, int y0, int x1, int y1) {
  // Integer Bresenham line. No heap allocation and fine for this tiny sprite.
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    fishPixel(display, x0, y0);
    if (x0 == x1 && y0 == y1) break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// helper

static void maskAdultFishSprite(DisplayDriver& d, int x, int y, bool right) {
  d.setColor(UIColor::window_bkg);

  if (right) {
    // Fill the rounded body interior.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 7 + inset, y + yy,
               x + 9 - inset, y + yy);
    }

    // Fill forked tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int left = x - 14;
      int rightEdge = x - 8 - (ax / 3);
      fishLine(d, left, y + yy, rightEdge, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x - 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x + 2 + half,
               y + yy);
    }

  } else {
    // Mirrored body.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 9 + inset, y + yy,
               x + 7 - inset, y + yy);
    }

    // Mirrored tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int leftEdge = x + 8 + (ax / 3);
      fishLine(d,
               leftEdge, y + yy,
               x + 14, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x + 1 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 2 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }
  }
}

static void drawAdultFishSprite(DisplayDriver& display, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  // V0.3.1: recognisable aquarium fish designed for the real monochrome OLED.
  // Approx. 29x17 pixels. Mostly outline/detail rather than one solid blob.
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);
  display.setColor(UIColor::primary_txt);

    // Clear everything behind the actual fish silhouette first.
  maskAdultFishSprite(display, x, y, right);

  // Now draw the fish normally.
  display.setColor(bodyColor);

  if (right) {
    // Forked tail.
    fishLine(display, x-8, y,   x-14, y-6);
    fishLine(display, x-14,y-6, x-12, y);
    fishLine(display, x-12,y,   x-14, y+6);
    fishLine(display, x-14,y+6, x-8,  y+2);

    // Rounded body outline.
    fishLine(display, x-8,y-2, x-5,y-5);
    fishLine(display, x-5,y-5, x+3,y-6);
    fishLine(display, x+3,y-6, x+8,y-4);
    fishLine(display, x+8,y-4, x+11,y-1);
    fishLine(display, x+11,y-1,x+11,y+2);
    fishLine(display, x+11,y+2,x+7,y+5);
    fishLine(display, x+7,y+5, x-3,y+6);
    fishLine(display, x-3,y+6, x-8,y+2);

    // Head/gill gives the fish a face rather than a rectangle.
    fishLine(display, x+5,y-4, x+5,y+4);
    display.setColor(eyesColor);
    display.fillRect(x+7,y-1,2,2);

    // Scales / body texture.
    display.setColor(scalesColor);
    fishPixel(display, x-4,y-2);
    fishPixel(display, x-1,y-3);
    fishPixel(display, x+2,y-2);
    fishPixel(display, x-5,y+1);
    fishPixel(display, x-2,y+2);
    fishPixel(display, x+1,y+1);
    fishPixel(display, x+3,y+3);

    // Dorsal fin and lower fin.
    display.setColor(bodyColor);
    fishLine(display, x-4,y-5, x-1,y-9);
    fishLine(display, x-1,y-9, x+3,y-6);
    fishLine(display, x-1,y+6, x+2,y+9);
    fishLine(display, x+2,y+9, x+4,y+5);

    // Small mouth.
    fishPixel(display, x+12,y+1);

  } else {
    // Mirrored left-facing fish.
    fishLine(display, x+8, y,   x+14, y-6);
    fishLine(display, x+14,y-6, x+12, y);
    fishLine(display, x+12,y,   x+14, y+6);
    fishLine(display, x+14,y+6, x+8,  y+2);

    fishLine(display, x+8,y-2, x+5,y-5);
    fishLine(display, x+5,y-5, x-3,y-6);
    fishLine(display, x-3,y-6, x-8,y-4);
    fishLine(display, x-8,y-4, x-11,y-1);
    fishLine(display, x-11,y-1,x-11,y+2);
    fishLine(display, x-11,y+2,x-7,y+5);
    fishLine(display, x-7,y+5, x+3,y+6);
    fishLine(display, x+3,y+6, x+8,y+2);

    fishLine(display, x-5,y-4, x-5,y+4);
    display.setColor(eyesColor);
    display.fillRect(x-9,y-1,2,2);

    display.setColor(scalesColor);
    fishPixel(display, x+4,y-2);
    fishPixel(display, x+1,y-3);
    fishPixel(display, x-2,y-2);
    fishPixel(display, x+5,y+1);
    fishPixel(display, x+2,y+2);
    fishPixel(display, x-1,y+1);
    fishPixel(display, x-3,y+3);

    display.setColor(bodyColor);
    fishLine(display, x+4,y-5, x+1,y-9);
    fishLine(display, x+1,y-9, x-3,y-6);
    fishLine(display, x+1,y+6, x-2,y+9);
    fishLine(display, x-2,y+9, x-4,y+5);

    fishPixel(display, x-12,y+1);
  }

  display.setColor(UIColor::primary_txt);
}
static void drawFryFishSprite(DisplayDriver& d, int x, int y, bool right,
                             const LoRaFishAppearance& appearance) {
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);
  // Opaque mask
  d.setColor(UIColor::window_bkg);

  if (right) {
    // Tail interior
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x-8, y-i, x-3, y);
      fishLine(d, x-8, y+i, x-3, y);
    }

    // Body/head
    d.fillRect(x-3, y-2, 9, 5);

  } else {
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x+8, y-i, x+3, y);
      fishLine(d, x+8, y+i, x+3, y);
    }

    d.fillRect(x-6, y-2, 9, 5);
  }


  
  d.setColor(bodyColor);

  // Tiny fry: small body with a proportionally large tail.
  if (right) {
    fishLine(d,x-3,y,   x-8,y-4);
    fishLine(d,x-3,y,   x-8,y+4);
    fishLine(d,x-8,y-4, x-8,y+4);
    d.fillRect(x-3,y-2,7,5);
    d.fillRect(x+4,y-1,2,3);
    d.setColor(scalesColor);
    fishPixel(d,x-1,y+1);
    d.setColor(eyesColor);
    fishPixel(d,x+2,y-1);
  } else {
    fishLine(d,x+3,y,   x+8,y-4);
    fishLine(d,x+3,y,   x+8,y+4);
    fishLine(d,x+8,y-4, x+8,y+4);
    d.fillRect(x-4,y-2,7,5);
    d.fillRect(x-6,y-1,2,3);
    d.setColor(scalesColor);
    fishPixel(d,x+1,y+1);
    d.setColor(eyesColor);
    fishPixel(d,x-3,y-1);
  }
}

static void drawYoungFishSprite(DisplayDriver& d, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);

 // Opaque mask
  d.setColor(UIColor::window_bkg);

  if (right) {
    // Tail
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x-11, y-i, x-6, y);
      fishLine(d, x-11, y+i, x-6, y);
    }

    // Body/head
    d.fillRect(x-6, y-3, 14, 7);

    // Dorsal and lower fins
    for (int i = 0; i <= 3; i++)
      fishLine(d, x-1, y-3, x+1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x-1, y+3, x+1, y+5-i);

  } else {
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x+11, y-i, x+6, y);
      fishLine(d, x+11, y+i, x+6, y);
    }

    d.fillRect(x-8, y-3, 14, 7);

    for (int i = 0; i <= 3; i++)
      fishLine(d, x+1, y-3, x-1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x+1, y+3, x-1, y+5-i);
  }



  d.setColor(bodyColor);

  // Juvenile: longer/slimmer than fry with its first proper dorsal fin.
  if (right) {
    fishLine(d,x-6,y,    x-11,y-5);
    fishLine(d,x-6,y,    x-11,y+5);
    fishLine(d,x-11,y-5, x-11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x+6,y-1,2,3);
    fishLine(d,x-1,y-3, x+1,y-6);
    fishLine(d,x-1,y+3, x+1,y+5);
    d.setColor(scalesColor);
    fishPixel(d,x-2,y+1);
    fishPixel(d,x+1,y+2);
    d.setColor(eyesColor);
    fishPixel(d,x+4,y-1);
  } else {
    fishLine(d,x+6,y,    x+11,y-5);
    fishLine(d,x+6,y,    x+11,y+5);
    fishLine(d,x+11,y-5, x+11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x-8,y-1,2,3);
    fishLine(d,x+1,y-3, x-1,y-6);
    fishLine(d,x+1,y+3, x-1,y+5);
    d.setColor(scalesColor);
    fishPixel(d,x+2,y+1);
    fishPixel(d,x-1,y+2);
    d.setColor(eyesColor);
    fishPixel(d,x-5,y-1);
  }
}

static void drawElderFishSprite(DisplayDriver& d, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  d.setColor(UIColor::window_bkg);

  if (right) {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x-14,y-6+i, x-17,y-8+i);
      fishLine(d,x-14,y+6-i, x-17,y+8-i);
      fishLine(d,x-1+i,y-9,  x-3+i,y-11);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x+14,y-6+i, x+17,y-8+i);
      fishLine(d,x+14,y+6-i, x+17,y+8-i);
      fishLine(d,x+1-i,y-9,  x+3-i,y-11);
    }
  }

  // Keep the known-good adult fish, then add longer flowing fins.
  drawAdultFishSprite(d,x,y,right,appearance);
  d.setColor(bodyColor);

  if (right) {
    fishLine(d,x-14,y-6, x-17,y-8);
    fishLine(d,x-14,y+6, x-17,y+8);
    fishLine(d,x-1,y-9,  x-3,y-11);
  } else {
    fishLine(d,x+14,y-6, x+17,y-8);
    fishLine(d,x+14,y+6, x+17,y+8);
    fishLine(d,x+1,y-9,  x+3,y-11);
  }
}

// Cardputer-only lightweight 2.5D effects. Other boards compile none of this.
#if defined(LORAFISH_CARDPUTER_25D)
static void drawCardputerFishDepth(DisplayDriver& d,int x,int y,bool right,uint8_t stage,
                                  const LoRaFishAppearance& appearance) {
  const int radius = stage==0 ? 5 : stage==1 ? 7 : stage==3 ? 11 : 9;
  const ColorVal base=loRaFishColorValue(appearance.body);
  // A soft dark silhouette under the original sprite, preserving its distinctive details.
  d.setColor(0x0125);
  for(int yy=-radius/2;yy<=radius/2;yy++) {
    const int inset=abs(yy)/2;
    d.fillRect(x-radius+inset+1,y+yy+2,(radius-inset)*2,1);
  }
  // Light-catching upper contour and animated fin glint.
  d.setColor(0xCFFF);
  d.fillRect(x-radius/2,y-radius/2-1,radius,1);
  d.setColor(base);
  const int sway=(millis()/220UL)%3;
  d.fillRect(x+(right ? -radius-5 : radius+3),y-2+sway,2,3);
}

static void drawCardputerTankDepth(DisplayDriver& d) {
  const int w=d.width(),h=d.height();
  // Horizontal bands are deliberately cheap on the ESP32-S3's display driver.
  static const ColorVal water[]={0x010B,0x012D,0x014F,0x0170,0x0190,0x01B0,0x01D0,0x01EF};
  const int top=17,bottom=h-17,span=bottom-top;
  for(int i=0;i<8;i++) {
    int y0=top+(span*i)/8, y1=top+(span*(i+1))/8;
    d.setColor(water[i]); d.fillRect(0,y0,w,y1-y0);
  }
  // Surface shafts of light, offset in successive bands for depth.
  d.setColor(0x2B75);
  for(int i=0;i<5;i++) {
    int x=18+i*48;
    fishLine(d,x,18,x-13,39);
    fishLine(d,x+3,18,x-10,39);
  }
  // Far-off reef silhouettes.
  d.setColor(0x0147);
  for(int i=0;i<7;i++) {
    int x=i*43-12, y=h-31+(i%3)*3;
    d.fillRect(x,y,30,h-18-y);
    d.fillRect(x+8,y-5,12,6);
  }
  // Perspective sand layers and glints.
  d.setColor(0x6328); d.fillRect(0,h-23,w,6);
  d.setColor(0x9C4B); d.fillRect(0,h-18,w,3);
  d.setColor(0xDDAF); d.fillRect(0,h-15,w,1);
  // Parallax seaweed: stems sway gently with millis(), no buffers/allocations.
  for(int i=0;i<7;i++) {
    int x=8+i*37, base=h-24, tall=13+(i%3)*5;
    int sway=(int)((millis()/480UL+i*3)%5)-2;
    d.setColor((i%2)?0x03C8:0x05A8);
    fishLine(d,x,base,x+sway,base-tall);
    fishLine(d,x+2,base,x+5-sway,base-tall+5);
  }
  // Tiny rising particles add depth without expensive alpha blending.
  d.setColor(0x6DBB);
  for(int i=0;i<8;i++) {
    int x=9+i*29, y=20+((i*17+millis()/190UL)%(unsigned)(span>1?span:1));
    d.fillRect(x,y,1,1);
  }
}
#endif

static void drawFishSpriteForStage(DisplayDriver& d,int x,int y,bool right,uint8_t stage,
                                  const LoRaFishAppearance& appearance) {
#if defined(LORAFISH_TDECK_PIXEL_ART)
  // Two-tone pixel shadow and animated sparkle; original evolution sprites
  // and colour genetics remain untouched.
  const int radius=stage==0?6:stage==1?9:stage==2?13:15;
  d.setColor(0x0128);
  d.fillRect(x-radius+2,y+8,radius*2-4,2);
  d.setColor(0x4E1B);
  d.fillRect(x-radius/2,y-radius/2-6,5,2);
#endif
#if defined(LORAFISH_CARDPUTER_25D)
  drawCardputerFishDepth(d,x,y,right,stage,appearance);
#endif
  switch(stage) {
    case 0: drawFryFishSprite(d,x,y,right,appearance); break;
    case 1: drawYoungFishSprite(d,x,y,right,appearance); break;
    case 2: drawAdultFishSprite(d,x,y,right,appearance); break;
    default: drawElderFishSprite(d,x,y,right,appearance); break;
  }
}

static void loRaFishQueueTravelAnimation(LoRaFishTravelAnimation& animation,bool entering,
                                         uint8_t stage,const LoRaFishAppearance& appearance) {
  animation.pending=true;
  animation.active=false;
  animation.entering=entering;
  animation.stage=stage;
  animation.appearance=appearance;
}

static void loRaFishObserveTravelTransitions() {
  // Radio completion, not a sampled animation edge, counts the visit. ARRIVED
  // and RETURN can both be received between two UI ticks at a community tank.
  if (loragotchiDXHome.completedVisit) {
    loragotchiDXHome.completedVisit = false;
    if (loragotchiDXHome.communityVisit) {
      const bool living = !loRaFishNeedsAppearanceSetup &&
          fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES;
      loRaFishHappiness.aquarium(living, fish.happiness);
    }
    loragotchiDXHome.communityVisit = false;
    fish.dxVisits++;
    saveLoRaFish();
    Serial.printf("[LFG2] DX visit complete, total=%lu\n",(unsigned long)fish.dxVisits);
  }
  const bool homeAway=loragotchiDXHome.away;
  const bool guestPresent=loragotchiDXGuest.present;
  const LoRaFishAppearance guestAppearance=loRaFishGuestRenderAppearance();

  if (!loRaFishTravelStateInitialized) {
    loRaFishLastHomeAway=homeAway;
    loRaFishLastGuestPresent=guestPresent;
    loRaFishLastGuestStage=loragotchiDXGuest.stage;
    if (guestPresent) loRaFishLastGuestAppearance=guestAppearance;
    loRaFishTravelStateInitialized=true;
    return;
  }

  if (homeAway!=loRaFishLastHomeAway) {
    loRaFishQueueTravelAnimation(loRaFishHomeTravelAnimation,!homeAway,fish.evolution,
                                 loRaFishLocalAppearance());
    loRaFishLastHomeAway=homeAway;
  }

  if (guestPresent!=loRaFishLastGuestPresent) {
    loRaFishQueueTravelAnimation(loRaFishGuestTravelAnimation,guestPresent,
        guestPresent ? loragotchiDXGuest.stage : loRaFishLastGuestStage,
        guestPresent ? guestAppearance : loRaFishLastGuestAppearance);
    loRaFishLastGuestPresent=guestPresent;
  }

  if (guestPresent) {
    loRaFishLastGuestStage=loragotchiDXGuest.stage;
    loRaFishLastGuestAppearance=guestAppearance;
  }
}

static void loRaFishBeginTravelAnimation(LoRaFishTravelAnimation& animation,int width,
                                         int targetX,int targetY) {
  if (!animation.pending) return;
  animation.pending=false;
  animation.active=true;
  animation.started=millis();
  animation.startX=animation.entering ? -20 : targetX;
  animation.endX=animation.entering ? targetX : width+20;
  animation.y=targetY;
}

static bool loRaFishDrawTravelAnimation(DisplayDriver& display,
                                        LoRaFishTravelAnimation& animation) {
  if (!animation.active) return false;
  const unsigned long elapsed=millis()-animation.started;
  if (elapsed>=LORAFISH_TRAVEL_ANIMATION_MS) {
    animation.active=false;
    return false;
  }
  const float progress=elapsed/(float)LORAFISH_TRAVEL_ANIMATION_MS;
  const int x=animation.startX+(int)((animation.endX-animation.startX)*progress);
  drawFishSpriteForStage(display,x,animation.y,true,animation.stage,animation.appearance);
  return true;
}

static void drawFishSprite(DisplayDriver& d,int x,int y,bool right,
                           const LoRaFishAppearance& appearance) {
  drawFishSpriteForStage(d,x,y,right,fish.evolution,appearance);
}

static void drawHomeFishSprite(DisplayDriver& display,int x,int y,bool right) {
  if (loragotchiDXReturnedFromMonochrome) {
    loragotchiDXReturnedFromMonochrome=false;
    loRaFishReturnShakeUntil=millis()+2400UL;
  }

  LoRaFishAppearance appearance=loRaFishLocalAppearance();
  if (loRaFishColorDisplay && (long)(loRaFishReturnShakeUntil-millis())>0) {
    const unsigned long phase=millis();
    if ((phase/180UL)&1UL) appearance.body=0xFF;
    x += ((phase/70UL)&1UL) ? 2 : -2;
  }
  drawFishSprite(display,x,y,right,appearance);
}

static void drawLoRaFishHeader(DisplayDriver& display, const char* title,
                               const char* right) {
  const int w=display.width();
  display.setColor(UIColor::primary_txt);
  display.setTextSize(1);
  display.setCursor(2,5);
  display.print(title);
  display.drawTextRightAlign(w-2,5,right);

  // Keep the separator visibly clear of the header glyphs.
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,15,w-4,1);
}

static bool loRaFishBatteryValid = false;
static uint32_t loRaFishBatterySampledAt = 0;
static uint16_t loRaFishBatteryMilliVolts = 0;

static uint16_t loRaFishDisplayBattery(UITask* task) {
  const uint32_t now = millis();
  if (!loRaFishBatteryValid || now - loRaFishBatterySampledAt >= 5000UL) {
    loRaFishBatteryMilliVolts = task->getBattMilliVolts();
    loRaFishBatterySampledAt = now;
    loRaFishBatteryValid = true;
  }
  return loRaFishBatteryMilliVolts;
}

// Compact battery icon immediately to the left of the aquarium clock.
// Uses the same 3.3-4.2 V percentage mapping as the RF monitor.
static void drawLoRaFishTankBattery(DisplayDriver& display, UITask* task,
                                   const char* clockText) {
  if (!task) return;
  const int w = display.width();
  const int clockWidth = (int)strlen(clockText) * 6; // default text size 1
  const int x = w - 2 - clockWidth - 21;
  if (x < 0) return;
  const uint16_t mv = loRaFishDisplayBattery(task);
  const int pct = constrain(((int)mv - 3300) * 100 / 900, 0, 100);
  display.setColor(UIColor::primary_txt);
  display.drawRect(x, 5, 15, 8);
  display.fillRect(x + 15, 7, 2, 4);
  if (mv > 0) {
    const int fill = (pct * 11 + 50) / 100;
    if (fill > 0) display.fillRect(x + 2, 7, fill, 4);
  } else {
    display.setCursor(x + 5, 5);
    display.print("?");
  }
}

static void drawLoRaFishEggSprite(DisplayDriver& display,int centerX,int topY) {
  int wobble=((millis()/350UL)%3)-1;
  display.setColor(UIColor::primary_txt);
  display.drawRect(centerX-7+wobble,topY+3,14,14);
  display.fillRect(centerX-5+wobble,topY,10,2);
  display.fillRect(centerX-6+wobble,topY+2,12,1);
  display.fillRect(centerX-5+wobble,topY+17,10,2);
}

static void drawLoRaFishTankStatusBar(DisplayDriver& display,const char* stage,UITask* task) {
  char packetCount[8];
  const uint32_t packets=lorafish_rf_sample_seq;
  if (packets<10000UL) {
    snprintf(packetCount,sizeof(packetCount),"%lu",(unsigned long)packets);
  } else if (packets<1000000UL) {
    snprintf(packetCount,sizeof(packetCount),"%luk",(unsigned long)(packets/1000UL));
  } else {
    snprintf(packetCount,sizeof(packetCount),"%luM",(unsigned long)(packets/1000000UL));
  }
  const int unread=max(0,task->getMsgCount());
  const char* pendingEgg=loRaFishPendingEgg ? "0 " : "";
  char status[40];
  snprintf(status,sizeof(status),"%s%s PKT:%s UNR:%d",pendingEgg,stage,packetCount,unread);
  if (display.getTextWidth(status)>display.width()-4)
    snprintf(status,sizeof(status),"%s%s P:%s U:%d",pendingEgg,stage,packetCount,unread);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(display.width()/2,display.height()-9,status);
}

static void drawLoRaFishEgg(DisplayDriver& display,UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);
  drawLoRaFishEggSprite(display,w/2,24);
  display.fillRect(2,h-15,w-4,1);
  drawLoRaFishTankStatusBar(display,"0",task);
}

static void drawLoRaFishMemorial(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  drawLoRaFishHeader(display,loRaFishDisplayName(),"REST");
  display.setColor(UIColor::primary_txt);
  if (loRaFishPendingEgg) {
    if (loRaFishMemorialEggSide==0) loRaFishMemorialEggSide=random(0,2) ? 1 : -1;
    const int stoneCenter=w/2-loRaFishMemorialEggSide*18;
    const int eggCenter=w/2+loRaFishMemorialEggSide*18;
    display.fillRect(stoneCenter-12,28,24,18);
    display.drawRect(stoneCenter-9,22,18,25);
    display.drawTextCentered(stoneCenter,29,"RIP");
    drawLoRaFishEggSprite(display,eggCenter,27);
  } else {
    display.fillRect(w/2-12,28,24,18);
    display.drawRect(w/2-9,22,18,25);
    display.drawTextCentered(w/2,29,"RIP");
  }
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  if (loRaFishPendingEgg) {
    char generation[24];
    const uint16_t nextGeneration=loRaFishGeneration<65535 ? loRaFishGeneration+1 : 65535;
    snprintf(generation,sizeof(generation),"GEN %u HATCHING",(unsigned)nextGeneration);
    display.drawTextCentered(w/2,h-9,generation);
  } else {
    display.drawTextCentered(w/2,h-9,"Long press reset in OPTIONS");
  }
}

static const char* loRaFishOptionLabel(LoRaFishOption option) {
  static const char* labels[] = {"Send Advert", "Guess Game", "Breed with visitor", "Reset Pet",
      "Display", "Sound", "Schedule and Time", "RF Monitor", "GNSS Monitor", "Send visitor home"};
  return labels[static_cast<uint8_t>(option)];
}

static void drawLoRaFishMenuHint(DisplayDriver& display) {
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(display.width()/2, 56, "1:NEXT 2:BACK L:OK");
}

static void drawLoRaFishValuePicker(DisplayDriver& display) {
  const int w = display.width();
  const uint8_t value = loRaFishValuePicker.value();
  const auto setting = loRaFishValuePicker.key();
  static const char* titles[] = {"FIRMWARE THEME", "BRIGHTNESS", "GAME RENDERING",
      "SCALES", "BODY / BORDER", "EYES", "SLEEP HOUR", "WAKE HOUR", "UTC OFFSET",
      "AWAKE TIMEOUT", "ASLEEP TIMEOUT", "DIM AFTER", "VOLUME", "WAKE ALARM",
      "SLEEP CUE", "MESSAGE TONE", "SCREEN COLOUR"};
  char valueText[24];
  const char* text = valueText;
  switch (setting) {
    case LoRaFishSetting::Theme: text = loRaFishThemes[value].name; break;
    case LoRaFishSetting::Brightness: text = loRaFishBrightnessNames[value]; break;
    case LoRaFishSetting::Rendering: text = value ? "Simulation" : "Smooth"; break;
    case LoRaFishSetting::Scales:
    case LoRaFishSetting::Body:
    case LoRaFishSetting::Eyes: text = loRaFishColorNames[value]; break;
    case LoRaFishSetting::SleepHour:
    case LoRaFishSetting::WakeHour: snprintf(valueText, sizeof(valueText), "%02u:00", value); break;
    case LoRaFishSetting::UtcOffset: {
      const int minutes = (int(value) - 48) * 15;
      snprintf(valueText, sizeof(valueText), "UTC%c%02d:%02d", minutes < 0 ? '-' : '+',
               abs(minutes)/60, abs(minutes)%60);
      break;
    }
    case LoRaFishSetting::TimeoutAwake:
    case LoRaFishSetting::TimeoutAsleep: text = loRaFishTimeoutShortNames[value]; break;
    case LoRaFishSetting::DimAfter: text = loRaFishAlwaysOnDimNames[value]; break;
    case LoRaFishSetting::Volume: text = loRaFishVolumeNames[value]; break;
    case LoRaFishSetting::WakeSound: text = loRaFishWakeSoundNames[value]; break;
    case LoRaFishSetting::SleepSound: text = loRaFishSleepSoundNames[value]; break;
    case LoRaFishSetting::MessageSound: text = loRaFishMessageSoundNames[value]; break;
    case LoRaFishSetting::MonoDisplayColour: text = loRaFishMonoDisplayColourNames[value]; break;
  }
  drawLoRaFishHeader(display, titles[static_cast<uint8_t>(setting)], "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2, 27, text);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2, 42, "Hold to save");
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishOptions(DisplayDriver& display, UITask* task) {
  const bool breed = loRaFishCanBreed();
  if (!LoRaFishOptions::available(loRaFishOption, loRaFishHasSound, breed, loragotchiDXGuest.present))
    loRaFishOption = LoRaFishOption::Advert;
  const uint8_t selected = LoRaFishOptions::index(loRaFishOption, loRaFishHasSound, breed, loragotchiDXGuest.present);
  const uint8_t count = loRaFishOptionCount();
  const uint8_t first = selected > 3 ? selected - 3 : 0;
  drawLoRaFishHeader(display, "OPTIONS", "");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row = 0; row < 4 && first + row < count; ++row) {
    const auto option = LoRaFishOptions::at(first + row, loRaFishHasSound, breed, loragotchiDXGuest.present);
    char line[24];
    snprintf(line, sizeof(line), "%c %s", option == loRaFishOption ? '>' : ' ', loRaFishOptionLabel(option));
    display.setCursor(3, 18 + row*9);
    display.print(line);
  }
  drawLoRaFishMenuHint(display);
}

static void closeLoRaFishAdvertMenu() {
  loRaFishAdvertMenu.close();
  returnToLoRaFishOptions(LoRaFishOption::Advert);
}

static void drawLoRaFishAdvertMenu(DisplayDriver& display) {
  const int w = display.width();
  drawLoRaFishHeader(display, "SEND ADVERT", "");
  display.setColor(UIColor::primary_txt);
  static const char* labels[] = {"Local (zero-hop)", "Flood (multi-hop)", "Back"};
  for (uint8_t row = 0; row < 3; row++) {
    char line[24];
    snprintf(line, sizeof(line), "%c %s",
             row == static_cast<uint8_t>(loRaFishAdvertMenu.selected()) ? '>' : ' ', labels[row]);
    display.setCursor(3, 18 + row * 11);
    display.print(line);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void cancelLoRaFishReset() {
  loRaFishResetConfirmation.cancel();
  returnToLoRaFishOptions(LoRaFishOption::Reset);
}

static void drawLoRaFishResetConfirmation(DisplayDriver& display) {
  const int w = display.width();
  drawLoRaFishHeader(display, "Start a new egg?", "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2, 17, "Pet progress & eggs");
  display.drawTextCentered(w/2, 26, "will be lost forever.");
  const bool reset = loRaFishResetConfirmation.resetSelected();
  display.setCursor(3, 37);
  display.print(reset ? "  Cancel" : "> Cancel");
  display.setCursor(3, 46);
  display.print(reset ? "> Yes, reset pet" : "  Yes, reset pet");
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void startLoRaFishGuessGame() {
  const uint32_t seed=(uint32_t)micros() ^ (uint32_t)millis() ^
      (fish.ageMinutes<<16) ^ fish.rfEvents;
  randomSeed(seed);
  loRaFishGuessNumber=(uint8_t)random(1,10);
  loRaFishGuessPrevious=0;
  loRaFishGuessRounds=0;
  loRaFishGuessScore=0;
  loRaFishGuessHigher=true;
  loRaFishGuessResultShown=false;
  loRaFishGuessCorrect=false;
  loRaFishGuessComplete=false;
}

static void submitLoRaFishGuess() {
  tickLoRaFish();
  if (loRaFishGuessComplete) {
    startLoRaFishGuessGame();
    return;
  }

  uint8_t nextNumber=(uint8_t)random(1,10);
  while (nextNumber==loRaFishGuessNumber)
    nextNumber=(uint8_t)random(1,10);

  loRaFishGuessPrevious=loRaFishGuessNumber;
  loRaFishGuessCorrect=loRaFishGuessHigher
      ? nextNumber>loRaFishGuessNumber
      : nextNumber<loRaFishGuessNumber;
  loRaFishGuessNumber=nextNumber;
  loRaFishGuessRounds++;
  loRaFishGuessResultShown=true;

  if (loRaFishGuessCorrect) {
    loRaFishGuessScore++;
    if (fish.happiness<100) {
      fish.happiness++;
      saveLoRaFish();
    }
  }
  if (loRaFishGuessRounds>=5) loRaFishGuessComplete=true;
}

static void drawLoRaFishGuessGame(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"GUESS GAME","");
  display.setColor(UIColor::primary_txt);

  if (loRaFishGuessComplete) {
    char scoreLine[24];
    snprintf(scoreLine,sizeof(scoreLine),"SCORE %u / 5",(unsigned)loRaFishGuessScore);
    display.drawTextCentered(w/2,26,scoreLine);
    display.drawTextCentered(w/2,40,"HAPPY +1 PER WIN");
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,55,"LONG: PLAY AGAIN");
    return;
  }

  char numberLine[20];
  if (loRaFishGuessResultShown) {
    snprintf(numberLine,sizeof(numberLine),"%u -> %u",
             (unsigned)loRaFishGuessPrevious,(unsigned)loRaFishGuessNumber);
    display.drawTextCentered(w/2,21,numberLine);
    display.drawTextCentered(w/2,31,loRaFishGuessCorrect?"CORRECT":"MISS");
  } else {
    snprintf(numberLine,sizeof(numberLine),"NUMBER %u",(unsigned)loRaFishGuessNumber);
    display.drawTextCentered(w/2,25,numberLine);
  }

  char roundLine[24];
  snprintf(roundLine,sizeof(roundLine),"ROUND %u/5  SCORE %u",
           (unsigned)(loRaFishGuessRounds+1),(unsigned)loRaFishGuessScore);
  display.drawTextCentered(w/2,42,roundLine);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,55,loRaFishGuessHigher?"HIGHER  LONG: PICK":"LOWER  LONG: PICK");
}

static void drawLoRaFishDisplayMenu(DisplayDriver& display) {
  static const char* monoLabels[] = {"Screen timeout", "Brightness", "Firmware theme", "Screen colour", "Game rendering"};
  static const char* colorLabels[] = {"Screen timeout", "Brightness", "Firmware theme", "Fish color", "Game rendering"};
  const uint8_t count = loRaFishDisplayOptionCount();
  const uint8_t first = loRaFishDisplayOption > 3 ? loRaFishDisplayOption - 3 : 0;
  drawLoRaFishHeader(display, "DISPLAY", "");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row = 0; row < 4 && first + row < count; ++row) {
    const uint8_t option = first + row;
    display.setCursor(3, 18 + row*9);
    display.print(option == loRaFishDisplayOption ? "> " : "  ");
    display.print(loRaFishColorDisplay ? colorLabels[option] : monoLabels[option]);
  }
  drawLoRaFishMenuHint(display);
}





static void drawLoRaFishColorMenu(DisplayDriver& display) {
  static const char* parts[] = {"Scales", "Body", "Eyes", "Start fish"};
  const uint8_t colors[] = {loRaFishScaleColor, loRaFishColor, loRaFishEyeColor};
  drawLoRaFishHeader(display, loRaFishNeedsAppearanceSetup ? "NEW FISH" : "FISH COLOR", "");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row = 0; row < (loRaFishNeedsAppearanceSetup ? 4 : 3); ++row) {
    display.setCursor(3, 18 + row*9);
    display.print(row == loRaFishColorField ? "> " : "  ");
    display.print(parts[row]);
    if (row < 3) display.drawTextRightAlign(display.width()-3, 18 + row*9,
        colors[row] > 6 ? "Inherited" : loRaFishColorNames[colors[row]]);
  }
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishTimeMenu(DisplayDriver& display) {
  const int w=display.width();
  char value[20];
  drawLoRaFishHeader(display,"SCHEDULE","");

  const char* labels[]={"Sleep","Wake","UTC offset"};
  const int hours[]={loRaFishSleepHour,loRaFishWakeHour};
  display.setColor(UIColor::primary_txt);
  for (uint8_t row=0; row<3; row++) {
    if (row<2) snprintf(value,sizeof(value),"%02d:00",hours[row]);
    else loRaFishFormatUtcOffset(value,sizeof(value),false);
    display.setCursor(3,22+row*11);
    display.print(row==loRaFishTimeField ? ">" : " ");
    display.print(row==2 ? "Offset" : labels[row]);
    display.drawTextRightAlign(w-3,22+row*11,value);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishTimeoutMenu(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"TIMEOUT","");

  display.setColor(UIColor::primary_txt);
  for (uint8_t row=0; row<3; row++) {
    const uint8_t setting=row==0 ? loRaFishTimeoutAwakeSetting
                                 : row==1 ? loRaFishTimeoutAsleepSetting : loRaFishAlwaysOnDimSetting;
    const char* label=row==0 ? "Awake" : row==1 ? "Asleep" : "Dim after";
    const char* value=row<2 ? loRaFishTimeoutShortNames[min((uint8_t)5,setting)]
                : loRaFishAlwaysOnDimNames[min((uint8_t)2,loRaFishAlwaysOnDimSetting)];
    display.setCursor(3,22+row*12);
    display.print(row==loRaFishTimeoutField ? "> " : "  ");
    display.print(label);
    display.drawTextRightAlign(w-3,22+row*12,value);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}



#ifdef PIN_BUZZER
static void drawLoRaFishSoundMenu(DisplayDriver& display) {
  const int w=display.width();
  static const char* labels[]={"Volume","Wake alarm","Sleep cue","Msg tone"};
  const char* values[]={
    loRaFishVolumeNames[min((uint8_t)4,loRaFishBuzzerVolume)],
    loRaFishWakeSoundNames[min((uint8_t)3,loRaFishWakeSound)],
    loRaFishSleepSoundNames[min((uint8_t)2,loRaFishSleepSound)],
    loRaFishMessageSoundNames[min((uint8_t)3,loRaFishMessageSound)],
  };
  drawLoRaFishHeader(display,"SOUND","");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row=0; row<4; row++) {
    display.setCursor(3,18+row*9);
    display.print(row==loRaFishSoundField ? "> " : "  ");
    display.print(labels[row]);
    display.drawTextRightAlign(w-3,18+row*9,values[row]);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}
#endif

static uint32_t loRaFishDXContactIndex=0;
// Selection is a persistent-contact index, never a filtered row number.
static uint8_t loRaFishDXDestinationKey[PUB_KEY_SIZE];
static bool loRaFishDXDestinationKnown = false;

static bool loRaFishDXDestination(ContactInfo& c) {
  if (loRaFishDXDestinationKnown) {
    const ContactInfo* found = the_mesh.lookupContactByPubKey(loRaFishDXDestinationKey, PUB_KEY_SIZE);
    if (!found) return false;
    c = *found;
    return true;
  }
  return the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + loragotchiDXHome.destinationIndex, c);
}

static bool loRaFishDXEligible(const ContactInfo& c) {
  return c.type == ADV_TYPE_CHAT && LoRaFishText::hasFish(c.name, sizeof(c.name));
}

static uint32_t loRaFishDXContactCount(uint32_t* selectedRow = nullptr) {
  uint32_t count = 0;
  for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
    ContactInfo c;
    if (the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + i, c) && loRaFishDXEligible(c)) {
      if (selectedRow && (uint32_t)i == loRaFishDXContactIndex) *selectedRow = count;
      ++count;
    }
  }
  return count;
}

static bool loRaFishDXGetContact(ContactInfo& c) {
  const int available = the_mesh.getNumContacts();
  if (available <= 0) return false;
  const uint32_t count = (uint32_t)available;
  if (loRaFishDXContactIndex >= count) loRaFishDXContactIndex = 0;
  for (uint32_t offset = 0; offset < count; ++offset) {
    const uint32_t index = (loRaFishDXContactIndex + offset) % count;
    if (the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + index, c) && loRaFishDXEligible(c)) {
      loRaFishDXContactIndex = index;
      return true;
    }
  }
  return false;
}

static void moveLoRaFishDXContact(bool previous) {
  const int available = the_mesh.getNumContacts();
  if (available <= 0) return;
  const uint32_t count = (uint32_t)available;
  loRaFishDXContactIndex %= count;
  for (uint32_t step = 1; step <= count; ++step) {
    const uint32_t index = (loRaFishDXContactIndex + (previous ? count - step : step)) % count;
    ContactInfo c;
    if (the_mesh.getContactByIdx((uint32_t)MAX_ANON_CONTACTS + index, c) && loRaFishDXEligible(c)) {
      loRaFishDXContactIndex = index;
      return;
    }
  }
}

static void drawLoRaFishName(DisplayDriver& display, int y, const char* name, size_t capacity) {
  char text[32];
  const size_t columns = min((int)sizeof(text) - 1, max(0, (display.width() - 6) / 6));
  LoRaFishText::display(text, columns + 1, name, capacity);
  display.drawTextCentered(display.width() / 2, y, text);
}

static void drawLoRaFishDX(DisplayDriver& display) {
  const int w=display.width(),h=display.height();
  const bool choosingContact = loRaFishPage == LORAFISH_DX_CONTACT_PAGE;
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"DX VISIT",tb);
  display.setColor(UIColor::primary_txt);
  const char* returnStatus=loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Pending?"RETURNING VISITOR":
      loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Failed?"RETURN UNCONFIRMED":
      loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Confirmed?"RETURN DELIVERED":"Visit a friend";
  if (!choosingContact && loragotchiDXGuestReturn.status != LoRaFishGuestReturn::Idle &&
      (loragotchiDXHome.away || loragotchiDXHome.pending || loragotchiDXHome.committing))
    display.drawTextCentered(w/2,16,returnStatus);

  if(loragotchiDXHome.away) {
    display.drawTextCentered(w/2,27,loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ?
        "RETURN UNCONFIRMED" : loragotchiDXVisit.phase == LoRaFishVisit::Recall ? "RECALLING..." : "FISH AWAY");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,39,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if(loragotchiDXHome.committing) {
    display.drawTextCentered(w/2,28,loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ?
        "UNCONFIRMED" : loragotchiDXVisit.phase == LoRaFishVisit::Recall ? "RECALLING..." : "DELIVERING...");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,40,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if(loragotchiDXHome.pending) {
    display.drawTextCentered(w/2,28,"CALLING...");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,40,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if (!choosingContact) {
    display.drawTextCentered(w/2,27,loragotchiDXVisit.phase == LoRaFishVisit::NoReply ? "NO REPLY - RETRY" : returnStatus);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,39,"Hold to choose");
  } else {
    ContactInfo c;
    if(loRaFishDXGetContact(c)) {
      drawLoRaFishName(display,27,c.name,sizeof(c.name));
      uint32_t row = 0;
      const uint32_t count = loRaFishDXContactCount(&row);
      char b[28]; snprintf(b,sizeof(b),"%lu/%lu fish tanks",
          (unsigned long)(row+1),(unsigned long)count);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,39,b);
    } else {
      display.drawTextCentered(w/2,27,"NO FISH TANKS");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,39,"Fish-marked friends");
    }
  }
  display.setColor(UIColor::secondary_txt);
  const char* hint = "1:NEXT 2:BACK L:OPEN";
  if (choosingContact) {
    if (loragotchiDXHome.away || loragotchiDXHome.committing) hint = "2:BACK L:RECALL";
    else if (loragotchiDXHome.pending) hint = "2:BACK";
    else hint = "1:NEXT 2:BACK L:SEND";
  }
  display.drawTextCentered(w/2,h-9,hint);
}

static void drawLoRaFishRFMonitor(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();

  // Capture one history point per actual MeshCore RX sample.
  if (lorafish_rf_sample_seq != loRaFishRfSeenSeq) {
    loRaFishRfSeenSeq = lorafish_rf_sample_seq;
    int v=(int)lorafish_last_rssi;
    if (v < -127) v=-127;
    if (v > -20) v=-20;
    if (loRaFishRfHistoryCount < 48) {
      loRaFishRfHistory[loRaFishRfHistoryCount++]=(int8_t)v;
    } else {
      memmove(&loRaFishRfHistory[0],&loRaFishRfHistory[1],47);
      loRaFishRfHistory[47]=(int8_t)v;
    }
  }

  char right[16];
  uint16_t mv=loRaFishDisplayBattery(task);
  int batt=(int)((mv-3300L)*100L/(4200L-3300L));
  batt=constrain(batt,0,100);
  snprintf(right,sizeof(right),"BAT %d%%",batt);
  drawLoRaFishHeader(display,"RF MONITOR",right);

  display.setColor(UIColor::primary_txt);
  char line[32];
  snprintf(line,sizeof(line),"RSSI %.0f  SNR %.1f",lorafish_last_rssi,lorafish_last_snr);
  display.setCursor(3,20);
  display.print(line);

  // 48-column RSSI history graph. -120 dBm is bottom, -40 dBm is top.
  const int gx=3, gy=31, gw=min(48,w-6), gh=20;
  display.setColor(UIColor::secondary_txt);
  display.fillRect(gx,gy+gh,gw,1);
  display.fillRect(gx,gy,1,gh+1);
  int start=max(0,(int)loRaFishRfHistoryCount-gw);
  for(int i=start;i<(int)loRaFishRfHistoryCount;i++) {
    int r=loRaFishRfHistory[i];
    int level=constrain((r+120)*gh/80,0,gh);
    int x=gx+(i-start);
    display.fillRect(x,gy+gh-level,1,max(1,level));
  }

  display.setColor(UIColor::secondary_txt);
  if (lorafish_last_rf_ms==0) {
    display.drawTextRightAlign(w-2,55,"RX --");
  } else {
    unsigned long age=(millis()-lorafish_last_rf_ms)/1000UL;
    snprintf(line,sizeof(line),"RX %lus",age);
    display.drawTextRightAlign(w-2,55,line);
  }

  // Graphical battery gauge.
  int bx=3, by=55, bw=38, bh=7;
  display.drawRect(bx,by,bw,bh);
  display.fillRect(bx+bw,by+2,2,3);
  int fill=(batt*(bw-4))/100;
  if(fill>0) display.fillRect(bx+2,by+2,fill,bh-4);
}

static void drawLoRaFishGNSS(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  LocationProvider* location=sensors.getLocationProvider();
  const bool enabled=task->getGPSState();
  drawLoRaFishHeader(display,"GNSS",enabled ? "ON" : "OFF");

  display.setColor(UIColor::primary_txt);
  if (!location) {
    display.drawTextCentered(w/2,27,"NO RECEIVER");
  } else if (!enabled) {
    display.drawTextCentered(w/2,27,"GNSS OFF");
  } else {
    display.drawTextCentered(w/2,22,location->isValid() ? "FIX" : "SEARCHING");
    char line[32];
    snprintf(line,sizeof(line),"SAT %ld",location->satellitesCount());
    display.drawTextCentered(w/2,33,line);
    if (location->isValid()) {
      snprintf(line,sizeof(line),"%.4f %.4f",
               location->getLatitude()/1000000.0,
               location->getLongitude()/1000000.0);
      display.drawTextCentered(w/2,44,line);
    } else {
      display.drawTextCentered(w/2,44,"WAITING FOR FIX");
    }
  }

  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,56,"LONG:TOGGLE");
}


// T-Deck 16-bit aquarium scenery. All shapes are integer pixel primitives;
// no spritesheets, dynamic allocation, or additional flash partitions.
#if defined(LORAFISH_TDECK_PIXEL_ART)
static void drawTDeckPixelAquarium(DisplayDriver& d) {
  const int w=d.width(), h=d.height();
  const int top=17, bottom=h-17, span=bottom-top;
  if (span<30) return;
  // Four broad bands reduce large SPI transactions on the unbuffered LCD.
  static const uint16_t water[4]={0x0129,0x016B,0x01AD,0x01EF};
  for(int i=0;i<4;i++) {
    const int y0=top+span*i/4, y1=top+span*(i+1)/4;
    d.setColor(water[i]); d.fillRect(0,y0,w,y1-y0);
  }
  // Broken pixel-light shafts near the surface.
  d.setColor(0x2B95);
  for(int i=0;i<5;i++) {
    const int x=19+i*(w-38)/4;
    for(int j=0;j<5;j++) d.fillRect(x-j*2,top+3+j*5,3,3);
  }
  // Distant reef: stepped silhouettes behind the swimming fish.
  d.setColor(0x0168);
  for(int i=0;i<9;i++) {
    const int x=i*41-12, y=bottom-25-(i%3)*5;
    d.fillRect(x,y,29,bottom-y);
    d.fillRect(x+7,y-5,15,6);
    d.fillRect(x+11,y-9,7,5);
  }
  // Layered sandy floor and chunky rocks.
  d.setColor(0x5AC9); d.fillRect(0,bottom-13,w,13);
  d.setColor(0x9C6B); d.fillRect(0,bottom-9,w,9);
  d.setColor(0xDDAF); d.fillRect(0,bottom-9,w,2);
  for(int i=0;i<7;i++) {
    const int x=12+i*49;
    d.setColor((i&1)?0x39A8:0x3187);
    d.fillRect(x,bottom-18,21,7);
    d.fillRect(x+4,bottom-22,13,5);
    d.setColor(0x6B6D); d.fillRect(x+5,bottom-21,5,2);
  }
  // Pixel seaweed: three offset stems, subtly animated.
  for(int i=0;i<9;i++) {
    const int x=9+i*38, base=bottom-10;
    const int sway=(int)((millis()/420UL+i*3)%5)-2;
    d.setColor((i&1)?0x04E7:0x05A8);
    for(int j=0;j<5;j++) {
      const int yy=base-j*6;
      const int xx=x+((j&1)?sway:-sway);
      d.fillRect(xx,yy,3,7);
      if(j>0 && j<4) d.fillRect(xx+((j&1)?3:-4),yy+1,5,2);
    }
  }
  // Sparse animated floating pixels, distinct from existing bubbles.
  d.setColor(0x65DB);
  for(int i=0;i<13;i++) {
    const int x=7+(i*53)%(w-14);
    const int y=top+5+(int)((i*29+millis()/280UL)%(unsigned)(span-24));
    d.fillRect(x,y,2,2);
  }
}
#endif

static void drawLoRaFishBubbles(DisplayDriver& display) {
  const int h = display.height();

  // Different speeds and starting positions so they don't move together.
  int y1 = h - 10 - ((millis() / 180) % 35);
  int y2 = h - 10 - ((millis() / 260 + 13) % 38);
  int y3 = h - 10 - ((millis() / 340 + 25) % 36);

  // Gentle sideways wobble.
  int x1 = 15 + (int)(2.0f * sinf(millis() / 700.0f));
  int x2 = 29 + (int)(2.0f * sinf(millis() / 900.0f));
  int x3 = display.width() - 18 +
           (int)(2.0f * sinf(millis() / 800.0f));

  display.setColor(UIColor::secondary_txt);

  display.drawRect(x1, y1, 2, 2);
  display.drawRect(x2, y2, 3, 3);
  display.drawRect(x3, y3, 2, 2);
}


static void drawLoRaFishPoop(DisplayDriver& display, int x, int y, uint8_t phase) {
  display.setColor(UIColor::primary_txt);
  int wobble = phase & 1;

  // v0.3.5.3: gloriously oversized 9x8 Tamagotchi-style pile.
  display.fillRect(x+4+wobble, y,   2,1);
  display.fillRect(x+3,        y+1, 4,1);
  display.fillRect(x+2,        y+2, 5,1);
  display.fillRect(x+1,        y+3, 7,2);
  display.fillRect(x,          y+5, 9,2);
  display.fillRect(x+1,        y+7, 7,1);

  // Three-phase animated stink trails.
  uint8_t stink = (millis()/280UL + phase) % 3;
  if (stink == 0) {
    display.fillRect(x+1,y-5,1,2);
    display.fillRect(x+2,y-7,1,2);
    display.fillRect(x+6,y-6,1,2);
    display.fillRect(x+7,y-8,1,2);
  } else if (stink == 1) {
    display.fillRect(x+2,y-5,1,2);
    display.fillRect(x+1,y-7,1,2);
    display.fillRect(x+7,y-6,1,2);
    display.fillRect(x+6,y-8,1,2);
  } else {
    display.fillRect(x+1,y-6,1,2);
    display.fillRect(x+2,y-8,1,2);
    display.fillRect(x+6,y-5,1,2);
    display.fillRect(x+7,y-7,1,2);
  }
}

static void drawLoRaFishHygiene(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  const int floorY=h-20;
  uint8_t wiggle=(millis()/350UL)&1;

  static const int poopX[3]={20,58,96};
  for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++)
    drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));

  if (loRaFishCleaning) {
    float p=(millis()-loRaFishCleaningStarted)/(float)LORAFISH_CLEAN_MS;
    if(p>1.0f) p=1.0f;
    int bx=-8+(int)((w+10)*p);

    // Tiny broom: diagonal handle + 7px brush.
    display.setColor(UIColor::primary_txt);
    fishLine(display,bx+5,floorY-15,bx,floorY-5);
    display.fillRect(bx-3,floorY-5,8,2);
    display.fillRect(bx-2,floorY-3,1,2);
    display.fillRect(bx,floorY-3,1,2);
    display.fillRect(bx+2,floorY-3,1,2);
    display.fillRect(bx+4,floorY-3,1,2);

    // Poop already passed by the broom visually disappears.
    for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++) {
      if (poopX[i]+9 > bx+5)
        drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));
    }
  }
}

static void drawLoRaFishAquarium(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
#if defined(LORAFISH_CARDPUTER_25D)
  drawCardputerTankDepth(display);
#elif defined(LORAFISH_TDECK_PIXEL_ART)
  drawTDeckPixelAquarium(display);
#endif
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);

  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  drawLoRaFishBubbles(display);
  //display.drawRect(15,21,2,2);
  //display.drawRect(24,31,3,3);
  //display.drawRect(w-18,20,2,2);
  //display.fillRect(6,h-16,2,6);
  //display.fillRect(w-9,h-18,2,8);

  int x=0, y=0;
  bool right=true;
  loRaFishNaturalPosition(w,x,y,right);

  if (loRaFishFeeding) {
    const unsigned long elapsed=millis()-loRaFishFeedStarted;
    const int foodX=w/2+18;
    const int targetX=foodX-15;
    const int targetY=35;

    if (elapsed<LORAFISH_FEED_DROP_MS) {
      const float dropProgress=elapsed/(float)LORAFISH_FEED_DROP_MS;
      const int foodY=18+(int)(16*dropProgress);
      display.setColor(UIColor::primary_txt);
      display.fillRect(foodX,foodY,2,2);
    }

    if (elapsed>=LORAFISH_FEED_NOTICE_MS && elapsed<LORAFISH_FEED_DONE_MS) {
      if (!loRaFishFeedChasing) {
        loRaFishFeedStartX=x;
        loRaFishFeedStartY=y;
        loRaFishFeedLastX=x;
        loRaFishFeedLastXValid=true;
        loRaFishFeedChasing=true;
      }

      const bool facingFood=targetX>=loRaFishFeedStartX;
      if (elapsed<LORAFISH_FEED_NOTICE_MS+LORAFISH_FEED_RUSH_MS) {
        const float rushProgress=(elapsed-LORAFISH_FEED_NOTICE_MS)/
            (float)LORAFISH_FEED_RUSH_MS;
        x=loRaFishFeedStartX+(int)((targetX-loRaFishFeedStartX)*rushProgress);
        y=loRaFishFeedStartY+(int)((targetY-loRaFishFeedStartY)*rushProgress);
        right=facingFood;
      } else if (elapsed<LORAFISH_FEED_DROP_MS+LORAFISH_FEED_EAT_MS) {
        x=targetX;
        y=targetY;
        right=facingFood;
      } else {
        const float returnProgress=(elapsed-LORAFISH_FEED_DROP_MS-
            LORAFISH_FEED_EAT_MS)/(float)LORAFISH_FEED_RETURN_MS;
        x=targetX+(int)((x-targetX)*returnProgress);
        y=targetY+(int)((y-targetY)*returnProgress);
        if (returnProgress<0.5f) right=facingFood;
      }

      if (loRaFishFeedLastXValid) {
        if (x>loRaFishFeedLastX) right=true;
        else if (x<loRaFishFeedLastX) right=false;
        loRaFishFeedLastX=x;
      }
    }

    if (elapsed>=LORAFISH_FEED_DONE_MS) {
      loRaFishFeeding=false;
      loRaFishFeedChasing=false;
      loRaFishFeedLastXValid=false;
    }
  }

 drawLoRaFishHygiene(display);
  const bool localSleeping = loRaFishIsSleeping();
  const int homeTargetX=localSleeping ? (loragotchiDXGuest.present ? w/3 : w/2-5) : x;
  const int homeTargetY=localSleeping ? 38 : y;
  loRaFishBeginTravelAnimation(loRaFishHomeTravelAnimation,w,homeTargetX,homeTargetY);
  if (!loragotchiDXHome.away && !loragotchiDXHome.committing) {
    // A guest must not freeze the host fish.  The home fish keeps its normal
    // swim path; if it is bedtime, only the home fish sleeps.
    bool homeTraveling=false;
    if (loRaFishHomeTravelAnimation.active && loRaFishHomeTravelAnimation.entering)
      homeTraveling=loRaFishDrawTravelAnimation(display,loRaFishHomeTravelAnimation);
    if (!homeTraveling && !loRaFishHomeTravelAnimation.active && localSleeping) {
      const int sleepX = loragotchiDXGuest.present ? w/3 : w/2-5;
      const int sleepY = 38;
      drawHomeFishSprite(display,sleepX,sleepY,true);
      display.setColor(UIColor::primary_txt);
      display.setCursor(min(w-4,sleepX+12), max(17,sleepY-11)); display.print("z");
      display.setCursor(min(w-4,sleepX+19), max(12,sleepY-16)); display.print("Z");
    } else if (!homeTraveling && !loRaFishHomeTravelAnimation.active) {
      drawHomeFishSprite(display,x,y,right);
    }
  } else {
    const bool homeLeaving=loRaFishHomeTravelAnimation.active &&
        !loRaFishHomeTravelAnimation.entering &&
        loRaFishDrawTravelAnimation(display,loRaFishHomeTravelAnimation);
    if (!homeLeaving && !loRaFishGuestTravelAnimation.active && !loragotchiDXGuest.present) {
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,35,loragotchiDXHome.committing ?
          (loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ? "VISIT UNCONFIRMED" : "DELIVERING...") : "FISH DX AWAY");
    }
  }

  const bool guestPresent=loragotchiDXGuest.present;
  const uint8_t guestStage=guestPresent ? loragotchiDXGuest.stage : loRaFishGuestTravelAnimation.stage;
  unsigned long guestPeriod=8000UL;
  if(guestStage==0) guestPeriod=5200UL;
  else if(guestStage==1) guestPeriod=6500UL;
  else if(guestStage>=3) guestPeriod=11000UL;
  int gx=0,gy=0; bool gright=true;
  loRaFishNaturalPositionForStage(w,guestStage,guestPeriod/2,gx,gy,gright);
  loRaFishBeginTravelAnimation(loRaFishGuestTravelAnimation,w,gx,gy);
  if (guestPresent) {
    // Same movement model as a lone fish, phase shifted by half a cycle.
    // This keeps the visitor inside the tank and removes the old erratic
    // full-screen sweep.
    const bool guestEntering=loRaFishGuestTravelAnimation.active &&
        loRaFishGuestTravelAnimation.entering &&
        loRaFishDrawTravelAnimation(display,loRaFishGuestTravelAnimation);
    if (!guestEntering && !loRaFishGuestTravelAnimation.active)
      drawFishSpriteForStage(display,gx,gy,gright,loragotchiDXGuest.stage,loRaFishGuestRenderAppearance());
    if (!loRaFishGuestTravelAnimation.active) {
      display.setColor(UIColor::secondary_txt);
      drawLoRaFishName(display,27,loragotchiDXGuest.guestName,sizeof(loragotchiDXGuest.guestName));
    }
  } else if (loRaFishGuestTravelAnimation.active && !loRaFishGuestTravelAnimation.entering) {
    loRaFishDrawTravelAnimation(display,loRaFishGuestTravelAnimation);
  }

  if (millis() < loRaFishEvolutionFlashUntil) {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,19,"* EVOLVED! *");
  } else if (fishReaction==1) display.drawTextCentered(w/2,19,"* MESSAGE *");
  else if (fishReaction==2) display.drawTextCentered(w/2,19,"* SENT *");
  else if (fishReaction==3) display.drawTextCentered(w/2,19,"* HELLO! *");

  const char* tankStage=fish.ageMinutes<LORAFISH_EGG_MINUTES ? "0" : fishStageName();
  drawLoRaFishTankStatusBar(display,tankStage,task);
}

static void drawLoRaFishStats(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  char generation[12];
  snprintf(generation,sizeof(generation),"G%u",(unsigned)loRaFishGeneration);
  drawLoRaFishHeader(display,"STATS","");
  display.setColor(UIColor::primary_txt);
  char statLine[48];

  snprintf(statLine,sizeof(statLine),"Age  %luh %02lum",
           (unsigned long)(fish.ageMinutes/60UL),
           (unsigned long)(fish.ageMinutes%60UL));
  display.setCursor(2,21);
  display.print(statLine);
  display.drawTextRightAlign(w-2,21,generation);

  snprintf(statLine,sizeof(statLine),"Happy %u%%",(unsigned)fish.happiness);
  display.setCursor(2,31);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"RF %lu  Msg %lu",
           (unsigned long)fish.rfEvents,
           (unsigned long)fish.messages);
  display.setCursor(2,41);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"ACK %lu  BLE %s",
           (unsigned long)fish.txAcks,
           task->hasConnection()?"ON":"OFF");
  display.setCursor(2,51);
  display.print(statLine);
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,60,w-4,1);
}

static void drawLoRaFishClockPage(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  char tb[8], db[24];
  bool meshTime = loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"CLOCK",task->hasConnection()?"PHONE":"MESH");

  display.setColor(UIColor::primary_txt);
  display.setTextSize(2);
  display.drawTextCentered(w/2,26,tb);
  display.setTextSize(1);
  display.drawTextCentered(w/2,44,db);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,55,meshTime ? "RTC + LOCAL OFFSET" : "build-time fallback");
}

static void drawLoRaFishPairing(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"PHONE PAIRING","BLE");

  uint32_t activePin = the_mesh.getBLEPin();

  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2,20,"MeshCore Companion");

  char pin[16];
  if (activePin != 0) {
    snprintf(pin,sizeof(pin),"%06lu",(unsigned long)activePin);
    display.setTextSize(2);
    display.drawTextCentered(w/2,30,pin);
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,45,"PIN for phone");
    display.drawTextCentered(w/2,55,"ENTER: next");
  } else {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,32,"No PIN configured");
    display.drawTextCentered(w/2,47,"BLE security off");
    display.drawTextCentered(w/2,63,"ENTER: next");
  }
}

static bool loRaFishIsSleeping() {
  return LoRaFishHappiness::sleeping((loRaFishLocalEpoch() / 3600) % 24,
                                    loRaFishSleepHour, loRaFishWakeHour);
}

static void drawLoRaFishSleeping(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
#if defined(LORAFISH_CARDPUTER_25D)
  drawCardputerTankDepth(display);
  drawLoRaFishBubbles(display);
#elif defined(LORAFISH_TDECK_PIXEL_ART)
  // Sleeping fish stays inside the same aquarium as the awake fish.
  drawTDeckPixelAquarium(display);
  drawLoRaFishBubbles(display);
#endif
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);

 drawHomeFishSprite(display,w/2-5,38,true);

display.setColor(UIColor::primary_txt);

display.setCursor(w/2+12, 27);
display.print("z");

display.setCursor(w/2+19, 22);
display.print("Z");

display.setCursor(w/2+27, 17);
display.print("Z");

display.setColor(UIColor::secondary_txt);
display.fillRect(2,h-15,w-4,1);
display.drawTextCentered(w/2,h-9,"Sleeping");
}

static void drawLoRaFishChannelNotice(DisplayDriver& display) {
  const int x = display.width() / 2;
  display.setTextSize(1);
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(x, 2, "#loragotchi-dx");
  display.drawTextCentered(x, 14, "Could not join");
  display.drawTextCentered(x, 26, "No free channel slot");
  display.drawTextCentered(x, 38, "Channels unchanged");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(x, 56, "CLICK TO DISMISS");
}

static void drawLoRaFishNameNotice(DisplayDriver& display) {
  const int x = display.width() / 2;
  drawLoRaFishHeader(display, "GET FOUND BY FRIENDS", "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(x, 18, "Add a fish emoji to");
  display.drawTextCentered(x, 27, "your MeshCore name");
  display.drawTextCentered(x, 36, "to appear as a");
  display.drawTextCentered(x, 45, "LoRagotchi-DX tank.");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(x, 56, "CLICK TO DISMISS");
}

static void drawLoRaFishMonoDisplayNotice(DisplayDriver& display) {
  const int x = display.width() / 2;
  drawLoRaFishHeader(display, "MONO SCREEN COLOUR", "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(x, 18, "Choose your OLED panel");
  display.drawTextCentered(x, 28, "so colour peers can");
  display.drawTextCentered(x, 38, "render your visitor.");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(x, 56, "CLICK TO DISMISS");
}

static void drawLoRaFishAdoption(DisplayDriver& display) {
  drawLoRaFishHeader(display,"AQUARIUM EGGS","");
  display.setColor(UIColor::primary_txt);
  const bool saving=loRaFishEggApplying || loRaFishEggSession.phase==LoRaFishEgg::Offered;
  display.drawTextCentered(display.width()/2,17,saving ? "Saving egg..." :
      loRaFishEggSession.phase==LoRaFishEgg::Choose ? "Aquarium detected!" : "Looking for eggs...");
  display.drawTextEllipsized(3,29,display.width()-6,loRaFishEggTankName);
  display.drawTextCentered(display.width()/2,41,saving ? "Retrying if needed" :
      loRaFishEggSession.phase==LoRaFishEgg::Choose ? "Choose egg in app" : "Checking support");
  display.drawTextCentered(display.width()/2,55,saving ? "Please wait" : "Click next/Hold skip");
}

static void drawLoRaFish(DisplayDriver& display, UITask* task) {
  if (loRaFishChannelNotice) {
    drawLoRaFishChannelNotice(display);
    return;
  }
  if (loRaFishNameNotice) {
    drawLoRaFishNameNotice(display);
    return;
  }
  if (loRaFishMonoDisplayNotice) {
    drawLoRaFishMonoDisplayNotice(display);
    return;
  }
  if (loRaFishAdoptionEligible) {
    drawLoRaFishAdoption(display);
    return;
  }
  if (loRaFishValuePicker.active()) {
    drawLoRaFishValuePicker(display);
    return;
  }
  if (millis() >= fishReactionUntil) fishReaction=0;

  const bool sleeping = loRaFishIsSleeping();

  switch (loRaFishPage) {
    case 1: drawLoRaFishStats(display,task); break;
    case 2: drawLoRaFishClockPage(display,task); break;
    case 3: drawLoRaFishPairing(display); break;
    case 4: drawLoRaFishDX(display); break;
    case LORAFISH_DX_CONTACT_PAGE: drawLoRaFishDX(display); break;
    case 5: drawLoRaFishOptions(display,task); break;
    case 6: drawLoRaFishRFMonitor(display,task); break;
    case 7: drawLoRaFishGNSS(display,task); break;
    case 8: drawLoRaFishDisplayMenu(display); break;
    case 9: drawLoRaFishColorMenu(display); break;
    case 10: drawLoRaFishTimeMenu(display); break;
    case 11: drawLoRaFishTimeoutMenu(display); break;
    case 12: drawLoRaFishDisplayMenu(display); break;
    case 13: drawLoRaFishDisplayMenu(display); break;
    case 15: drawLoRaFishDisplayMenu(display); break;
    case 16: drawLoRaFishGuessGame(display); break;
    case LORAFISH_RESET_CONFIRM_PAGE: drawLoRaFishResetConfirmation(display); break;
    case LORAFISH_ADVERT_PAGE: drawLoRaFishAdvertMenu(display); break;
  #ifdef PIN_BUZZER
    case 14: drawLoRaFishSoundMenu(display); break;
  #endif
    default:
      if (fish.ageMinutes >= LORAFISH_LIFESPAN_MINUTES) drawLoRaFishMemorial(display);
      else if (fish.ageMinutes < LORAFISH_EGG_MINUTES) drawLoRaFishEgg(display,task);
      // DX away state takes priority over the local sleep schedule.
      // drawLoRaFishAquarium() already handles away and guest states.
      else if (loragotchiDXHome.away || loragotchiDXGuest.present) drawLoRaFishAquarium(display,task);
      else if (sleeping) drawLoRaFishSleeping(display,task);
      else drawLoRaFishAquarium(display,task);
      break;
  }
}

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }


  //=============== startup screen ====================================
  int render(DisplayDriver& display) override {
    // LoRaGotchi DX v1.0 splash. Render the title with the display font rather
    // than an XBM logo so it stays crisp on both M2 and Heltec V3 OLEDs.
    const int cx = display.width() / 2;
    display.setColor(UIColor::primary_txt);
    display.setTextSize(2);
    display.drawTextCentered(cx, 3, "LoRaGotchi");

    // Tiny pixel-fish motif, deliberately kept inside the title area.
    display.setTextSize(1);
    display.drawTextCentered(cx, 21, "><((o>");
    display.drawTextCentered(cx, 33, "DX v1.0");

    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(cx, 44, "M0YNW");
    display.drawTextCentered(cx, 54, "Powered by MeshCore");

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
#if UI_DISCOVER_SCREEN
    DISCOVERY,
#endif
#ifndef UI_NO_HIBERNATE
    SHUTDOWN,
#endif
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];
#if UI_DISCOVER_SCREEN
  DiscoveredNode discovered[DISCOVERED_NODES_TABLE_SIZE]; // not circular, latest discovered nodes are not kept
  uint32_t disc_node_req_tag = 0;
  uint32_t disc_nodes_count = 0;
  uint32_t discovery_req_time = 0;
  bool discovery_disp_names = true; // by default desplay names if available (removes SNR_O)
#endif

  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    // Convert millivolts to percentage
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3000
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    const int minMilliVolts = BATT_MIN_MILLIVOLTS;
    const int maxMilliVolts = BATT_MAX_MILLIVOLTS;
    int batteryPercentage = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
    if (batteryPercentage < 0) batteryPercentage = 0; // Clamp to 0%
    if (batteryPercentage > 100) batteryPercentage = 100; // Clamp to 100%

    // battery icon
    int iconWidth = 24;
    int iconHeight = 10;
    int iconX = display.width() - iconWidth - 5; // Position the icon near the top-right corner
    int iconY = 0;
    display.setColor(UIColor::title_txt);

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 3, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // while charging, show a bolt (or a plug once full) just left of the battery,
    // keeping the fill bar itself clean and uninterrupted
    bool charging = board.isExternalPowered();
    if (charging) {
      // There's no charge-complete signal on most boards, so "full" is a high
      // voltage band rather than an exact 100% (a real pack rarely reads 4.2V).
      const int BATT_FULL_PCT = 95;
      const uint8_t* symbol = (batteryPercentage >= BATT_FULL_PCT) ? plug_icon : charging_icon;
      display.setColor(UIColor::title_txt);
      display.drawXbm(iconX - 9, iconY + 1, symbol, 8, 8);
    }

    // show muted icon if buzzer is muted (shifted further left when the charging
    // icon already occupies the slot immediately left of the battery)
#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(UIColor::warning_txt);
      display.drawXbm(iconX - (charging ? 18 : 9), iconY + 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;

  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _shutdown_init(false), sensors_lpp(200) {
#ifdef LORAFISH_UI
    loRaFishRTC = rtc;
#endif
  }

#if UI_DISCOVER_SCREEN
  bool sendDiscoverRequest() {
    uint8_t cmd_bytes[6];
    cmd_bytes[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1; // DISCOVER_REQ | prefix only
    cmd_bytes[1] = 0xFF;     // Repeaters
    the_mesh.getRNG()->random((uint8_t *) &disc_node_req_tag, 4);  // generate random tag
    memcpy(&cmd_bytes[2], &disc_node_req_tag, 4);
    disc_nodes_count = 0;
    mesh::Packet* req = the_mesh.createControlData(cmd_bytes, sizeof(cmd_bytes));
    if (req) {
      the_mesh.sendZeroHop(req);
      discovery_req_time = millis();
      return true;
    }
    return false;
  }

  void handleDiscoverResponse(const mesh::Packet* packet) {
    if (disc_nodes_count < DISCOVERED_NODES_TABLE_SIZE && memcmp(&packet->payload[2], &disc_node_req_tag, 4) == 0) {
      auto d = &discovered[disc_nodes_count++];
      memcpy(d->pubkey_prefix, &packet->payload[6], 8);
      d->type = packet->payload[0] & 0xF;
      d->snr_out = ((int8_t)packet->payload[1]) / 4.0;
      d->snr_in = radio_driver.getLastSNR();
      ContactInfo* c = the_mesh.lookupContactByPubKey(&packet->payload[6], 8);
      if (c != NULL) {
        strncpy(d->name, c->name, 32);
      } else {
        d->name[0] = 0;
      }
    }
  }
#endif

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
#ifdef LORAFISH_UI
    drawLoRaFish(display, _task);
    if (loRaFishChannelNotice || loRaFishNameNotice || loRaFishMonoDisplayNotice) return 2000;
    // Static pages (menus, stats, pairing) only need an occasional redraw; button
    // presses reset _next_refresh in checkDisplayOn(), so they stay responsive.
    if (loRaFishPage == 0) {
#if defined(LORAFISH_CARDPUTER_25D)
      // Cardputer LCD uses a framebuffer; 100 ms = only 10 FPS and makes
      // the pixel-stepped fish appear jerky. Target ~30 FPS on this board only.
      return loRaFishSimulationMode ? 100 : 33;
#else
      return loRaFishSimulationMode ? 250 : 100;
#endif
    }
    if (loRaFishPage == 2 || loRaFishPage == 4 ||
        loRaFishPage == LORAFISH_DX_CONTACT_PAGE || loRaFishPage == 6) return 1000;  // clock, DX, RF
    return 2000;                                     // static pages
#else
    display.setColor(UIColor::title_bkg);
    display.fillRect(0, 0, display.width(), 12);
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(UIColor::title_txt);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 2);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    if (UIColor::title_bkg == UIColor::window_bkg) {
      display.setColor(UIColor::title_txt);
    } else {
      display.setColor(UIColor::title_bkg);
    }
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 4, 4);
      } else {
        display.fillRect(x, y, 2, 2);
      }
    }

    if (_page == HomePage::FIRST) {
      display.setColor(UIColor::primary_txt);
      #ifdef ENABLE_WIFI_INTERFACE
      // smaller unread message ui for wifi variants
      display.setTextSize(1);
      sprintf(tmp, "Unread Msg: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 22, tmp);
      #else
      // larger unread message ui for non wifi variants
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 22, tmp);
      #endif
      #ifdef UI_SHOW_CLOCK
      display.setTextSize(3);
      uint32_t now = _rtc->getCurrentTime();
      now += (int32_t)_node_prefs->tz_offset * 3600;
      DateTime dt (now);
      sprintf(tmp, "%02d:%02d", dt.hour(), dt.minute());
      display.drawTextCentered(display.width() / 2, 60, tmp);
      display.setTextSize(1);
      sprintf(tmp, "%02d/%02d/%d", dt.day(), dt.month(), dt.year());
      display.drawTextCentered(display.width() / 2, 80, tmp);
      #endif
      #ifdef ENABLE_WIFI_INTERFACE
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 32, tmp);
      #endif
      if (_task->hasConnection()) {
        display.setColor(UIColor::warning_txt);
        display.setTextSize(1);
        #ifdef UI_SHOW_CLOCK
        display.drawTextCentered(display.width() / 2, 110, "< Connected >");
        #else
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");
        #endif
      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(UIColor::warning_txt);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        #ifdef UI_SHOW_CLOCK
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 110, tmp);
        #else
        display.setTextSize(2);
        display.drawTextCentered(display.width() / 2, 43, tmp);
        #endif
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(UIColor::primary_txt);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }

        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;

        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isBluetoothEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setColor(UIColor::secondary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(UIColor::corp_blue);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.setColor(UIColor::primary_txt);
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        display.setColor(UIColor::primary_txt);
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "sat");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "pos");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.setColor(UIColor::secondary_txt);
        display.drawTextLeftAlign(0, y, "alt");
        display.setColor(UIColor::primary_txt);
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.setColor(UIColor::secondary_txt);
        display.print(name);
        display.setColor(UIColor::primary_txt);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
#if UI_DISCOVER_SCREEN
    } else if (_page == HomePage::DISCOVERY) {
      int count = disc_nodes_count;
      display.setColor(UIColor::primary_txt);
      int y = 20;
      for (int i = 0; i < count; i++, y += 11) {
        char name[32];
        auto a = &discovered[i];
        if ((a->name[0] == 0) || !discovery_disp_names) {
          mesh::Utils::toHex(name, a->pubkey_prefix, 4);
        } else {
          strncpy(name, a->name, 32);
        }
        char filtered_name[sizeof(name)];
        char snr_s[12];
        if (strlen(name) <= 8) { // display snr_o
          sprintf(snr_s, "%02.1f>%02.1f", a->snr_out, a->snr_in);
        } else {
          sprintf(snr_s, "%02.1f", a->snr_in);
        }
        int snr_width = display.getTextWidth(snr_s);
        int max_name_width = display.width() - snr_width - 1;
        display.translateUTF8ToBlocks(filtered_name, name, sizeof(filtered_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_name);
        display.setCursor(display.width() - snr_width - 1, y);
        display.print(snr_s);
      }
      if (millis() < discovery_req_time + 5000) {
        return 1000; // more frequent updates just after req
      } else if (count < DISCOVERED_NODES_TABLE_SIZE -1) { // show only 5 sec after last disc
        y = 10 + 11 * DISCOVERED_NODES_TABLE_SIZE;
        display.drawTextCentered(display.width() / 2, y, "discover: " PRESS_LABEL);
      }
#endif
#ifndef UI_NO_HIBERNATE
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::corp_blue);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.setColor(UIColor::warning_txt);
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.setColor(UIColor::secondary_txt);
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
#endif
    }
    return 5000;   // next render after 5000 ms
#endif
  }

  bool handleInput(char c) override {
#ifdef LORAFISH_UI
    if (dismissLoRaFishNotice()) {
      return true;  // The dismissal gesture must not also navigate or act.
    }
    if (loRaFishAdoptionEligible) {
      if (!loRaFishEggApplying && loRaFishEggSession.phase!=LoRaFishEgg::Offered)
        selectLoRaFishEggTank(true);
      return true;
    }
    const bool previous = c == KEY_PREV || c == KEY_LEFT;
    if (!(previous || c == KEY_ENTER || c == KEY_NEXT || c == KEY_RIGHT)) return true;
    if (loRaFishValuePicker.active()) {
      loRaFishValuePicker.move(previous);
      return true;
    }
    if (loRaFishPage == LORAFISH_ADVERT_PAGE) {
      if (previous) loRaFishAdvertMenu.previous();
      else loRaFishAdvertMenu.next();
      return true;
    }
    if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) {
      loRaFishResetConfirmation.toggle();
      return true;
    }
    // All short presses navigate; actions and persistence belong to Select.
    auto move = [previous](uint8_t value, uint8_t count) -> uint8_t {
      return (value + (previous ? count - 1 : 1)) % count;
    };
    if (loRaFishPage == 5) {
      const bool breed = loRaFishCanBreed();
      const uint8_t row = LoRaFishOptions::index(loRaFishOption, loRaFishHasSound, breed, loragotchiDXGuest.present);
      loRaFishOption = LoRaFishOptions::at(move(row, loRaFishOptionCount()), loRaFishHasSound, breed, loragotchiDXGuest.present);
    } else if (loRaFishPage == 12) {
      loRaFishDisplayOption = move(loRaFishDisplayOption, loRaFishDisplayOptionCount());
    } else if (loRaFishPage == 9) {
      loRaFishColorField = move(loRaFishColorField, loRaFishNeedsAppearanceSetup ? 4 : 3);
    } else if (loRaFishPage == 10) {
      loRaFishTimeField = move(loRaFishTimeField, 3);
    } else if (loRaFishPage == 11) {
      loRaFishTimeoutField = move(loRaFishTimeoutField, 3);
    } else if (loRaFishPage == 14) {
      loRaFishSoundField = move(loRaFishSoundField, 4);
    } else if (loRaFishPage == 16) {
      if (!loRaFishGuessComplete) {
        loRaFishGuessHigher = !loRaFishGuessHigher;
        loRaFishGuessResultShown = false;
      }
    } else if (loRaFishPage == LORAFISH_DX_CONTACT_PAGE) {
      if (!loragotchiDXHome.away && !loragotchiDXHome.pending && !loragotchiDXHome.committing) {
        moveLoRaFishDXContact(previous);
      }
    } else {
      loRaFishPage = move(loRaFishPage, LORAFISH_PAGE_COUNT);
    }
    return true;
#else
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
#if UI_DISCOVER_SCREEN
      if (_page == HomePage::DISCOVERY) {
        _task->showAlert("Repeater disc", 800);
      }
#endif
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isBluetoothEnabled()) {  // toggle Bluetooth on/off
        _task->disableBluetooth();
      } else {
        _task->enableBluetooth();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
#if UI_DISCOVER_SCREEN
    if (c == KEY_ENTER && _page == HomePage::DISCOVERY) {
      if (millis() > discovery_req_time + 5000) { // rate limiter
        sendDiscoverRequest();
      }
      return true;
    }
    if (c == KEY_SELECT && _page == HomePage::DISCOVERY) {
      discovery_disp_names = !discovery_disp_names;
      return true;
    }
#endif
#ifndef UI_NO_HIBERNATE
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
#endif
    return false;
#endif
  }};

#ifndef UI_MSG_PREVIEW_SIZE
  #define UI_MSG_PREVIEW_SIZE 78
#endif

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[UI_MSG_PREVIEW_SIZE];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(UIColor::corp_blue);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(UIColor::secondary_txt);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(UIColor::primary_txt);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {

  _display = display;
  _sensors = sensors;
  #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;
#ifdef LORAFISH_UI
  loRaFishMeshNodeName = node_prefs ? node_prefs->node_name : nullptr;
  beginLoRaFishChannelNotice();
  beginLoRaFishNameNotice();
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#ifndef LORAFISH_UI
  buzzer.startup();
#endif
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
#ifdef LORAFISH_UI
  loRaFishColorDisplay = display && display->supportsColor();
  loadLoRaFish();
  loadLoRaFishSettings();
  loadLoRaFishEggReceipt();
  if (!loRaFishColorDisplay && !loRaFishAppearanceLoaded) {
    loRaFishColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishScaleColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishEyeColor=LORAFISH_MONOCHROME_COLOR;
  }
  applyLoRaFishTheme();
  loRaFishMonoDisplayNotice = !loRaFishColorDisplay && !loRaFishMonoDisplayColourKnown;
#ifdef PIN_BUZZER
  buzzer.setVolume(loRaFishBuzzerVolume);
  buzzer.startup();
#endif
  if (display) {
    applyLoRaFishBrightness(*display);
    loRaFishRegisterDisplayActivity(*display);
  }
  loRaFishSyncDXAppearance();
  loRaFishBootMs = millis();
#endif
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
#ifdef LORAFISH_UI
  LoRaFishText::display(_alert, sizeof(_alert), text, sizeof(_alert) - 1);
#else
  snprintf(_alert, sizeof(_alert), "%s", text);
#endif
  _alert_expiry = millis() + duration_millis;
}

void UITask::previewLoRaFishSoundSetting(uint8_t field) {
#if defined(LORAFISH_UI) && defined(PIN_BUZZER)
  const char* preview = "";
  if (field == 0) {
    buzzer.setVolume(loRaFishBuzzerVolume);
    if (loRaFishBuzzerVolume > 0) preview = "SoundTest:d=16,o=6,b=165:c6,g6";
  } else if (field == 1) preview = loRaFishWakeMelodies[loRaFishWakeSound];
  else if (field == 2) preview = loRaFishSleepMelodies[loRaFishSleepSound];
  else if (field == 3) preview = loRaFishMessageMelodies[loRaFishMessageSound];
  if (preview[0]) buzzer.play(preview);
#else
  (void)field;
#endif
}

void UITask::notify(UIEventType t) {
#ifdef LORAFISH_UI
  switch (t) {
    case UIEventType::contactMessage:
    case UIEventType::channelMessage:
    case UIEventType::roomMessage:
      break;
    case UIEventType::ack:
      fish.txAcks++;
      reactLoRaFish(2);
      break;
    case UIEventType::newContactMessage:
      reactLoRaFish(3);
      break;
    default:
      break;
  }
#endif

#if defined(PIN_BUZZER)
#ifdef LORAFISH_UI
switch(t) {
  case UIEventType::contactMessage:
  case UIEventType::channelMessage:
  case UIEventType::roomMessage:
    if (loRaFishMessageSound)
      buzzer.play(loRaFishMessageMelodies[min((uint8_t)3,loRaFishMessageSound)]);
    break;
  case UIEventType::ack:
    buzzer.play("Ack:d=32,o=8,b=120:c");
    break;
  default:
    break;
}
#else
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}

void UITask::onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;

#ifdef LORAFISH_UI
  handleLoRaFishMessage();
#else
  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from.name, text);
  setCurrScreen(msg_preview);
  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) _display->turnOn();
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
#endif

  if (!hasConnection()) notify(UIEventType::contactMessage);
}

void UITask::onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) {
#ifdef LORAFISH_UI
  handleLoRaFishMessage();
#else
  uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, channel_details.name, text);
  setCurrScreen(msg_preview);
  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) _display->turnOn();
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
      _next_refresh = 100;
    }
  }
#endif

  if (!hasConnection()) notify(UIEventType::channelMessage);
}

#ifdef LORAFISH_UI
void UITask::handleLoRaFishMessage() {
  fish.messages++;
  reactLoRaFish(1);
  // Passive counters stay in RAM until the next checkpoint or immediate care save.
  gotoHomeScreen();

  if (_display != NULL && _display->isOn()) {
    loRaFishRegisterDisplayActivity(*_display);
    _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
    _next_refresh = 0;
  }
}
#endif

void UITask::onQueueSizeChanged(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) gotoHomeScreen();
}

void UITask::onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  if (!hasConnection()) notify(UIEventType::newContactMessage);
}

void UITask::onControlDataRecv(const mesh::Packet* packet) {
#if UI_DISCOVER_SCREEN
  if (packet->payload_len >= 14 && (packet->payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP) {
    ((HomeScreen *) home)->handleDiscoverResponse(packet);
  }
#endif
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
#ifdef LORAFISH_UI
  if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE && c != home) cancelLoRaFishReset();
  if (loRaFishPage == LORAFISH_ADVERT_PAGE && c != home) closeLoRaFishAdvertMenu();
  if (c != home) cancelLoRaFishValue();
#endif
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){
#ifdef LORAFISH_UI
  flushLoRaFish();
#endif

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    display.forceFullRefresh();
    display.clear();
    display.endFrame();
    // Power off board including radio, display, GPS and components
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

#ifdef LORAFISH_UI
static bool loRaFishStaticFrameValid = false;
static uint8_t loRaFishStaticFrame[64];

static bool shouldRenderLoRaFish(bool homeVisible, bool alertVisible, bool forced) {
  // Only genuinely static menu pages. Aquarium (even sleeping/dimmed), Stats,
  // clock, pairing, DX and RF/GNSS retain their existing refresh cadence.
  const bool staticPage = loRaFishChannelNotice || loRaFishNameNotice || loRaFishMonoDisplayNotice || loRaFishPage == 5 ||
      (loRaFishPage >= 8 && loRaFishPage <= 18);
  if (!homeVisible || alertVisible || !staticPage || loRaFishAdoptionEligible) {
    loRaFishStaticFrameValid = false;
    return true;
  }
  uint8_t settings[LORAFISH_SETTINGS_SIZE];
  encodeLoRaFishSettings(settings);
  uint8_t frame[] = {
      (uint8_t)loRaFishChannelNotice, (uint8_t)loRaFishNameNotice, (uint8_t)loRaFishMonoDisplayNotice,
      loRaFishPage, (uint8_t)loRaFishOption, loRaFishDisplayOption, loRaFishTimeField,
      loRaFishTimeoutField, loRaFishSoundField, loRaFishColorField,
      (uint8_t)loRaFishValuePicker.active(), (uint8_t)loRaFishValuePicker.key(),
      loRaFishValuePicker.value(), (uint8_t)loRaFishCanBreed(),
      (uint8_t)loRaFishResetConfirmation.resetSelected(), (uint8_t)loRaFishAdvertMenu.selected(),
      loRaFishGuessNumber, loRaFishGuessPrevious, loRaFishGuessRounds, loRaFishGuessScore,
      (uint8_t)loRaFishGuessHigher, (uint8_t)loRaFishGuessResultShown,
      (uint8_t)loRaFishGuessCorrect, (uint8_t)loRaFishGuessComplete,
      (uint8_t)loRaFishColorDisplay,
      (uint8_t)loragotchiDXGuest.present,
      (uint8_t)loragotchiDXGuestReturn.status,
      // UI settings only: elapsed Happy timers must not dirty a static frame.
      settings[0], settings[2], settings[3], settings[4], settings[5], settings[6],
      settings[7], settings[8], settings[9], settings[10], settings[11], settings[12],
      settings[13], settings[14], settings[15], settings[22], settings[23]};
  static_assert(sizeof(frame) <= sizeof(loRaFishStaticFrame), "static frame key capacity");
  const bool changed = forced || !loRaFishStaticFrameValid ||
      memcmp(frame, loRaFishStaticFrame, sizeof(frame)) != 0;
  memcpy(loRaFishStaticFrame, frame, sizeof(frame));
  loRaFishStaticFrameValid = true;
  return changed;
}
#endif

// Alert popup, drawn over whatever page is showing. Used by both the full-frame
// and the banded render paths.
static void drawUIAlertBox(DisplayDriver& display, const char* alert) {
  display.setTextSize(1);
  int y = display.height() / 3;
  int p = display.height() / 32;
  display.setColor(UIColor::popup_bkg);
  display.fillRect(p, y, display.width() - p*2, y);
  display.setColor(UIColor::popup_txt);  // draw box border
  display.drawRect(p, y, display.width() - p*2, y);
  display.drawTextCentered(display.width() / 2, y + p*3, alert);
}

void UITask::loop() {
#ifdef LORAFISH_UI
  // A hidden/interrupted prompt must never leave a destructive choice armed.
  if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE &&
      (!_display || !_display->isOn() || curr != home || millis() < _alert_expiry)) {
    cancelLoRaFishReset();
    _next_refresh = 0;
  }
  serviceLoRaFishAdoption(curr == home && !loRaFishNameNotice && !loRaFishChannelNotice && !loRaFishMonoDisplayNotice);
  tickLoRaFish();
#if defined(LORAFISH_SERIAL_TEST)
  {
    // Test-only commands over the USB serial port, one per line:
    // next, enter, long, double, advert, page. Not present in normal builds.
    static char cmdBuf[24];
    static uint8_t cmdLen = 0;
    while (Serial.available()) {
      char ch = (char)Serial.read();
      if (ch == '\r') continue;
      if (ch != '\n') {
        if (cmdLen < sizeof(cmdBuf) - 1) cmdBuf[cmdLen++] = ch;
        continue;
      }
      cmdBuf[cmdLen] = '\0';
      cmdLen = 0;
      if (!strcmp(cmdBuf, "next") && curr) {
        curr->handleInput(KEY_NEXT);
      } else if (!strcmp(cmdBuf, "enter") && curr) {
        curr->handleInput(KEY_ENTER);
      } else if (!strcmp(cmdBuf, "long")) {
        char c = handleLongPress(KEY_ENTER);
        if (c && curr) curr->handleInput(c);
      } else if (!strcmp(cmdBuf, "double")) {
        char c = handleDoubleClick(KEY_PREV);
        if (c && curr) curr->handleInput(c);
      } else if (!strcmp(cmdBuf, "advert")) {
        bool ok = the_mesh.advert();
        Serial.printf("[TEST] advert %s\n", ok ? "sent" : "failed");
      } else if (!strcmp(cmdBuf, "page")) {
        Serial.printf("[TEST] page %u\n", (unsigned)loRaFishPage);
      } else {
        Serial.printf("[TEST] unknown '%s'\n", cmdBuf);
      }
      _next_refresh = 0;
    }
  }
#endif
  loRaFishObserveTravelTransitions();
  if (loragotchiDXHome.returned) {
    if (!loRaFishReturnNavigationHandled) {
      loRaFishResetConfirmation.cancel();
      loRaFishAdvertMenu.close();
      loRaFishValuePicker.cancel();
      loRaFishReturnNavigationHandled=true;
      loRaFishPage=0;
      _next_refresh=0;
    }
  } else {
    loRaFishReturnNavigationHandled=false;
  }

  // OLED off does not pause care. Actual meals (including sound/waste) are
  // limited to one per hour; skipped thresholds never accumulate a backlog.
  const bool canAnimateMeal = _display && _display->isOn() &&
                              loRaFishPage == 0 && !loRaFishFeeding;
  if (consumeLoRaFishMeals(canAnimateMeal)) playFeedPip();

  if (tickLoRaFishPoop()) {
    playCleanMelody();
  }

  const bool sleepingNow=loRaFishIsSleeping();
  if (!loRaFishSoundSleepStateKnown) {
    loRaFishSoundWasSleeping=sleepingNow;
    loRaFishSoundSleepStateKnown=true;
  } else if (sleepingNow!=loRaFishSoundWasSleeping) {
    loRaFishSoundWasSleeping=sleepingNow;
#ifdef PIN_BUZZER
    if (sleepingNow && loRaFishSleepSound)
      buzzer.play(loRaFishSleepMelodies[min((uint8_t)2,loRaFishSleepSound)]);
    else if (!sleepingNow && loRaFishWakeSound)
      buzzer.play(loRaFishWakeMelodies[min((uint8_t)3,loRaFishWakeSound)]);
#endif
  }

  if (fish.ageMinutes >= LORAFISH_EGG_MINUTES &&
      fish.ageMinutes < LORAFISH_EGG_MINUTES + 2 &&
      !loRaFishHatchMelodyPlayed) {
    playHatchMelody();
    loRaFishHatchMelodyPlayed = true;
  }
#endif
  char c = 0;
#if defined(M5STACK_CARDPUTER) && defined(LORAFISH_UI)
  // Dedicated Cardputer keyboard path. Fn + ; / . / , / / are the hardware arrows.
  // W/A/S/D also work without holding Fn.
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange()) {
    const auto& keys = M5Cardputer.Keyboard.keysState();
    bool up=keys.up, down=keys.down, left=keys.left, right=keys.right;
    for (char k : keys.word) {
      if (k=='w' || k=='W') up=true;
      if (k=='s' || k=='S') down=true;
      if (k=='a' || k=='A') left=true;
      if (k=='d' || k=='D') right=true;
    }
    if (keys.esc || keys.backspace) {
      c = handleDoubleClick(KEY_PREV);
    } else if (keys.enter || keys.space) {
      c = handleLongPress(KEY_ENTER);
    } else if (right || down) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (left || up) {
      c = checkDisplayOn(KEY_PREV);
    }
  }
#endif
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
#ifdef LORAFISH_UI
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
#endif
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  #ifdef UI_HAS_NAV_INPUT
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
#ifdef LORAFISH_UI
    c = handleLongPress(KEY_ENTER);
#else
    display.turnOff();
#endif
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_SELECT);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
  #else
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
  #endif  
#endif
#if defined(UI_HAS_ROTARY_INPUT)
  RotaryInputEvent rotaryEv = rotary_input.poll();
  if (c == 0 && _display != NULL && _display->isOn()) {
    if (rotaryEv == RotaryInputEvent::Next) {
      c = KEY_NEXT;
    } else if (rotaryEv == RotaryInputEvent::Prev) {
      c = KEY_PREV;
    }
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
#ifdef LORAFISH_UI
    if (_display != NULL) loRaFishRegisterDisplayActivity(*_display);
#endif
    #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

#ifdef LORAFISH_UI
  if (!_display || !_display->isOn()) loRaFishStaticFrameValid = false;
#endif
  if (_display != NULL && _display->isOn()) {
#ifdef LORAFISH_UI
    const bool sleeping=loRaFishIsSleeping();
    if (!loRaFishTimeoutSleepStateKnown || sleeping!=loRaFishTimeoutSleepState) {
      loRaFishTimeoutSleepState=sleeping;
      loRaFishTimeoutSleepStateKnown=true;
      _auto_off=loRaFishScreenTimeoutMs() ? millis()+loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
    }
    if (loRaFishBrightnessNeedsApply) {
      applyLoRaFishBrightness(*_display);
      loRaFishScreenDimmed=false;
      loRaFishBrightnessNeedsApply=false;
    }
    const bool shouldDim=loRaFishScreenTimeoutMs()==0 &&
        millis()-loRaFishLastActivityMs>=loRaFishAlwaysOnDimValues[min((uint8_t)2,loRaFishAlwaysOnDimSetting)];
    if (shouldDim!=loRaFishScreenDimmed) {
      loRaFishScreenDimmed=shouldDim;
      applyLoRaFishBrightness(*_display,shouldDim);
    }
    if (millis() >= _next_refresh && curr &&
        !shouldRenderLoRaFish(curr == home, millis() < _alert_expiry, _next_refresh == 0)) {
      _next_refresh = millis() + 2000;
    }
#endif
    if (millis() >= _next_refresh && curr) {
      int delay_millis = 0;
      bool banded = false;
#ifdef LORAFISH_STRIP_REDRAW
      // The aquarium is the animated page that flickered on a full-screen clear,
      // so it is drawn in bands: each band is cleared and pushed on its own.
      banded = (loRaFishPage == 0) && _display->stripRows() > 0;
#endif
      if (banded) {
        const int rows = _display->stripRows();
        const int panelRows = _display->physicalHeight();
        for (int y0 = 0; y0 < panelRows; y0 += rows) {
          _display->beginStrip(y0);
          _display->startFrame();
          delay_millis = curr->render(*_display);
          if (millis() < _alert_expiry) drawUIAlertBox(*_display, _alert);
          _display->endStrip();
        }
      } else {
        _display->startFrame();
        delay_millis = curr->render(*_display);
        if (millis() < _alert_expiry) drawUIAlertBox(*_display, _alert);  // render alert popup
        _display->endFrame();
      }
      if (millis() < _alert_expiry) {
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      #ifdef LORAFISH_UI
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif
    }
#endif
    #ifdef LORAFISH_UI
    if (loRaFishScreenTimeoutMs() != 0 && millis() > _auto_off) {
#else
    if (millis() > _auto_off) {
#endif
#ifdef LORAFISH_UI
      if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) cancelLoRaFishReset();
#endif
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      if(!board.isExternalPowered()) {
        if (_display != NULL) {
          _display->startFrame();
          _display->setTextSize(2);
          _display->setColor(UIColor::warning_txt);
          _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
          _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
          _display->endFrame();
          if (_display->isEink() == false) { delay(3000); }
        }
        shutdown();
      }
    }
    next_batt_chck = millis() + 8000;
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
#ifdef LORAFISH_UI
      if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) cancelLoRaFishReset();
#endif
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
#ifdef LORAFISH_UI
    loRaFishRegisterDisplayActivity(*_display);
  _auto_off = loRaFishScreenTimeoutMs() ? millis() + loRaFishScreenTimeoutMs() : 0xFFFFFFFFUL;
#else
  _auto_off = millis() + AUTO_OFF_MILLIS;
#endif   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  // Keep MeshCore's startup rescue gesture.
  if (millis() - ui_started_at < 8000) {
    the_mesh.enterCLIRescue();
    return 0;
  }

#ifdef LORAFISH_UI
  if (!_display || !_display->isOn()) {
    checkDisplayOn(0);
    return 0;
  }
  if (curr == home && dismissLoRaFishNotice()) {
    checkDisplayOn(0);
    return 0;
  }
  if (curr != home) return c;  // Select belongs to the visible non-pet screen.
  if (millis() < _alert_expiry) return 0;
  checkDisplayOn(0);
  if (loRaFishAdoptionEligible) { closeLoRaFishAdoption();return 0; }
  if (loRaFishValuePicker.active()) {
    selectLoRaFishValue();
    if (loRaFishPage == 14) previewLoRaFishSoundSetting(loRaFishSoundField);
    checkDisplayOn(0);  // Apply the newly selected timeout to the inactivity timer.
    return 0;
  }
  if (loRaFishPage == LORAFISH_ADVERT_PAGE) {
    const bool visible = _display && _display->isOn() && curr == home &&
                         millis() >= _alert_expiry;
    const uint32_t now = millis();
    checkDisplayOn(0);  // A wake gesture must not transmit.
    const auto result = loRaFishAdvertMenu.activate(now, visible,
        [](bool flood) { return the_mesh.advert(flood); });
    if (result == AdvertMenu::Result::Sent) {
      closeLoRaFishAdvertMenu();
      notify(UIEventType::ack);
      showAlert("Advert queued!", 1000);
    } else if (result == AdvertMenu::Result::Back) {
      closeLoRaFishAdvertMenu();
    } else if (result == AdvertMenu::Result::RateLimited) {
      char message[24];
      snprintf(message, sizeof(message), "Wait %lus", (unsigned long)loRaFishAdvertMenu.waitSeconds(now));
      showAlert(message, 1000);
    } else if (result == AdvertMenu::Result::Failed) {
      showAlert("Advert failed", 1000);
    }
    _next_refresh = 0;
    return 0;
  }
  if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) {
    const bool visible = _display && _display->isOn() && curr == home &&
                         millis() >= _alert_expiry;
    const bool reset = loRaFishResetConfirmation.confirm(visible);
    cancelLoRaFishReset();
    checkDisplayOn(0);  // Refresh the inactivity timer; a sleeping screen only wakes.
    if (reset) {
      resetLoRaFishPet();
      loRaFishPage = loRaFishNeedsAppearanceSetup ? 9 : 0;
      loRaFishOption = LoRaFishOption::Advert;
      loRaFishHatchMelodyPlayed = false;
      if (!loRaFishNeedsAppearanceSetup) showAlert("New egg!", 900);
    }
    _next_refresh = 0;
    return 0;
  }
  if (_display != NULL && _display->isOn()) loRaFishRegisterDisplayActivity(*_display);
  if (loRaFishPage == 0) {
    if (loRaFishPoopCount > 0 && !loRaFishCleaning) {
      startLoRaFishCleaning();
      showAlert("Cleaning tank!", 450);
      _next_refresh = 0;
    }
    // Consume aquarium long press so MeshCore cannot treat it as another action.
    return 0;
  }

  if (loRaFishPage == 7) {
    toggleGPS();
    _next_refresh = 0;
    return 0;
  }

  if (loRaFishPage == 9) {
    if (loRaFishColorField == 3 && loRaFishNeedsAppearanceSetup) {
      loRaFishNeedsAppearanceSetup = false;
      loRaFishPage = 0;
      loRaFishSyncDXAppearance();
      saveLoRaFishSettings();
      saveLoRaFish();
      showAlert("Fish ready!", 700);
    } else {
      const LoRaFishSetting fields[] = {LoRaFishSetting::Scales, LoRaFishSetting::Body, LoRaFishSetting::Eyes};
      openLoRaFishValue(fields[loRaFishColorField % 3]);
    }
    _next_refresh = 0;
    return 0;
  }
  if (loRaFishPage == 10) {
    const LoRaFishSetting fields[] = {LoRaFishSetting::SleepHour, LoRaFishSetting::WakeHour, LoRaFishSetting::UtcOffset};
    openLoRaFishValue(fields[loRaFishTimeField]);
    return 0;
  }
  if (loRaFishPage == 11) {
    const LoRaFishSetting fields[] = {LoRaFishSetting::TimeoutAwake, LoRaFishSetting::TimeoutAsleep, LoRaFishSetting::DimAfter};
    openLoRaFishValue(fields[loRaFishTimeoutField]);
    return 0;
  }
  if (loRaFishPage == 16) {
    submitLoRaFishGuess();
    _next_refresh = 0;
    return 0;
  }
#ifdef PIN_BUZZER
  if (loRaFishPage == 14) {
    const LoRaFishSetting fields[] = {LoRaFishSetting::Volume, LoRaFishSetting::WakeSound,
        LoRaFishSetting::SleepSound, LoRaFishSetting::MessageSound};
    openLoRaFishValue(fields[loRaFishSoundField]);
    return 0;
  }
#endif
  if (loRaFishPage == 12) {
    if (loRaFishDisplayOption == 0) {
      loRaFishTimeoutField = 0;
      loRaFishPage = 11;
    } else if (loRaFishDisplayOption == 1) {
      loRaFishPage = 13;
      openLoRaFishValue(LoRaFishSetting::Brightness);
    } else if (loRaFishDisplayOption == 2) {
      loRaFishPage = 8;
      openLoRaFishValue(LoRaFishSetting::Theme);
    } else if (loRaFishDisplayOption == 3 && loRaFishColorDisplay) {
      loRaFishColorField = 0;
      loRaFishPage = 9;
    } else if (loRaFishDisplayOption == 3) {
      loRaFishPage = 15;
      openLoRaFishValue(LoRaFishSetting::MonoDisplayColour);
    } else if (loRaFishDisplayOption == 4) {
      loRaFishPage = 15;
      openLoRaFishValue(LoRaFishSetting::Rendering);
    }
    _next_refresh = 0;
    return 0;
  }

  if (loRaFishPage == 4) {
    // The overview stays in the main-page route; only Select enters contact browsing.
    loRaFishPage = LORAFISH_DX_CONTACT_PAGE;
    _next_refresh = 0;
    return 0;
  }
  if (loRaFishPage == LORAFISH_DX_CONTACT_PAGE) {
    ContactInfo c;
    uint32_t ack=0,to=0;
    if(loragotchiDXHome.away || loragotchiDXHome.committing) {
      // Recall the exact contact used for the visit, even if ARRIVED confirmation is still pending.
      // This provides a safe recovery path if the final confirmation is lost.
      if (!loRaFishDXDestination(c)) {
        showAlert("Tank not in contacts", 900);
        return 0;
      }
      showAlert(the_mesh.recallLoRaFish() ? "Recall queued" : "No active visit",700);
    } else if(!loragotchiDXHome.pending && !loragotchiDXHome.committing) {
      if (!loRaFishDXGetContact(c)) { showAlert("No fish tanks", 900); return 0; }
      // Sleeping fish stay home.  Recall remains available above so an
      // already-away fish can always be recovered at night.
      if (loRaFishIsSleeping()) {
        showAlert("Fish is sleeping",900);
        Serial.println("[LFG1 PEER] SEND BLOCKED: local fish is sleeping");
        _next_refresh=0;
        return 0;
      }
      if(!loragotchiDXHome.fishid) {
        loragotchiDXHome.fishid =
            0x4C460000UL ^
            ((uint32_t)fish.ageMinutes << 8) ^
            (uint32_t)fish.rfEvents ^
            (uint32_t)millis();
        if(!loragotchiDXHome.fishid) loragotchiDXHome.fishid=0x4C460001UL;
      }
      loragotchiDXHome.txid=(uint32_t)(millis() ^ loragotchiDXHome.fishid);
      loragotchiDXHome.destinationIndex=loRaFishDXContactIndex;
      LoRaFishText::rawPrefix(loragotchiDXHome.destinationName,
          sizeof(loragotchiDXHome.destinationName), c.name, sizeof(c.name));
      char msg[112]; snprintf(msg,sizeof(msg),"LFG1|REQ|%lu|%lu|%u|%s",
        (unsigned long)loragotchiDXHome.txid,(unsigned long)loragotchiDXHome.fishid,
        (unsigned)fish.evolution,loRaFishProtocolName());
      int r=the_mesh.sendMessage(c,the_mesh.getRTCClock()->getCurrentTime(),0,msg,ack,to);
      loragotchiDXHome.pending=(r>=0);
      if(loragotchiDXHome.pending) {
        memcpy(loragotchiDXHome.destinationKey,c.id.pub_key,PUB_KEY_SIZE);
        loragotchiDXHome.destinationKnown=true;
        memcpy(loRaFishDXDestinationKey, c.id.pub_key, PUB_KEY_SIZE);
        loRaFishDXDestinationKnown = true;
        the_mesh.trackLoRaFishRequest(msg,to);
        loragotchiDXSave();
      }
      Serial.printf("[LFG1 M2] REQ TX result=%d to='%s'\n",r,c.name);
      showAlert(r>=0?"DX request sent":"DX send failed",800);
    }
    _next_refresh=0; return 0;
  }

  if (loRaFishPage == 5) {
    if (!LoRaFishOptions::available(loRaFishOption, loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present)) {
      loRaFishOption = LoRaFishOption::Advert;
      return 0;  // A disappearing optional action must not select a different action.
    }
    switch (loRaFishOption) {
      case LoRaFishOption::ReturnGuest:
        showAlert(the_mesh.returnLoRaFishGuest()?"Return queued":"No visitor owner",1100);break;
      case LoRaFishOption::Advert:
        loRaFishAdvertMenu.open(); loRaFishPage = LORAFISH_ADVERT_PAGE; break;
      case LoRaFishOption::Guess:
        startLoRaFishGuessGame(); loRaFishPage = 16; break;
      case LoRaFishOption::Reset:
        loRaFishResetConfirmation.open(); loRaFishPage = LORAFISH_RESET_CONFIRM_PAGE; break;
      case LoRaFishOption::Display:
        loRaFishDisplayOption = 0; loRaFishPage = 12; break;
      case LoRaFishOption::Sound:
#ifdef PIN_BUZZER
        loRaFishSoundField = 0; loRaFishPage = 14;
#endif
        break;
      case LoRaFishOption::Schedule:
        loRaFishTimeField = 0; loRaFishPage = 10; break;
      case LoRaFishOption::RF: loRaFishPage = 6; break;
      case LoRaFishOption::GNSS: loRaFishPage = 7; break;
      case LoRaFishOption::Breed:
        if (breedLoRaFishWithGuest()) {
          loRaFishOption = LoRaFishOption::Guess;
          showAlert("Egg saved", 1100);
        }
        break;
      default: break;
    }
    _next_refresh = 0;
    return 0;
  }

  // Protected design: long press outside Options cannot reset the pet.
  return 0;
#endif

  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
#ifdef LORAFISH_UI
  if (!_display || !_display->isOn()) {
    checkDisplayOn(0);
    return 0;
  }
  checkDisplayOn(0);
  if (curr == home && dismissLoRaFishNotice()) {
    return 0;
  }
  if (curr != home) {
    gotoHomeScreen();
    return 0;
  }
  if (loRaFishAdoptionEligible) { closeLoRaFishAdoption();return 0; }
  if (loRaFishValuePicker.active()) {
    cancelLoRaFishValue();
    return 0;
  }
  if (loRaFishPage == LORAFISH_ADVERT_PAGE) {
    closeLoRaFishAdvertMenu();
    return 0;
  }
  if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) {
    cancelLoRaFishReset();
    return 0;
  }
  switch (loRaFishPage) {
    case LORAFISH_DX_CONTACT_PAGE: loRaFishPage = 4; break;
    case 6: returnToLoRaFishOptions(LoRaFishOption::RF); break;
    case 7: returnToLoRaFishOptions(LoRaFishOption::GNSS); break;
    case 10: returnToLoRaFishOptions(LoRaFishOption::Schedule); break;
    case 12: returnToLoRaFishOptions(LoRaFishOption::Display); break;
    case 14: returnToLoRaFishOptions(LoRaFishOption::Sound); break;
    case 16: returnToLoRaFishOptions(LoRaFishOption::Guess); break;
    case 8: loRaFishDisplayOption = 2; loRaFishPage = 12; break;
    case 9: loRaFishDisplayOption = 3; loRaFishPage = 12; break;
    case 11: loRaFishDisplayOption = 0; loRaFishPage = 12; break;
    case 13: loRaFishDisplayOption = 1; loRaFishPage = 12; break;
    case 15: loRaFishDisplayOption = loRaFishColorDisplay ? 4 : (loRaFishValuePicker.key() == LoRaFishSetting::MonoDisplayColour ? 3 : 4); loRaFishPage = 12; break;
    default: loRaFishPage = 0; loRaFishOption = LoRaFishOption::Advert; break;
  }
  _next_refresh = 0;
  return 0;
#else
  checkDisplayOn(c);
  return c;
#endif
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
#ifdef LORAFISH_UI
  if (loRaFishChannelNotice || loRaFishNameNotice) {
    if (_display && _display->isOn() && curr == home) dismissLoRaFishNotice();
    checkDisplayOn(0);
    return 0;
  }
#endif
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  }
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::playHatchMelody() {
#ifdef PIN_BUZZER
  buzzer.play("hatch:d=16,o=6,b=150:c,e,g,c7");
#endif
}

void UITask::playFeedPip() {
#ifdef PIN_BUZZER
  buzzer.play("feed:d=32,o=8,b=120:c");
#endif
}

void UITask::playCleanMelody() {
#ifdef PIN_BUZZER
  // Short cheerful ascending clean-up jingle.
  buzzer.play("clean:d=16,o=7,b=180:c,e,g,c8");
#endif
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
