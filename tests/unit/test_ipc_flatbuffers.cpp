#include <gtest/gtest.h>

#include "ipc/codec.hpp"
#include "ipc/codec_fb.hpp"
#include "ipc/message.hpp"

using namespace spectra::ipc;

// ═══════════════════════════════════════════════════════════════════════════════
// FlatBuffers Roundtrip Tests
// ═══════════════════════════════════════════════════════════════════════════════
// Each test encodes with encode_fb_*, verifies the format prefix byte is 0x01,
// strips the prefix, and decodes with decode_fb_*.

TEST(IpcFlatBuffers, HelloRoundTrip)
{
    HelloPayload p;
    p.protocol_major = 1;
    p.protocol_minor = 1;
    p.agent_build    = "test-build-42";
    p.capabilities   = CAPABILITY_FLATBUFFERS;
    p.client_type    = "python";

    auto buf = encode_fb_hello(p);
    ASSERT_GE(buf.size(), 2u);
    EXPECT_EQ(buf[0], static_cast<uint8_t>(PayloadFormat::FLATBUFFERS));

    auto decoded = decode_fb_hello(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->protocol_major, 1u);
    EXPECT_EQ(decoded->protocol_minor, 1u);
    EXPECT_EQ(decoded->agent_build, "test-build-42");
    EXPECT_EQ(decoded->capabilities, CAPABILITY_FLATBUFFERS);
    EXPECT_EQ(decoded->client_type, "python");
}

TEST(IpcFlatBuffers, WelcomeRoundTrip)
{
    WelcomePayload p;
    p.session_id   = 12345;
    p.window_id    = 67890;
    p.process_id   = 42;
    p.heartbeat_ms = 3000;
    p.mode         = "multiproc";

    auto buf     = encode_fb_welcome(p);
    auto decoded = decode_fb_welcome(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session_id, 12345u);
    EXPECT_EQ(decoded->window_id, 67890u);
    EXPECT_EQ(decoded->process_id, 42u);
    EXPECT_EQ(decoded->heartbeat_ms, 3000u);
    EXPECT_EQ(decoded->mode, "multiproc");
}

TEST(IpcFlatBuffers, RespOkRoundTrip)
{
    RespOkPayload p;
    p.request_id = 999;

    auto buf     = encode_fb_resp_ok(p);
    auto decoded = decode_fb_resp_ok(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 999u);
}

TEST(IpcFlatBuffers, RespErrRoundTrip)
{
    RespErrPayload p;
    p.request_id = 42;
    p.code       = 404;
    p.message    = "figure not found";

    auto buf     = encode_fb_resp_err(p);
    auto decoded = decode_fb_resp_err(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 42u);
    EXPECT_EQ(decoded->code, 404u);
    EXPECT_EQ(decoded->message, "figure not found");
}

TEST(IpcFlatBuffers, CmdAssignFiguresRoundTrip)
{
    CmdAssignFiguresPayload p;
    p.window_id        = 100;
    p.figure_ids       = {1, 2, 3, 4};
    p.active_figure_id = 2;

    auto buf     = encode_fb_cmd_assign_figures(p);
    auto decoded = decode_fb_cmd_assign_figures(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 100u);
    ASSERT_EQ(decoded->figure_ids.size(), 4u);
    EXPECT_EQ(decoded->figure_ids[0], 1u);
    EXPECT_EQ(decoded->figure_ids[3], 4u);
    EXPECT_EQ(decoded->active_figure_id, 2u);
}

