// SPDX-License-Identifier: GPL-3.0-or-later
// Tests for the game-independent logic (run natively by build.sh and in CI).
#include "core/NamePool.hpp"
#include "game/Bridge.hpp"
#include "game/Settings.hpp"

#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

static int failures = 0;
#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) {                                                           \
            std::printf("FALLO %s:%d  %s\n", __FILE__, __LINE__, #expr);        \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

using namespace Bridge;

static void TestValidation() {
    CHECK(SearchKey(L"ÁCIDO de araña") == L"acido de arana");
    CHECK(SearchKey(L"MUTACIÓN") == SearchKey(L"mutacion"));
    float parsed = 0;
    CHECK(ParseMultiplier(L"2,5", parsed) && parsed == 2.5f);
    CHECK(ParseMultiplier(L"0.5", parsed) && parsed == .5f);
    for (auto text : {L"", L"1,2,3", L"nan", L"inf", L"-1", L"1001", L"2x", L"2 3"}) CHECK(!ParseMultiplier(text, parsed));

    Request r;
    r.action = Action::Spawn;
    r.id = "FiberRaw";
    for (int n : {-1, 0, 1000, 100000}) { r.count = n; CHECK(!Valid(r)); }
    for (int n : {1, 2, 999}) { r.count = n; CHECK(Valid(r)); }
    r.id.clear();
    CHECK(!Valid(r));

    for (Action action : {Action::OutgoingDamage, Action::IncomingDamage}) {
        r.action = action;
        for (float f : {-1.0f, 1000.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
            r.value = f;
            CHECK(!Valid(r));
        }
        for (float f : {0.0f, .5f, 1.0f, 1000.0f}) { r.value = f; CHECK(Valid(r)); }
    }
    r.action = Action::OutgoingDamage;
    r.value = 2;
    r.scope = DamageScope::Self;
    CHECK(Valid(r));
    r.scope = DamageScope::Player;
    r.playerId = -1;
    CHECK(!Valid(r));
    r.playerId = 42;
    CHECK(Valid(r));
    r.scope = static_cast<DamageScope>(99);
    CHECK(!Valid(r));

    for (Action action : {Action::AutoPickup, Action::ClearNearby}) {
        r = {};
        r.action = action;
        for (float radius : {0.0f, 24.0f, 2001.0f, std::numeric_limits<float>::quiet_NaN()}) { r.value = radius; CHECK(!Valid(r)); }
        for (float radius : {25.0f, 300.0f, 2000.0f}) { r.value = radius; CHECK(Valid(r)); }
    }
    CHECK(WithinRadius(300, 0, 0, 300));
    CHECK(!WithinRadius(301, 0, 0, 300));
    CHECK(!WithinRadius(250, 250, 0, 300));
    CHECK(!WithinRadius(0, 0, std::numeric_limits<double>::infinity(), 300));

    LooseItemState loose;
    loose.inventoryItem = true;
    CHECK(EligibleLooseItem(loose, true) && EligibleLooseItem(loose, false));
    for (bool LooseItemState::*blocked : {&LooseItemState::destroying, &LooseItemState::attached, &LooseItemState::equipped,
                                          &LooseItemState::processing, &LooseItemState::inFlight, &LooseItemState::keyItem}) {
        auto item = loose;
        item.*blocked = true;
        CHECK(!EligibleLooseItem(item, true) && !EligibleLooseItem(item, false));
    }
    LooseItemState plank;
    plank.haulingItem = true;
    CHECK(!EligibleLooseItem(plank, true) && EligibleLooseItem(plank, false));

    r = {};
    for (Action action : {Action::AddWhiteMolars, Action::AddGoldMolars, Action::AddBuggyPoints}) {
        r.action = action;
        r.count = 0; CHECK(!Valid(r));
        r.count = 999; CHECK(Valid(r));
        r.count = 1000; CHECK(!Valid(r));
    }
    r.action = Action::AddScience;
    r.count = 1000000; CHECK(Valid(r));
    r.count = 1000001; CHECK(!Valid(r));
    r.action = Action::MutationSlots;
    for (int n : {0, 1, 33}) { r.count = n; CHECK(!Valid(r)); }
    for (int n : {2, 5, 32}) { r.count = n; CHECK(Valid(r)); }

    // New in 0.3.0
    r = {};
    r.action = Action::Speed;
    for (float v : {0.0f, 0.2f, 11.0f}) { r.value = v; CHECK(!Valid(r)); }
    for (float v : {0.25f, 1.0f, 10.0f}) { r.value = v; CHECK(Valid(r)); }
    r.action = Action::SetTime;
    r.count = 24; CHECK(!Valid(r));
    r.count = 0; CHECK(Valid(r));
    r.count = 23; CHECK(Valid(r));
    r.action = Action::TeleportTo;
    r.x = 1e8; CHECK(!Valid(r));
    r.x = 1234.5; CHECK(Valid(r));
    r.y = std::numeric_limits<double>::quiet_NaN(); CHECK(!Valid(r));
    r = {};
    r.action = Action::SpawnMount;
    r.id = "Ladybug";
    r.count = 4; CHECK(!Valid(r));
    r.count = 3; CHECK(Valid(r));
    r.action = Action::SetRule;
    r.value = -1; CHECK(!Valid(r));
    r.value = 2; CHECK(Valid(r));
    r.action = Action::Count;
    CHECK(!Valid(r));
}

// Builds a fake FNamePool block: entries are a 16-bit header + characters, 2-byte aligned.
static void TestNamePool() {
    std::vector<uint8_t> block(1 << 17, 0);
    uint32_t cursor = 0;
    auto add = [&](const char* text, bool wide = false) -> uint32_t {
        const uint32_t index = cursor / 2;
        const uint16_t len = static_cast<uint16_t>(std::strlen(text));
        const uint16_t header = static_cast<uint16_t>((len << 6) | (5 << 1) | (wide ? 1 : 0));
        std::memcpy(&block[cursor], &header, 2);
        cursor += 2;
        for (uint16_t i = 0; i < len; ++i) {
            if (wide) {
                const char16_t c = static_cast<char16_t>(static_cast<unsigned char>(text[i]));
                std::memcpy(&block[cursor], &c, 2);
                cursor += 2;
            } else {
                block[cursor++] = static_cast<uint8_t>(text[i]);
            }
        }
        cursor = (cursor + 1) & ~1u;
        return index;
    };
    const uint32_t none = add("None");
    const uint32_t byteProperty = add("ByteProperty");
    const uint32_t player = add("SurvivalPlayerCharacter");
    const uint32_t wide = add("Telarana", true);

    uint32_t stride = 0;
    NamePool::HeaderLayout layout;
    CHECK(NamePool::LooksLikeFirstBlock(block.data(), block.size(), stride, layout));
    CHECK(stride == 2 && layout == NamePool::HeaderLayout::ProbeHash);
    CHECK(none == 0 && byteProperty == 3);

    const uint8_t* blocks[2] = {block.data(), nullptr};
    const uint32_t currentBlock = 0;
    NamePool::View view;
    view.blocks = blocks;
    view.currentBlock = &currentBlock;
    std::u16string text;
    CHECK(view.ToString(player, 0, text) && NamePool::ToUtf8(text) == "SurvivalPlayerCharacter");
    CHECK(view.ToString(player, 3, text) && NamePool::ToUtf8(text) == "SurvivalPlayerCharacter_2");
    CHECK(view.ToString(wide, 0, text) && NamePool::ToUtf8(text) == "Telarana");
    CHECK(!view.Decode((1u << 16) | 4, text));     // block 1 is beyond CurrentBlock
    CHECK(!view.Decode(0xFFFF, text));              // empty slot (length 0)

    std::vector<uint8_t> garbage(64, 0x41);
    CHECK(!NamePool::LooksLikeFirstBlock(garbage.data(), garbage.size(), stride, layout));
    CHECK(NamePool::ToUtf8(u"Araña") == "Ara\xc3\xb1" "a");
}

static void TestSettings() {
    Settings::Data d;
    d.hotkey = 0x2D;
    d.alwaysOnTop = true;
    d.opacity = 0.8f;
    d.uiScale = 1.25f;
    d.page = 3;
    d.pickupRadius = 450;
    d.favorites = {"FiberRaw", "Sap"};
    Settings::AddRecent(d, "Twig");
    Settings::AddRecent(d, "Sap");
    Settings::AddRecent(d, "Twig");
    d.waypoints.push_back({L"Base del roble", 100.5, -200.25, 30});
    d.waypoints.push_back({L"Lab|raro", 1, 2, 3});
    const Settings::Data back = Settings::Parse(Settings::Serialize(d));
    CHECK(back.hotkey == 0x2D && back.alwaysOnTop && back.page == 3);
    CHECK(back.opacity > 0.79f && back.opacity < 0.81f && back.uiScale > 1.24f && back.uiScale < 1.26f);
    CHECK(back.pickupRadius == 450);
    CHECK(back.favorites.size() == 2 && Settings::IsFavorite(back, "Sap"));
    CHECK(back.recents.size() == 2 && back.recents[0] == "Twig");
    CHECK(back.waypoints.size() == 2 && back.waypoints[0].name == L"Base del roble" && back.waypoints[0].y == -200.25);
    CHECK(back.waypoints[1].name == L"Lab raro");

    const Settings::Data hostile = Settings::Parse("opacity=nan\nuiScale=99\nhotkey=999\nfavorite=../../etc\nwaypoint=a|b\npickupRadius=1\n");
    CHECK(hostile.opacity == 1.0f && hostile.uiScale == 1.6f && hostile.hotkey == 0x77);
    CHECK(hostile.favorites.empty() && hostile.waypoints.empty() && hostile.pickupRadius == 25);
    Settings::Data favs;
    CHECK(Settings::ToggleFavorite(favs, "Clay") && !Settings::ToggleFavorite(favs, "Clay") && favs.favorites.empty());
}

int main() {
    TestValidation();
    TestNamePool();
    TestSettings();
    if (failures) {
        std::printf("%d comprobaciones fallaron\n", failures);
        return 1;
    }
    std::printf("Todas las pruebas superadas\n");
    return 0;
}
