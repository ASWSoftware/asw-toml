/* **************************************************************************
Test_ASWToml_Converters_Structs.cpp
Author: Anthony S. West - ASW Software

See header for info.

Copyright 2026 ASW Software

Licensed under the MIT License: see the LICENSE file that comes with this
library for its full text.

SPDX-License-Identifier: MIT

************************************************************************** */

//---------------------------------------------------------------------------
// Module header
#include "Test_ASWToml_Converters_Structs.h"
//---------------------------------------------------------------------------
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
//---------------------------------------------------------------------------
#include "ASWUnitTests_Registry.h"
//---------------------------------------------------------------------------
#include "ASWToml_Converters.h"
#include "ASWToml_Options.h"
#include "ASWToml_Types.h"
#include "ASWToml_Value.h"
//---------------------------------------------------------------------------
#include "UT_Helper_Converters.h"
//---------------------------------------------------------------------------

namespace ASWUnitTests
{

namespace
{

/////////////////////////////////////////////////////////////////////////////
// TTestNamed
//
// A base class without fields of its own, whose member a derived struct lists
/////////////////////////////////////////////////////////////////////////////
struct TTestNamed
{
    std::string Name;

    bool operator==(TTestNamed const& other) const = default;
};


/////////////////////////////////////////////////////////////////////////////
// TTestLevel
//
// A struct that lists a member of its base class as a field
/////////////////////////////////////////////////////////////////////////////
struct TTestLevel : TTestNamed
{
    int Level = 1;

    bool operator==(TTestLevel const& other) const = default;
};

auto ASWTomlFields(TTestLevel const&)
{
    return std::tuple{ ASWToml::Field("name", &TTestLevel::Name), ASWToml::Field("level", &TTestLevel::Level) };
}


/////////////////////////////////////////////////////////////////////////////
// TTestExtended
//
// A struct derived from one with fields, without a list of its own, so it is converted by its base's list
/////////////////////////////////////////////////////////////////////////////
struct TTestExtended : TTestServer
{
    int Extra = 1;

    bool operator==(TTestExtended const& other) const = default;
};

// A struct without fields: an empty table
struct TTestEmpty
{
    bool operator==(TTestEmpty const& other) const = default;
};

std::tuple<> ASWTomlFields(TTestEmpty const&)
{
    return std::tuple<>();
}

// A struct whose list names a key twice, and one whose key isn't valid UTF-8, so neither can be written
struct TTestTwice
{
    int First = 1;
    int Second = 2;

    bool operator==(TTestTwice const& other) const = default;
};

auto ASWTomlFields(TTestTwice const&)
{
    return std::tuple{ ASWToml::Field("a", &TTestTwice::First), ASWToml::OptionalField("a", &TTestTwice::Second) };
}

struct TTestBadKey
{
    int Number = 1;

    bool operator==(TTestBadKey const& other) const = default;
};

auto ASWTomlFields(TTestBadKey const&)
{
    return std::tuple{ ASWToml::Field("\xFF", &TTestBadKey::Number) };
}

} // namespace

// A struct whose ASWTomlFields() doesn't return a std::tuple of fields, and one whose fields are another struct's, so
// neither has a converter. They are outside the anonymous namespace, since a function of it used only in a
// static_assert would be a warning (Clang's -Wunneeded-internal-declaration).
struct TTestNotFields
{
    int Number = 1;
};

std::vector<int> ASWTomlFields(TTestNotFields const&);

struct TTestOtherFields
{
    int Number = 1;
};

std::tuple<ASWToml::TASWTomlField<TTestNotFields, int> > ASWTomlFields(TTestOtherFields const&);

} // namespace ASWUnitTests

// The structs: those for which argument-dependent lookup finds ASWTomlFields() returning a std::tuple of their fields
// (or of a base class's), in a vector or a map too; not a struct without it, or whose function returns anything else
static_assert(ASWToml::Detail::StructWithFields<ASWUnitTests::TTestServer> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestScreen> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestLevel> &&
    ASWToml::Detail::StructWithFields<ASWUnitTests::TTestEmpty>, "a struct with a field list should be found");
static_assert(ASWToml::Detail::StructWithFields<ASWUnitTests::TTestExtended>,
    "a struct derived from one with a field list should be found, with its base's list");
static_assert(!ASWToml::Detail::StructWithFields<ASWUnitTests::TTestNamed> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestNotFields> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestOtherFields> &&
    !ASWToml::Detail::StructWithFields<ASWUnitTests::TTestServer const>,
    "a struct without a field list, or whose list isn't a std::tuple of its own fields, should be none");
