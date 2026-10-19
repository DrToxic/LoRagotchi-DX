## Getting Started with LoRaGotchi DX

Thank you for downloading and trying our firmware! LoRaGotchi DX is a virtual pet running as a MeshCore BLE companion. The firmware uses the device's LoRa radio and display for the aquarium, and can exchange visiting fish with compatible LoRaGotchi DX peers over the mesh.

It's actually built straight on the stock meshcore (1.17.1 at the time of writing) firmware with the idea that, you can flash to LoRagotchi-DX without losing your settings, channels, contacts etc. We've even flashed it via the meshcore.io flasher.

For supported boards and PlatformIO build commands, see the [README](README.md#supported-loragotchi-targets-in-this-tree). This guide describes the LoRaFish companion UI and behavior.

## First Use

0. Bringing a device already flashed with a MeshCore firmware is perfectly fine ☑️, though we do recommend making a backup first. don't erase when flashing to keep your settings, channels & ID
1. Flash the LoRaGotchi DX build for your board. The NRF52 builds produce a UF2 file; see its README instructions. Other target build commands are listed there.
2. Start the device and connect to it from the MeshCore app as a BLE companion. Use the PIN shown on the device's pairing screen when the app requests one.
3. Set the device clock from the app. LoRaGotchi uses MeshCore's clock for the displayed local time and scheduled sleep. If the clock has not been set to a valid date, the UI falls back to build time plus uptime; this fallback is not a substitute for setting the clock.
4. Use **Options → Schedule & time** to set your local UTC offset and sleep/wake hours.
5. On a color display, choose the starting Scales, Body/Border, and Eyes colors on the new-fish screen, then long-press to start. Monochrome displays skip this screen and start with monochrome traits.

Pet age, RF-event count, happiness, and other pet state are saved across restarts. Display and schedule settings are also saved. DX travel is temporary: after a reboot, the local fish returns home and any visiting-fish slot is cleared.

## Button Controls

Most nodes use a single user button. Hold it for about one second for a long-press. A click, double-click, long-press, and triple-click have different meanings depending on the screen.

| Gesture | General action |
| --- | --- |
| Short click | Advance to the next main screen, or change/select an item in a menu. |
| Long-press | Open the selected Options item or perform the current screen's action. |
| Double-click | Move to the next field in Schedule or Timeout; return to a parent menu on supported submenus. |
| Triple-click | Toggle global buzzer quiet mode when the board has a buzzer. |

During the first eight seconds after startup, long-press is reserved for MeshCore's CLI rescue gesture. After that, the LoRaGotchi menu actions are available.

When the display has turned off, the first click wakes the screen. On the aquarium, long-press starts cleaning if 💩 is present.

The main screens advance in this order:

1. Aquarium
2. Stats
3. Clock
4. Phone pairing
5. DX Visit
6. Options
7. RF Monitor
8. GNSS

Short-click on **Options** moves the selection. Long-press opens the selected item. Short-click cycles a value within a value editor; double-click generally advances to the next field; long-press returns to the parent menu.

## Options and Display

The main **Options** menu contains:

- **Display**: screen timeout, brightness, firmware theme, and aquarium rendering speed. Color displays also offer fish color settings.
- **Reset pet**: opens a confirmation screen with **Cancel** selected. Nothing is reset merely by opening it. See the reset controls below.
- **RF monitor**: view recent received signal information and activity.
- **GNSS monitor**: inspect GNSS status; long-press toggles GNSS where supported.
- **Schedule & time**: configure sleep, wake, and UTC offset.
- **Guess game**: play five rounds of higher/lower to earn happiness when mesh activity is quiet.
- **Sound**: configure buzzer volume, wake/sleep cues, and message tone on buzzer-equipped builds.
- **Send advert**: choose a local or flood advertisement, or go Back without sending.
- **Breed with visitor**: appears when the local fish and a visiting fish are both Adult or Elder and no offspring egg is already pending.

### Resetting your pet safely

Open **Options → Reset pet** with a long-press. The screen asks **"Start a new egg?"**
and warns **"Pet progress & eggs will be lost forever."**

- **Cancel** is selected every time the screen opens. Long-press to cancel.
- Short-click to select **Yes, reset pet**, then release and long-press to confirm.
- Double-click cancels from either choice and returns to Options.
- If the display turns off or another screen/alert interrupts the prompt, the
  confirmation is cancelled. Waking the display does not reset the pet.

Confirming starts a new egg at generation 1, clears pet progress and any pending
offspring egg, and cannot be undone. Color displays then open the starting-trait
picker; monochrome displays start with monochrome traits. Display, schedule, and
MeshCore settings, contacts, and channels are kept. Cancelling leaves your pet
untouched; normal gameplay continues while the prompt is open.

### Sending an advert

Open **Options → Send advert** with a long-press. Opening the submenu does not
send anything. Short-click selects an item; long-press activates it:

- **Local (zero-hop)**: advertise to nearby nodes without repeater forwarding.
- **Flood (multi-hop)**: advertise through the mesh using the configured default
  scope and path-hash mode, just like a flood advert from the MeshCore app.
- **Back**: return to Options without sending. Double-click also goes back.

Once MeshCore accepts an advert for sending, the submenu closes automatically,
**Send advert** stays selected in Options, and **Advert queued!** is shown briefly.
This confirms queuing, not reception by another node. A sleeping display's first
long-press only wakes it; it never sends an advert.

Local and flood adverts share the existing one-minute UI cooldown. If it is too
soon, the remaining wait is shown and the submenu stays open; Back still works.
A failed attempt stays in the submenu for retry and does not start the cooldown.
Both advert types keep your existing name and location-advertising policy.

### Guess game

Open **Options → Guess game**. Short-click switches between **Higher** and **Lower**; long-press guesses whether the next number will be higher or lower than the current number. Play five rounds. Each correct guess adds one happiness point; wrong guesses do not reduce happiness or count as mesh activity. At the end, long-press starts another game and double-click returns to Options.

### Display submenu

Open **Options → Display**. Short-click selects an item and long-press opens it. Double-click returns to Options.

- **Screen timeout** has independent **Fish Awake** and **Fish Asleep** settings. Each can be set to 15 seconds, 30 seconds, 1 minute, 2 minutes, 3 minutes, or Always On. The schedule determines which timeout is active.
- **Always-On-Dim** selects when an always-on screen dims after inactivity: 15, 30, or 60 seconds. It applies when the active awake/asleep timeout is Always On. A button press restores the selected brightness.
- **Brightness** cycles through 25%, 50%, 75%, and 100%. The default is 100%. Brightness is controlled by OLED contrast on the V4 and backlight level on color TFT targets.
- **Firmware theme** cycles the available themes. Color TFTs have Midnight, Paper, Lagoon, and Sunset. Monochrome OLEDs use the dark and paper themes.
- **Game rendering** selects the aquarium redraw interval. **Smooth** uses the original 100 ms interval and is the default; **Simulation** redraws every 250 ms. This affects the aquarium only; other screens keep their existing refresh rates. The selection is saved across restarts. Short-click changes the mode; long-press returns to Display.

Fish color traits are chosen at first run or after **Reset pet**, not changed mid-generation. On the starting-trait screen, short-click cycles the current trait, double-click switches between Scales, Body/Border, and Eyes, and long-press confirms the starting fish. Color displays offer Coral, Tangerine, Gold, Mint, Lagoon, Orchid, and Mono for each trait. Monochrome displays skip the picker and begin with all three traits set to Mono.

### Sound menu

On buzzer-equipped LoRaGotchi targets, open **Options → Sound**. Short-click changes the selected setting and previews non-Off choices; double-click moves to the next row; long-press returns to Options. The settings are:

- **Volume**: Off, 25%, 50%, 75%, or 100%. Volume changes the buzzer's PWM duty; triple-click remains a separate global quiet-mode toggle.
- **Wake alarm**: Off, Chirp, Rising, or Alarm. The selected original short motif plays when the scheduled sleep state changes to awake.
- **Sleep cue**: Off, Drift, or Soft bell. The selected motif plays when the fish enters scheduled sleep.
- **Message tone**: Off, Ping, Double, or Arpeggio for incoming messages.

The default is 100% volume, wake alarm Off, sleep cue Off, and Ping for incoming messages. The short motifs are original compositions rather than copied Nokia, Tamagotchi, or other device ringtones. Sound options are hidden on boards without a buzzer; T096 and T114 builds do not currently define buzzer hardware.

## Pet Behavior

### Growth and age

A new pet starts as an egg. It hatches after five minutes. The fish then progresses according to age:

- **Fry**: from hatching until one hour old.
- **Young**: one hour to six hours.
- **Adult**: six hours to three days.
- **Elder**: three days until the end of its lifespan.
- **Memorial**: shown at seven days; use **Options → Reset pet** to start over.

The age clock continues while the display is off and while the fish sleeps. The aquarium animation is only a presentation; turning the display off does not pause the pet.

### Breeding and generations

When an Adult or Elder local fish is hosting an Adult or Elder visitor, **Breed with visitor** appears in Options. Long-press it to produce one egg. For each of the three traits, the offspring randomly inherits that trait from either parent; Mono is inherited like any other trait. The visitor may remain until recalled, but a second egg cannot be produced while one is pending.

The egg is deliberately unmarked so its colors are not revealed. It appears as a small 🥚 indicator in the aquarium footer while the current fish lives. Generation is shown on the Stats screen. When the parent reaches the seven-day Memorial stage, the egg moves beside the gravestone on a random side. After five minutes there, it hatches into the next generation. Only the current generation and pending egg traits are saved; no family tree is retained. **Reset pet** clears a pending egg and restarts generation 1.

### Mesh activity, meals, and hygiene

MeshCore interactions such as received messages, acknowledgments, and new contacts contribute to the RF-event counter. Each group of ten events earns a meal while the fish is awake. Meals increase happiness and schedule waste to appear 30 to 90 seconds later. Happiness is capped at 100. The buzzer, when fitted and enabled, provides hatch, feeding, and cleaning sounds.

Waste accumulates up to three pieces. To clean the tank, long-press while on the aquarium screen. Cleaning clears the waste and adds a small happiness bonus. Cleaning is available only when waste is present.

### Sleep schedule

The default schedule is sleep at 21:00 and wake at 07:00. Sleep and wake are based on the device clock plus the manually configured UTC offset. Offset increments are 15 minutes, from UTC-12:00 through UTC+14:00. There is no automatic timezone or daylight-saving detection; update the offset when local rules change. Setting sleep and wake to the same hour keeps the fish awake all day.

During scheduled sleep the fish displays as sleeping. RF events are still counted, but meal thresholds reached while asleep are consumed without feeding, so meals do not accumulate and trigger all at once on waking. A sleeping fish cannot start a DX visit, though a fish already away can still be recalled.

## DX Visits

Open **DX Visit** to choose a compatible LoRaGotchi DX peer from the contact list. Short-click cycles through contacts. Long-press sends a visit request when the local fish is awake. The peers exchange request, acknowledgment, commit, and arrival-confirmation messages. Once accepted, the visitor appears in the other aquarium while the host fish continues its own behavior.

When your fish is away, long-press recalls it home. Double-click on DX Visit opens Options. A sleeping fish cannot begin a new visit. If a visit is interrupted or the device reboots, DX travel state is cleared and the local fish is restored at home.

Color-capable displays render visitor Scales, Body/Border, and Eyes traits independently. Mono traits use the display's foreground color; monochrome displays render every trait in their foreground color. A fish returning from a monochrome host briefly animates back into its saved home colors on a color display.

## Display and Hardware Notes

The UI screens are Aquarium, Stats, Clock, Phone pairing, DX Visit, Options, RF Monitor, and GNSS. GNSS details depend on the board and whether a receiver is fitted.

Fish color controls and colored visitor rendering require a color display; monochrome OLED targets remain monochrome. Monochrome and Colour tanks know about each other and act & display accordingly

Use the companion BLE connection for MeshCore functions such as messages, contacts, and setting device time. LoRaGotchi DX adds the pet UI and DX visit behavior on top of that companion firmware.
