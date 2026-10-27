// Automation add_series handler: JSON-escaped labels arrive decoded on the series.

#include <gtest/gtest.h>

#include <spectra/app.hpp>
#include <spectra/axes.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include <string>

#include "ui/automation/automation_handler.hpp"
#include "ui/automation/automation_json.hpp"
#include "ui/automation/automation_server.hpp"

using namespace spectra;

namespace
{

const AutomationHandlerEntry* find_handler(const std::vector<AutomationHandlerEntry>& v,
                                           const std::string&                         method)
{
    for (const auto& e : v)
        if (e.method == method)
            return &e;
    return nullptr;
}

}   // namespace

TEST(AutomationAddSeries, EscapedLabelIsDecodedOnSeries)
{
    App     app({.headless = true, .socket_path = ""});
    Figure& fig    = app.figure({.width = 320, .height = 240});
    auto    fig_id = app.figure_registry().find_id(&fig);
    ASSERT_NE(fig_id, 0u);

    const auto  handlers = make_figure_handlers();
    const auto* add      = find_handler(handlers, "add_series");
    ASSERT_NE(add, nullptr);

    AutomationRequest req;
    req.id          = 7;
    req.method      = "add_series";
    req.params_json = "{\"figure_id\":" + std::to_string(fig_id)
                      + R"(,"type":"line","x":[0,1,2],"y":[0,1,4],)"
                      + R"("label":"Signal A \u2014 \"\u0394t\" \ud83d\ude00\nline2"})";
    add->handler(req, &app, nullptr);

    EXPECT_NE(req.response_json.find("\"ok\":true"), std::string::npos) << req.response_json;
    ASSERT_FALSE(fig.axes().empty());
    ASSERT_EQ(fig.axes()[0]->series().size(), 1u);
    EXPECT_EQ(fig.axes()[0]->series()[0]->label(), "Signal A — \"Δt\" 😀\nline2");
}
