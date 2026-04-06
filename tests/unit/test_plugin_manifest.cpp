#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

#include "ui/workspace/plugin_manifest.hpp"

using namespace spectra;

namespace
{

// Write content to a temporary file and return its path.
std::string write_temp_file(const std::string& filename, const std::string& content)
{
    auto path = (std::filesystem::temp_directory_path() / filename).string();
    std::ofstream(path) << content;
    return path;
}

}   // namespace

// ─── PluginManifest::is_valid ─────────────────────────────────────────────────

TEST(PluginManifest, IsValidEmptyManifest)
{
    PluginManifest m;
    EXPECT_FALSE(m.is_valid());
}

TEST(PluginManifest, IsValidMissingVersion)
{
    PluginManifest m;
    m.name        = "Test";
    m.api_version = "2.0";
    EXPECT_FALSE(m.is_valid());
}

TEST(PluginManifest, IsValidMissingApiVersion)
{
    PluginManifest m;
    m.name    = "Test";
    m.version = "1.0.0";
    EXPECT_FALSE(m.is_valid());
}

TEST(PluginManifest, IsValidComplete)
{
    PluginManifest m;
    m.name        = "Test";
    m.version     = "1.0.0";
    m.api_version = "2.0";
    EXPECT_TRUE(m.is_valid());
}

// ─── PluginManifest::parsed_api_version ──────────────────────────────────────

TEST(PluginManifest, ParsedApiVersionValid)
{
    PluginManifest m;
    m.api_version       = "2.0";
    auto [major, minor] = m.parsed_api_version();
    EXPECT_EQ(major, 2u);
    EXPECT_EQ(minor, 0u);
}

TEST(PluginManifest, ParsedApiVersionNonZeroMinor)
{
    PluginManifest m;
    m.api_version       = "1.3";
    auto [major, minor] = m.parsed_api_version();
    EXPECT_EQ(major, 1u);
    EXPECT_EQ(minor, 3u);
}

TEST(PluginManifest, ParsedApiVersionNoDot)
{
    PluginManifest m;
    m.api_version       = "2";
    auto [major, minor] = m.parsed_api_version();
    EXPECT_EQ(major, 0u);
    EXPECT_EQ(minor, 0u);
}

TEST(PluginManifest, ParsedApiVersionEmpty)
{
    PluginManifest m;
    auto [major, minor] = m.parsed_api_version();
    EXPECT_EQ(major, 0u);
    EXPECT_EQ(minor, 0u);
}

TEST(PluginManifest, ParsedApiVersionNonNumeric)
{
    PluginManifest m;
    m.api_version       = "abc.def";
    auto [major, minor] = m.parsed_api_version();
    EXPECT_EQ(major, 0u);
    EXPECT_EQ(minor, 0u);
}

// ─── PluginManifest::is_api_compatible ───────────────────────────────────────

TEST(PluginManifest, ApiCompatibleExactMatch)
{
    PluginManifest m;
    m.api_version = "2.0";
    EXPECT_TRUE(m.is_api_compatible(2, 0));
}

TEST(PluginManifest, ApiCompatibleHostMinorGreater)
{
    PluginManifest m;
    m.api_version = "2.0";
    EXPECT_TRUE(m.is_api_compatible(2, 1));
}

TEST(PluginManifest, ApiCompatibleMajorMismatch)
{
    PluginManifest m;
    m.api_version = "2.0";
    EXPECT_FALSE(m.is_api_compatible(1, 0));
}

TEST(PluginManifest, ApiCompatiblePluginMinorTooHigh)
{
    PluginManifest m;
    m.api_version = "2.1";
    EXPECT_FALSE(m.is_api_compatible(2, 0));
}

TEST(PluginManifest, ApiCompatibleMinorExactMatch)
{
    PluginManifest m;
    m.api_version = "2.3";
    EXPECT_TRUE(m.is_api_compatible(2, 3));
}

// ─── load_plugin_manifest ────────────────────────────────────────────────────

TEST(LoadPluginManifest, ValidManifest)
{
    const std::string json = R"({
  "name": "My Plugin",
  "version": "1.2.0",
  "api_version": "2.0",
  "author": "Developer Name",
  "description": "A data source for serial ports",
  "capabilities": ["data_source", "overlay", "command"],
  "dependencies": ["base_plugin"],
  "min_spectra_version": "0.9.0"
})";

    auto path     = write_temp_file("test_plugin_manifest_valid.json", json);
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    EXPECT_TRUE(manifest.is_valid());
    EXPECT_EQ(manifest.name, "My Plugin");
    EXPECT_EQ(manifest.version, "1.2.0");
    EXPECT_EQ(manifest.api_version, "2.0");
    EXPECT_EQ(manifest.author, "Developer Name");
    EXPECT_EQ(manifest.description, "A data source for serial ports");
    EXPECT_EQ(manifest.min_spectra_version, "0.9.0");
}

