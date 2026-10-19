#include <gtest/gtest.h>
#include "../../examples/companion_radio/ui-new/AdvertMenu.h"

using Choice = AdvertMenu::Choice;
using Result = AdvertMenu::Result;

TEST(AdvertMenu, OpeningAndBrowsingNeverSends) {
  AdvertMenu menu;
  int sends = 0;
  auto send = [&](bool) { ++sends; return true; };
  EXPECT_EQ(menu.activate(0, true, send), Result::Ignored);
  menu.open();
  EXPECT_EQ(menu.selected(), Choice::Local);
  menu.previous();
  EXPECT_EQ(menu.selected(), Choice::Back);
  menu.next();
  EXPECT_EQ(menu.selected(), Choice::Local);
  EXPECT_EQ(sends, 0);
}

TEST(AdvertMenu, LocalAndFloodSelectTheirOwnTransport) {
  AdvertMenu menu;
  menu.open();
  EXPECT_EQ(menu.activate(100, true, [](bool flood) {
    EXPECT_FALSE(flood);
    return true;
  }), Result::Sent);
  menu.open();
  menu.next();
  EXPECT_EQ(menu.activate(60100, true, [](bool flood) {
    EXPECT_TRUE(flood);
    return true;
  }), Result::Sent);
}

TEST(AdvertMenu, BackAndDoubleClickCloseWithoutSending) {
  AdvertMenu menu;
  int sends = 0;
  auto send = [&](bool) { ++sends; return true; };
  menu.open();
  menu.previous();
  EXPECT_EQ(menu.activate(100, true, send), Result::Back);
  EXPECT_EQ(menu.activate(100, true, send), Result::Ignored);
  menu.open();
  menu.close();
  EXPECT_EQ(menu.activate(100, true, send), Result::Ignored);
  EXPECT_EQ(sends, 0);
}

TEST(AdvertMenu, SuccessfulSendClosesAndCannotRepeat) {
  AdvertMenu menu;
  int sends = 0;
  auto send = [&](bool) { ++sends; return true; };
  menu.open();
  EXPECT_EQ(menu.activate(100, true, send), Result::Sent);
  EXPECT_EQ(menu.activate(60100, true, send), Result::Ignored);
  EXPECT_EQ(sends, 1);
}

TEST(AdvertMenu, HiddenOrSleepingScreenCannotSend) {
  AdvertMenu menu;
  int sends = 0;
  auto send = [&](bool) { ++sends; return true; };
  menu.open();
  menu.next();
  EXPECT_EQ(menu.activate(100, false, send), Result::Ignored);
  EXPECT_EQ(sends, 0);
  EXPECT_EQ(menu.waitSeconds(100), 0u);
  EXPECT_EQ(menu.activate(100, true, send), Result::Sent);
  EXPECT_EQ(sends, 1);
}

TEST(AdvertMenu, CooldownSurvivesReopeningAndChangingTypeButAllowsBack) {
  AdvertMenu menu;
  int sends = 0;
  auto send = [&](bool) { ++sends; return true; };
  menu.open();
  EXPECT_EQ(menu.activate(0, true, send), Result::Sent);
  menu.open();
  menu.next();
  EXPECT_EQ(menu.activate(59999, true, send), Result::RateLimited);
  EXPECT_EQ(menu.waitSeconds(59999), 1u);
  EXPECT_EQ(sends, 1);
  menu.next();
  EXPECT_EQ(menu.activate(59999, true, send), Result::Back);
  menu.open();
  EXPECT_EQ(menu.selected(), Choice::Local);
  EXPECT_EQ(menu.activate(60000, true, send), Result::Sent);
  EXPECT_EQ(sends, 2);
}

TEST(AdvertMenu, FailedSendCanRetryWithoutCooldown) {
  AdvertMenu menu;
  menu.open();
  menu.next();
  EXPECT_EQ(menu.activate(100, true, [](bool) { return false; }), Result::Failed);
  EXPECT_EQ(menu.selected(), Choice::Flood);
  EXPECT_EQ(menu.waitSeconds(100), 0u);
  EXPECT_EQ(menu.activate(101, true, [](bool) { return true; }), Result::Sent);
}

TEST(AdvertMenu, CooldownHandlesMillisWraparound) {
  AdvertMenu menu;
  const uint32_t start = UINT32_MAX - 10000;
  menu.open();
  EXPECT_EQ(menu.activate(start, true, [](bool) { return true; }), Result::Sent);
  menu.open();
  EXPECT_EQ(menu.waitSeconds(start + uint32_t(59000)), 1u);
  EXPECT_EQ(menu.activate(start + uint32_t(60000), true,
                          [](bool) { return true; }), Result::Sent);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