TEST(IpcFlatBuffers, StateSnapshotRoundTrip)
{
    StateSnapshotPayload p;
    p.revision   = 7;
    p.session_id = 1001;

    SnapshotFigureState fig;
    fig.figure_id    = 42;
    fig.title        = "Figure 1";
    fig.width        = 1280;
    fig.height       = 720;
    fig.grid_rows    = 2;
    fig.grid_cols    = 2;
    fig.window_group = 1;

    SnapshotAxisState ax;
    ax.x_min        = -1.0;
    ax.x_max        = 10.0;
    ax.y_min        = -5.0;
    ax.y_max        = 5.0;
    ax.grid_visible = true;
    ax.x_label      = "Time";
    ax.y_label      = "Amplitude";
    ax.title        = "Signal";
    fig.axes.push_back(ax);

    SnapshotSeriesState s;
    s.name        = "sine";
    s.type        = "line";
    s.color_r     = 1.0f;
    s.color_g     = 0.0f;
    s.color_b     = 0.0f;
    s.color_a     = 1.0f;
    s.line_width  = 2.0f;
    s.marker_size = 0.0f;
    s.visible     = true;
    s.opacity     = 1.0f;
    s.point_count = 3;
    s.axes_index  = 0;
    s.data        = {1.0f, 2.0f, 3.0f};
    fig.series.push_back(s);

    p.figures.push_back(fig);

    auto buf     = encode_fb_state_snapshot(p);
    auto decoded = decode_fb_state_snapshot(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->revision, 7u);
    EXPECT_EQ(decoded->session_id, 1001u);
    ASSERT_EQ(decoded->figures.size(), 1u);

    const auto& df = decoded->figures[0];
    EXPECT_EQ(df.figure_id, 42u);
    EXPECT_EQ(df.title, "Figure 1");
    EXPECT_EQ(df.width, 1280u);
    EXPECT_EQ(df.height, 720u);
    ASSERT_EQ(df.axes.size(), 1u);
    EXPECT_EQ(df.axes[0].x_label, "Time");
    EXPECT_TRUE(df.axes[0].grid_visible);
    ASSERT_EQ(df.series.size(), 1u);
    EXPECT_EQ(df.series[0].name, "sine");
    EXPECT_EQ(df.series[0].point_count, 3u);
    ASSERT_EQ(df.series[0].data.size(), 3u);
    EXPECT_FLOAT_EQ(df.series[0].data[0], 1.0f);
}

TEST(IpcFlatBuffers, StateDiffRoundTrip)
{
    StateDiffPayload p;
    p.base_revision = 5;
    p.new_revision  = 6;

    DiffOp op;
    op.type       = DiffOp::Type::SET_AXIS_LIMITS;
    op.figure_id  = 1;
    op.axes_index = 0;
    op.f1         = -10.0;
    op.f2         = 10.0;
    op.str_val    = "x_range";
    p.ops.push_back(op);

    auto buf     = encode_fb_state_diff(p);
    auto decoded = decode_fb_state_diff(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->base_revision, 5u);
    EXPECT_EQ(decoded->new_revision, 6u);
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_LIMITS);
    EXPECT_EQ(decoded->ops[0].figure_id, 1u);
    EXPECT_DOUBLE_EQ(decoded->ops[0].f1, -10.0);
    EXPECT_EQ(decoded->ops[0].str_val, "x_range");
}

TEST(IpcFlatBuffers, ReqCreateFigureRoundTrip)
{
    ReqCreateFigurePayload p;
    p.title  = "My Plot";
    p.width  = 1920;
    p.height = 1080;

    auto buf     = encode_fb_req_create_figure(p);
    auto decoded = decode_fb_req_create_figure(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->title, "My Plot");
    EXPECT_EQ(decoded->width, 1920u);
    EXPECT_EQ(decoded->height, 1080u);
}