static_assert(ASWToml::TomlConvertible<ASWUnitTests::TTestServer> &&
    ASWToml::TomlConvertible<std::vector<ASWUnitTests::TTestServer> > &&
    ASWToml::TomlConvertible<std::map<std::string, ASWUnitTests::TTestCluster> >,
    "a struct should have a converter, in a vector or a map too");
static_assert(!ASWToml::TomlConvertible<ASWUnitTests::TTestNamed> &&
    !ASWToml::TomlConvertible<ASWUnitTests::TTestNotFields>, "a struct without a field list should have none");

// The field lists can be constant expressions
static_assert(ASWToml::Field("a", &ASWUnitTests::TTestServer::Host).Key == "a" &&
    !ASWToml::Field("a", &ASWUnitTests::TTestServer::Host).Optional &&
    ASWToml::OptionalField("b", &ASWUnitTests::TTestServer::Port).Optional &&
    ASWToml::OptionalField("b", &ASWUnitTests::TTestServer::Port).Member == &ASWUnitTests::TTestServer::Port,
    "Field() and OptionalField() should make a field in a constant expression");

namespace ASWUnitTests
{

namespace
{

using namespace std::string_literals;

using ASWToml::ETomlCode;
using ASWToml::ETomlType;
using ASWToml::TASWTomlConverter;
using ASWToml::TASWTomlResult;
using ASWToml::TASWTomlValue;
using ASWToml::TASWTomlValueOptions;

using TArray = TASWTomlValue::TValueVector;
using TTable = TASWTomlValue::TKeyValueVector;

} // namespace

//---------------------------------------------------------------------------

/////////////////////////////////////////////////////////////////////////////
// TTest_ASWToml_StructConverters
/////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------------------
TTest_ASWToml_StructConverters::TTest_ASWToml_StructConverters()
    : inherited("ASWToml_StructConverters_Tests")
{
}
//---------------------------------------------------------------------------
TTest_ASWToml_StructConverters::~TTest_ASWToml_StructConverters()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::SetUp_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::SetUp_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::TearDown_Group()
{
}
//---------------------------------------------------------------------------
void TTest_ASWToml_StructConverters::TearDown_Test(ITestCase& /*testCase*/)
{
}
//---------------------------------------------------------------------------

// /////// Begin tests after this line ///////////////////////

//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, EveryType_RefusesWhatIsntATable)
{
    // Arrange & Act & Assert
    std::initializer_list<ETomlType> const accepted = { ETomlType::Table };
    CheckEmpty(RefusalProblem(TTestServer(), accepted, "a table"), "a struct should read tables only");
    CheckEmpty(RefusalProblem(TTestEmpty(), accepted, "a table"), "even one without fields");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, Fields_OfABaseClassAndPrivateMembers)
{
    // Arrange
    TTestLevel level;
    TTestScreen screen;
    TTestExtended extended;
    extended.Extra = 7;

    // Act
    TASWTomlResult const readLevel = TASWTomlConverter<TTestLevel>::FromValue(TTable{ { "name", "n" }, { "level", 3 } },
        level, TASWTomlValueOptions());
    TASWTomlResult const readScreen = TASWTomlConverter<TTestScreen>::FromValue(TTable{ { "width", 800 } }, screen,
        TASWTomlValueOptions());
    TASWTomlResult const readExtended = TASWTomlConverter<TTestExtended>::FromValue(TTable{ { "host", "h" },
        { "port", 1 }, { "extra", 2 } }, extended, TASWTomlValueOptions());

    // Assert
    CheckTrue(readLevel.IsOk() && level.Name == "n" && level.Level == 3,
        "a member of a base class should be a field: " + readLevel.Message);
    CheckEquals("{ name = \"n\", level = 3 }"s, Written(level), "and be written");
    CheckTrue(readScreen.IsOk() && screen == TTestScreen(800, 480),
        "private members listed by a friend should be fields: " + readScreen.Message);
    CheckEquals("{ width = 800, height = 480 }"s, Written(screen), "and be written");
    CheckTrue(readExtended.IsOk() && extended.Host == "h" && extended.Port == 1 && extended.Extra == 7,
        "a struct derived from one with fields should be read by its base's list: " + readExtended.Message);
    CheckEquals("{ host = \"h\", port = 1, enabled = true }"s, Written(extended), "and written by it");
    CheckEquals("{}"s, Written(TTestEmpty()), "a struct without fields should be an empty table");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, FromValue_FailsAtTheFirstFieldThatFails)
{
    // Arrange
    TTestServer initial;
    initial.Host = "kept";
    std::vector<TTestServer> const initialServers = { initial };
    TTestCluster initialCluster;
    initialCluster.Name = "kept";
    TASWTomlValue const complete = TTable{ { "host", "a" }, { "port", 1 } };
    TASWTomlValue const incomplete = TTable{ { "host", "b" } };
    TASWTomlValue const servers = TArray{ complete, incomplete };
    TASWTomlValue const wrongWidth = TTable{ { "width", "wide" } };

    // Act & Assert
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" } }, initial, ETomlCode::NotFound,
        "The table has no key port.", "port"), "a key the table lacks should fail the struct, at the key");
    CheckEmpty(ReadFailureProblem(TTable{ { "port", 1 } }, initial, ETomlCode::NotFound, "The table has no key host.",
        "host"), "the first in the list's order");
    CheckEmpty(ReadFailureProblem(TTable{ { "port", "x" }, { "host", 5 } }, initial, ETomlCode::TypeMismatch,
        "The value 5 is an integer, not a string.", "host"), "a value that fails should fail it, at its key");
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" }, { "port", 70000 } }, initial, ETomlCode::ValueOutOfRange,
        "The value 70000 is out of range: it must be from 0 to 65535.", "port"), "with the value's code and message");
    CheckEmpty(ReadFailureProblem(TTable{ { "host", "a" }, { "port", 1 }, { "comment", 2 } }, initial,
        ETomlCode::TypeMismatch, "The value 2 is an integer, not a string.", "comment"),
        "a std::optional member should be read as its item's type");
    CheckEmpty(ReadFailureProblem(servers, initialServers, ETomlCode::NotFound, "The table has no key port.",
        "[1].port"), "an element of a vector of structs should fail at its index and key");
    CheckEmpty(ReadFailureProblem(TTable{ { "name", "c" }, { "screen", wrongWidth } }, initialCluster,
        ETomlCode::TypeMismatch, "The value \"wide\" is a string, not an integer.", "screen.width"),
        "a struct in a struct should fail at the path of its key");
    CheckEmpty(ReadFailureProblem(TTable{ { "name", "c" }, { "screen", complete }, { "servers", servers } },
        initialCluster, ETomlCode::NotFound, "The table has no key width.", "screen.width"),
        "the first field in the list's order, not the table's");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, FromValue_ReadsEachFieldIntoACopy)
{
    // Arrange
    TTestServer server;
    server.Host = "kept";
    server.Port = 1;
    server.Enabled = false;
    server.Comment = "kept";
    TTestServer partial = server;

    // Act
    TASWTomlResult const read = TASWTomlConverter<TTestServer>::FromValue(TTable{ { "extra", true },
        { "port", 8080 }, { "host", "example.com" } }, server, TASWTomlValueOptions());
    TASWTomlResult const readPartial = TASWTomlConverter<TTestServer>::FromValue(TTable{ { "host", "a" },
        { "port", 2 }, { "enabled", true }, { "comment", "new" } }, partial, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && server.Host == "example.com" && server.Port == 8080,
        "each field should read its key's value, whatever the table's order: " + read.Message);
    CheckFalse(server.Enabled, "an optional field the table lacks should keep the value it had");
    CheckFalse(server.Comment.has_value(), "a std::optional member the table lacks should be std::nullopt");
    CheckTrue(readPartial.IsOk() && partial.Enabled && partial.Comment == "new",
        "and both should read a key the table has: " + readPartial.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, OptionalStruct_ReadsIntoTheValueItHolds)
{
    // Arrange
    TTestCluster cluster;
    cluster.Name = "c";
    cluster.Spare = TTestScreen(1, 2);
    TTestCluster empty = cluster;
    empty.Spare.reset();
    TASWTomlValue const spare = TTable{ { "width", 3 } };

    // Act
    TASWTomlResult const read = TASWTomlConverter<TTestCluster>::FromValue(TTable{ { "name", "c" },
        { "screen", spare }, { "spare", spare } }, cluster, TASWTomlValueOptions());
    TASWTomlResult const readEmpty = TASWTomlConverter<TTestCluster>::FromValue(TTable{ { "name", "c" },
        { "screen", spare }, { "spare", spare } }, empty, TASWTomlValueOptions());

    // Assert
    CheckTrue(read.IsOk() && cluster.Spare == TTestScreen(3, 2),
        "a std::optional struct should be read into the struct it holds, which keeps what the table lacks: " +
        read.Message);
    CheckTrue(readEmpty.IsOk() && empty.Spare == TTestScreen(3, 480),
        "and into a new one when it holds none: " + readEmpty.Message);
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, RoundTrip_EveryType_ThroughADocument)
{
    // Arrange
    TTestServer first;
    first.Host = "a";
    TTestServer second;
    second.Host = "b";
    second.Port = 443;
    second.Enabled = false;
    second.Comment = "backup";
    TTestCluster cluster;
    cluster.Name = "c";
    cluster.Limits = { { "cpu", 2 } };
    cluster.Servers = { first, second };
    std::map<std::string, TTestServer> const named = { { "main", first } };

    // Act & Assert
    CheckEmpty(TableRoundTripProblem(first, "[v]\nhost = \"a\"\nport = 80\nenabled = true\n"),
        "a struct, without its std::optional member that is std::nullopt");
    CheckEmpty(TableRoundTripProblem(second, "[v]\nhost = \"b\"\nport = 443\nenabled = false\ncomment = \"backup\"\n"),
        "with it when it holds a value");
    CheckEmpty(TableRoundTripProblem(std::vector<TTestServer>{ first, second }, "[[v]]\nhost = \"a\"\nport = 80\n"
        "enabled = true\n\n[[v]]\nhost = \"b\"\nport = 443\nenabled = false\ncomment = \"backup\"\n"),
        "a vector of structs as an array of tables");
    CheckEmpty(TableRoundTripProblem(named, "[v.main]\nhost = \"a\"\nport = 80\nenabled = true\n"),
        "a map of structs as a table of tables");
    CheckEmpty(TableRoundTripProblem(cluster, "[v]\nname = \"c\"\n\n[v.screen]\nwidth = 640\nheight = 480\n\n"
        "[v.limits]\ncpu = 2\n\n[[v.servers]]\nhost = \"a\"\nport = 80\nenabled = true\n\n[[v.servers]]\nhost = \"b\"\n"
        "port = 443\nenabled = false\ncomment = \"backup\"\n"), "a struct of structs, maps and vectors");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, ToValue_FailsForAKeyOrValueThatCantBeWritten)
{
    // Arrange
    TTestServer server;
    server.Host = "\xFF";
    TTestCluster cluster;
    cluster.Servers = { TTestServer(), server };

    // Act & Assert
    CheckEmpty(WriteFailureProblem(server, ETomlCode::InvalidValue,
        "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", "host"),
        "a value that can't be written should fail the struct, at its key");
    CheckEmpty(WriteFailureProblem(cluster, ETomlCode::InvalidValue,
        "The string can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", "servers[1].host"),
        "inside a field's value too");
    CheckEmpty(WriteFailureProblem(TTestTwice(), ETomlCode::InvalidValue, "The key a is named by two fields of the "
        "struct's ASWTomlFields(): each field needs a key of its own.", ""),
        "a key named twice in the list should fail it, without a path");
    CheckEmpty(WriteFailureProblem(TTestBadKey(), ETomlCode::InvalidValue,
        "The key can't be written: it isn't valid UTF-8 (at byte 0), which TOML needs.", ""),
        "and so should a key that isn't valid UTF-8");
}
//---------------------------------------------------------------------------
ASW_TEST(TTest_ASWToml_StructConverters, ToValue_WritesEveryFieldInTheListsOrder)
{
    // Arrange
    TTestServer server;
    server.Host = "h";
    server.Comment = "c";
    TASWTomlValue value = TArray{ 1 };

    // Act
    TASWTomlResult const result = TASWTomlConverter<TTestServer>::ToValue(server, value, TASWTomlValueOptions());

    // Assert
    TTable const* const table = value.AsTable();
    CheckTrue(result.IsOk() && table != nullptr && table->size() == 4,
        "the array should be replaced by a table of the four fields: " + result.Message);
    if (table != nullptr && table->size() == 4)
    {
        CheckTrue((*table)[0].Key == "host" && (*table)[1].Key == "port" && (*table)[2].Key == "enabled" &&
            (*table)[3].Key == "comment", "in the order of the list");
        CheckTrue((*table)[0].Value == TASWTomlValue("h") && (*table)[1].Value == TASWTomlValue(80) &&
            (*table)[2].Value == TASWTomlValue(true) && (*table)[3].Value == TASWTomlValue("c"),
            "each with its member's value");
    }
}
//---------------------------------------------------------------------------

} // namespace ASWUnitTests

//---------------------------------------------------------------------------
ASW_REGISTER_TEST_GROUP_IN_SUITE(ASWUnitTests::TTest_ASWToml_StructConverters, "Converters")
