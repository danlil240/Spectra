#include <gtest/gtest.h>

#include <spectra/app.hpp>

#include "ui/automation/automation_dispatch.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/automation/automation_server.hpp"

using namespace spectra;

TEST(AutomationDispatch, JsonHasKeyFindsPresentKey)
{
    EXPECT_TRUE(json_has_key(R"({"command_id":"view.reset"})", "command_id"));
    EXPECT_TRUE(json_has_key(R"({"x":1.5,"y":2.0})", "x"));
}

TEST(AutomationDispatch, JsonHasKeyMissingKey)
{
    EXPECT_FALSE(json_has_key(R"({"x":1.5})", "y"));
    EXPECT_FALSE(json_has_key("{}", "command_id"));
}

TEST(AutomationDispatch, ValidateRequiredStringParam)
{
    std::string error;
    const std::vector<ParamSpec> specs{
        {.name = "command_id", .kind = ParamKind::String, .required = true},
    };
    EXPECT_TRUE(validate_automation_params(R"({"command_id":"view.reset"})", specs, error));
    EXPECT_FALSE(validate_automation_params(R"({"command_id":""})", specs, error));
    EXPECT_FALSE(validate_automation_params("{}", specs, error));
    EXPECT_EQ(error, "Missing parameter: command_id");
}

TEST(AutomationDispatch, ValidateOptionalParamSkipped)
{
    std::string error;
    const std::vector<ParamSpec> specs{
        {.name = "path", .kind = ParamKind::String, .required = false},
    };
    EXPECT_TRUE(validate_automation_params("{}", specs, error));
}

TEST(AutomationDispatch, ValidateRequiredNumericParam)
{
    std::string error;
    const std::vector<ParamSpec> specs{
        {.name = "figure_id", .kind = ParamKind::Int, .required = true},
    };
    EXPECT_TRUE(validate_automation_params(R"({"figure_id":3})", specs, error));
    EXPECT_FALSE(validate_automation_params("{}", specs, error));
}

TEST(AutomationDispatch, CheckContextUiContextMissing)
{
    App            app;
    std::string    error;
    WindowUIContext* ui = nullptr;
    EXPECT_FALSE(
        check_automation_context(AutomationContextFlag::UiContext, app, ui, error));
    EXPECT_EQ(error, "No UI context");
}

TEST(AutomationDispatch, CheckContextNoneAlwaysPasses)
{
    App            app;
    std::string    error;
    WindowUIContext* ui = nullptr;
    EXPECT_TRUE(check_automation_context(AutomationContextFlag::None, app, ui, error));
}

TEST(AutomationDispatch, SerializeHandlerCatalog)
{
    std::vector<AutomationHandlerEntry> catalog;
    catalog.push_back(automation_handler(
        "ping",
        "Ping the application.",
        AutomationContextFlag::None,
        {},
        [](AutomationRequest&, App&, WindowUIContext*) {}));

    const std::string json = serialize_handler_catalog(catalog);
    EXPECT_NE(json.find("\"method\":\"ping\""), std::string::npos);
    EXPECT_NE(json.find("\"description\":\"Ping the application.\""), std::string::npos);
}

TEST(AutomationDispatch, WrapHandlerRejectsMissingParams)
{
    AutomationHandlerEntry entry = automation_handler(
        "test_method",
        "Test handler.",
        AutomationContextFlag::None,
        {{.name = "command_id", .kind = ParamKind::String, .required = true}},
        [](AutomationRequest& req, App&, WindowUIContext*)
        { req.response_json = json_ok(req.id); });

    auto              fn = wrap_automation_handler(std::move(entry));
    AutomationRequest req;
    req.id          = 42;
    req.params_json = "{}";
    App             app;
    fn(req, app, nullptr);
    EXPECT_NE(req.response_json.find("\"ok\":false"), std::string::npos);
    EXPECT_NE(req.response_json.find("Missing parameter: command_id"), std::string::npos);
}