TEST(IpcFlatBuffers, ReqSetDataRoundTrip)
{
    ReqSetDataPayload p;
    p.figure_id    = 1;
    p.series_index = 2;
    p.dtype        = 1;
    p.data         = {0.0f, 1.5f, 3.0f, 4.5f, 6.0f};

    auto buf     = encode_fb_req_set_data(p);
    auto decoded = decode_fb_req_set_data(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_EQ(decoded->series_index, 2u);
    EXPECT_EQ(decoded->dtype, 1u);
    ASSERT_EQ(decoded->data.size(), 5u);
    EXPECT_FLOAT_EQ(decoded->data[2], 3.0f);
}

TEST(IpcFlatBuffers, ReqUpdatePropertyRoundTrip)
{
    ReqUpdatePropertyPayload p;
    p.figure_id    = 10;
    p.axes_index   = 1;
    p.series_index = 2;
    p.property     = "line_width";
    p.f1           = 3.5;
    p.bool_val     = true;
    p.str_val      = "dashed";

    auto buf     = encode_fb_req_update_property(p);
    auto decoded = decode_fb_req_update_property(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 10u);
    EXPECT_EQ(decoded->axes_index, 1u);
    EXPECT_EQ(decoded->property, "line_width");
    EXPECT_DOUBLE_EQ(decoded->f1, 3.5);
    EXPECT_TRUE(decoded->bool_val);
    EXPECT_EQ(decoded->str_val, "dashed");
}

TEST(IpcFlatBuffers, EvtInputRoundTrip)
{
    EvtInputPayload p;
    p.window_id  = 5;
    p.input_type = EvtInputPayload::InputType::MOUSE_BUTTON;
    p.key        = 42;
    p.mods       = 3;
    p.x          = 100.5;
    p.y          = 200.5;
    p.figure_id  = 7;
    p.axes_index = 0;

    auto buf     = encode_fb_evt_input(p);
    auto decoded = decode_fb_evt_input(strip_fb_prefix(buf));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->window_id, 5u);
    EXPECT_EQ(decoded->input_type, EvtInputPayload::InputType::MOUSE_BUTTON);
    EXPECT_DOUBLE_EQ(decoded->x, 100.5);
    EXPECT_DOUBLE_EQ(decoded->y, 200.5);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Cross-format: FB-encode → TLV-aware decode (auto-detect)
// ═══════════════════════════════════════════════════════════════════════════════
// These tests verify that the existing decode_* functions correctly dispatch
// to FlatBuffers when the payload starts with 0x01.

TEST(IpcCrossCodec, HelloFbToTlvDecode)
{
    HelloPayload p;
    p.protocol_major = 1;
    p.protocol_minor = 1;
    p.agent_build    = "cross-test";
    p.capabilities   = CAPABILITY_FLATBUFFERS;
    p.client_type    = "agent";

    // Encode with FlatBuffers (includes 0x01 prefix)
    auto buf = encode_fb_hello(p);

    // Decode with the unified function (should auto-detect FB)
    auto decoded = decode_hello(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->agent_build, "cross-test");
    EXPECT_EQ(decoded->capabilities, CAPABILITY_FLATBUFFERS);
}

TEST(IpcCrossCodec, WelcomeFbToTlvDecode)
{
    WelcomePayload p;
    p.session_id   = 777;
    p.window_id    = 888;
    p.process_id   = 99;
    p.heartbeat_ms = 2000;
    p.mode         = "inproc";

    auto buf     = encode_fb_welcome(p);
    auto decoded = decode_welcome(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session_id, 777u);
    EXPECT_EQ(decoded->mode, "inproc");
}

TEST(IpcCrossCodec, RespErrFbToTlvDecode)
{
    RespErrPayload p;
    p.request_id = 10;
    p.code       = 500;
    p.message    = "internal error";

    auto buf     = encode_fb_resp_err(p);
    auto decoded = decode_resp_err(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->code, 500u);
    EXPECT_EQ(decoded->message, "internal error");
}

TEST(IpcCrossCodec, StateSnapshotFbToTlvDecode)
{
    StateSnapshotPayload p;
    p.revision   = 42;
    p.session_id = 100;
    p.figures.push_back(SnapshotFigureState{});
    p.figures[0].figure_id = 1;
    p.figures[0].title     = "Test";

    auto buf     = encode_fb_state_snapshot(p);
    auto decoded = decode_state_snapshot(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->revision, 42u);
    ASSERT_EQ(decoded->figures.size(), 1u);
    EXPECT_EQ(decoded->figures[0].title, "Test");
}

TEST(IpcCrossCodec, ReqSetDataFbToTlvDecode)
{
    ReqSetDataPayload p;
    p.figure_id    = 5;
    p.series_index = 0;
    p.data         = {1.0f, 2.0f, 3.0f};

    auto buf     = encode_fb_req_set_data(p);
    auto decoded = decode_req_set_data(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 5u);
    ASSERT_EQ(decoded->data.size(), 3u);
    EXPECT_FLOAT_EQ(decoded->data[1], 2.0f);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Legacy TLV still works (regression guard)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(IpcCrossCodec, LegacyTlvHelloStillDecodes)
{
    // Encode with legacy TLV
    HelloPayload p;
    p.agent_build = "legacy-test";
    p.client_type = "python";

    auto buf = encode_hello(p);
    // Should NOT start with 0x01
    ASSERT_FALSE(buf.empty());
    EXPECT_NE(buf[0], static_cast<uint8_t>(PayloadFormat::FLATBUFFERS));

    // Legacy decode should still work
    auto decoded = decode_hello(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->agent_build, "legacy-test");
}

TEST(IpcCrossCodec, LegacyTlvWelcomeStillDecodes)
{
    WelcomePayload p;
    p.session_id = 555;
    p.mode       = "multiproc";

    auto buf     = encode_welcome(p);
    auto decoded = decode_welcome(buf);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session_id, 555u);
    EXPECT_EQ(decoded->mode, "multiproc");
}
