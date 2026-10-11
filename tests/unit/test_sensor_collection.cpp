// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sensor_collection.h"
#include "sensor_enum_names.h"

#include "../catch_amalgamated.hpp"

using namespace helix::sensors;
using json = nlohmann::json;

namespace {

enum class Role { NONE = 0, MAIN = 1, AUX = 2 };
enum class Kind { A = 1, B = 2 };

struct Cfg {
    std::string klipper_name;
    Role role = Role::NONE;
    Kind type = Kind::A;
    bool enabled = true;
};

struct St {
    float value = 0.0f;
    bool available = false;
};

using Collection = SensorCollection<Cfg, St>;

Cfg cfg(const std::string& name, Role role = Role::NONE) {
    Cfg c;
    c.klipper_name = name;
    c.role = role;
    return c;
}

constexpr EnumName<Role> kRoles[] = {
    {Role::NONE, "none", "Unassigned"},
    {Role::MAIN, "main", "Main"},
    {Role::AUX, "aux", "Auxiliary"},
};

std::string role_id(Role r) {
    return enum_id(kRoles, r);
}
Role role_from(const std::string& s) {
    return enum_from_id(kRoles, s);
}
std::string kind_id(Kind k) {
    return k == Kind::B ? "b" : "a";
}

} // namespace

TEST_CASE("SensorCollection - reconcile adds, keeps and removes", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a"), cfg("b")});
    REQUIRE(c.size() == 2);
    REQUIRE(c.state("a")->available);
    REQUIRE(c.state("b")->available);

    c.state_at("a").value = 4.5f;

    c.reconcile({cfg("a"), cfg("c")});

    REQUIRE(c.size() == 2);
    REQUIRE(c.find("b") == nullptr);
    REQUIRE(c.state("b") == nullptr);
    REQUIRE(c.state("a")->value == 4.5f);
    REQUIRE(c.state("a")->available);
    REQUIRE(c.state("c")->available);
    REQUIRE(c.state("c")->value == 0.0f);
}

TEST_CASE("SensorCollection - reconcile keep_missing keeps the dropped sensor's state",
          "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a"), cfg("b")});
    c.state_at("b").value = 2.0f;

    c.reconcile({cfg("a")}, /*keep_missing=*/true);

    REQUIRE(c.find("b") == nullptr);
    REQUIRE(c.state("b") != nullptr);
    REQUIRE_FALSE(c.state("b")->available);
    REQUIRE(c.state("b")->value == 2.0f);

    c.reconcile({cfg("a"), cfg("b")}, true);
    REQUIRE(c.state("b")->available);
    REQUIRE(c.state("b")->value == 2.0f);
}

TEST_CASE("SensorCollection - clear drops configs and states", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a")});
    c.clear();
    REQUIRE(c.empty());
    REQUIRE(c.state("a") == nullptr);
}

TEST_CASE("SensorCollection - reconcile keeps discovery order", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("z"), cfg("a"), cfg("m")});
    std::vector<std::string> names;
    for (const auto& s : c) {
        names.push_back(s.klipper_name);
    }
    REQUIRE(names == std::vector<std::string>{"z", "a", "m"});
}

TEST_CASE("SensorCollection - find by name and role", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a"), cfg("b", Role::MAIN), cfg("c", Role::MAIN)});

    REQUIRE(c.find("b")->klipper_name == "b");
    REQUIRE(c.find("missing") == nullptr);
    REQUIRE(c.find_by_role(Role::MAIN)->klipper_name == "b");
    REQUIRE(c.find_by_role(Role::AUX) == nullptr);

    const Collection& cc = c;
    REQUIRE(cc.find("c")->klipper_name == "c");
    REQUIRE(cc.state("a") != nullptr);
    REQUIRE(cc.state("missing") == nullptr);
}