TEST(LoadPluginManifest, CapabilitiesArray)
{
    const std::string json = R"({
  "name": "Cap Plugin",
  "version": "1.0.0",
  "api_version": "2.0",
  "capabilities": ["data_source", "overlay", "command"],
  "dependencies": []
})";

    auto path     = write_temp_file("test_plugin_manifest_caps.json", json);
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    ASSERT_EQ(manifest.capabilities.size(), 3u);
    EXPECT_EQ(manifest.capabilities[0], "data_source");
    EXPECT_EQ(manifest.capabilities[1], "overlay");
    EXPECT_EQ(manifest.capabilities[2], "command");
    EXPECT_TRUE(manifest.dependencies.empty());
}

TEST(LoadPluginManifest, DependenciesArray)
{
    const std::string json = R"({
  "name": "Dep Plugin",
  "version": "1.0.0",
  "api_version": "2.0",
  "dependencies": ["plugin_a", "plugin_b"]
})";

    auto path     = write_temp_file("test_plugin_manifest_deps.json", json);
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    ASSERT_EQ(manifest.dependencies.size(), 2u);
    EXPECT_EQ(manifest.dependencies[0], "plugin_a");
    EXPECT_EQ(manifest.dependencies[1], "plugin_b");
}

TEST(LoadPluginManifest, NonExistentFile)
{
    auto manifest = load_plugin_manifest("/nonexistent/path/plugin.json");
    EXPECT_FALSE(manifest.is_valid());
    EXPECT_TRUE(manifest.name.empty());
}

TEST(LoadPluginManifest, InvalidJson)
{
    auto path     = write_temp_file("test_plugin_manifest_bad.json", "not json at all");
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    EXPECT_FALSE(manifest.is_valid());
}

TEST(LoadPluginManifest, MissingRequiredFields)
{
    const std::string json     = R"({"author": "Someone"})";
    auto              path     = write_temp_file("test_plugin_manifest_missing.json", json);
    auto              manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    EXPECT_FALSE(manifest.is_valid());
}

TEST(LoadPluginManifest, EscapedStrings)
{
    const std::string json = R"({
  "name": "Plugin \"Quoted\"",
  "version": "1.0.0",
  "api_version": "2.0"
})";

    auto path     = write_temp_file("test_plugin_manifest_escaped.json", json);
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    EXPECT_TRUE(manifest.is_valid());
    EXPECT_EQ(manifest.name, "Plugin \"Quoted\"");
}

TEST(LoadPluginManifest, EmptyArrays)
{
    const std::string json = R"({
  "name": "Empty Arrays Plugin",
  "version": "1.0.0",
  "api_version": "2.0",
  "capabilities": [],
  "dependencies": []
})";

    auto path     = write_temp_file("test_plugin_manifest_empty_arrays.json", json);
    auto manifest = load_plugin_manifest(path);
    std::filesystem::remove(path);

    EXPECT_TRUE(manifest.is_valid());
    EXPECT_TRUE(manifest.capabilities.empty());
    EXPECT_TRUE(manifest.dependencies.empty());
}

// ─── find_plugin_manifest_path ───────────────────────────────────────────────

TEST(FindPluginManifestPath, NotFound)
{
    // No plugin.json in a non-existent directory
    auto result = find_plugin_manifest_path("/nonexistent/dir/libplugin.so");
    EXPECT_TRUE(result.empty());
}

TEST(FindPluginManifestPath, FoundNextToLibrary)
{
    // Create a temporary directory structure with a plugin.json
    auto tmp_dir = std::filesystem::temp_directory_path() / "spectra_test_manifest_dir";
    std::filesystem::create_directories(tmp_dir);

    // Write plugin.json
    std::ofstream(tmp_dir / "plugin.json") << R"({"name":"X","version":"1.0","api_version":"2.0"})";

    // Fake library path in same directory
    std::string lib_path = (tmp_dir / "libmyplugin.so").string();

    auto result = find_plugin_manifest_path(lib_path);
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(result, (tmp_dir / "plugin.json").string());

    std::filesystem::remove_all(tmp_dir);
}

TEST(FindPluginManifestPath, NoManifestInDirectory)
{
    auto tmp_dir = std::filesystem::temp_directory_path() / "spectra_test_no_manifest_dir";
    std::filesystem::create_directories(tmp_dir);

    std::string lib_path = (tmp_dir / "libplugin.so").string();
    auto        result   = find_plugin_manifest_path(lib_path);
    EXPECT_TRUE(result.empty());

    std::filesystem::remove_all(tmp_dir);
}