TEST_CASE("SensorCollection - role_state and live_state", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a"), cfg("b", Role::MAIN)});
    c.state_at("b").value = 7.0f;

    REQUIRE(c.role_state(Role::NONE) == nullptr);
    REQUIRE(c.live_state(Role::NONE) == nullptr);
    REQUIRE(c.role_state(Role::MAIN)->value == 7.0f);
    REQUIRE(c.live_state(Role::MAIN)->value == 7.0f);
    REQUIRE(c.role_state(Role::AUX) == nullptr);

    c.find("b")->enabled = false;
    REQUIRE(c.role_state(Role::MAIN) != nullptr);
    REQUIRE(c.live_state(Role::MAIN) == nullptr);

    c.find("b")->enabled = true;
    c.state_at("b").available = false;
    REQUIRE(c.role_state(Role::MAIN) != nullptr);
    REQUIRE(c.live_state(Role::MAIN) == nullptr);
}

TEST_CASE("SensorCollection - assign_exclusive_role", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a", Role::MAIN), cfg("b", Role::AUX), cfg("c", Role::NONE)});

    REQUIRE(c.assign_exclusive_role("c", Role::MAIN) == c.find("c"));
    REQUIRE(c.find("a")->role == Role::NONE);
    REQUIRE(c.find("b")->role == Role::AUX);
    REQUIRE(c.find("c")->role == Role::MAIN);

    // NONE is never exclusive: unassigning one sensor leaves the others alone.
    c.find("a")->role = Role::NONE;
    c.assign_exclusive_role("b", Role::NONE);
    REQUIRE(c.find("a")->role == Role::NONE);
    REQUIRE(c.find("c")->role == Role::MAIN);

    // An unknown name still takes the role from its holder.
    c.find("b")->role = Role::AUX;
    REQUIRE(c.assign_exclusive_role("missing", Role::AUX) == nullptr);
    REQUIRE(c.find("b")->role == Role::NONE);
}

TEST_CASE("SensorCollection - JSON round trip", "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a", Role::MAIN), cfg("b")});
    c.find("b")->enabled = false;
    c.find("b")->type = Kind::B;

    const json saved = c.to_json(role_id, kind_id);
    REQUIRE(saved["sensors"].size() == 2);
    REQUIRE(saved["sensors"][0]["klipper_name"] == "a");
    REQUIRE(saved["sensors"][0]["role"] == "main");
    REQUIRE(saved["sensors"][1]["enabled"] == false);
    REQUIRE(saved["sensors"][1]["type"] == "b");

    Collection restored;
    restored.reconcile({cfg("a"), cfg("b")});
    REQUIRE(restored.apply_json(saved, role_from));
    REQUIRE(restored.find("a")->role == Role::MAIN);
    REQUIRE(restored.find("a")->enabled);
    REQUIRE_FALSE(restored.find("b")->enabled);
}

TEST_CASE("SensorCollection - apply_json skips unknown sensors and malformed fields",
          "[sensors][sensor_collection]") {
    Collection c;
    c.reconcile({cfg("a", Role::AUX)});

    REQUIRE_FALSE(c.apply_json(json::object(), role_from));
    REQUIRE_FALSE(c.apply_json(json{{"sensors", "nope"}}, role_from));

    const json saved = {
        {"sensors", json::array({
                        {{"klipper_name", "ghost"}, {"role", "main"}},
                        {{"role", "main"}},
                        {{"klipper_name", "a"}, {"role", "main"}, {"enabled", nullptr}},
                    })}};
    REQUIRE(c.apply_json(saved, role_from));
    REQUIRE(c.find("ghost") == nullptr);
    REQUIRE(c.find("a")->role == Role::MAIN);
    REQUIRE(c.find("a")->enabled);
}

TEST_CASE("EnumName table lookups", "[sensors][sensor_collection]") {
    REQUIRE(std::string(enum_id(kRoles, Role::AUX)) == "aux");
    REQUIRE(std::string(enum_display(kRoles, Role::MAIN)) == "Main");
    REQUIRE(enum_from_id(kRoles, "aux") == Role::AUX);
    REQUIRE(enum_from_id(kRoles, "bogus") == Role::NONE);
    REQUIRE(std::string(enum_id(kRoles, static_cast<Role>(42))) == "none");
    REQUIRE(std::string(enum_display(kRoles, static_cast<Role>(42))) == "Unassigned");
}
