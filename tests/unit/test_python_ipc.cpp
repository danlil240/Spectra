#include <gtest/gtest.h>

#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "daemon/client_router.hpp"
#include "daemon/figure_model.hpp"

using namespace spectra::ipc;
using namespace spectra::daemon;

// ─── Client classification ───────────────────────────────────────────────────

TEST(ClientRouter, ClassifyPython)
{
    HelloPayload hello;
    hello.client_type = "python";
    EXPECT_EQ(classify_client(hello), ClientType::PYTHON);
}

TEST(ClientRouter, ClassifyAgent)
{
    HelloPayload hello;
    hello.client_type = "agent";
    EXPECT_EQ(classify_client(hello), ClientType::AGENT);
}

TEST(ClientRouter, ClassifyLegacyAgent)
{
    HelloPayload hello;
    // No client_type set — should default to AGENT
    EXPECT_EQ(classify_client(hello), ClientType::AGENT);
}

TEST(ClientRouter, ClassifyApp)
{
    HelloPayload hello;
    hello.agent_build = "spectra-app v0.1";
    EXPECT_EQ(classify_client(hello), ClientType::APP);
}

TEST(ClientRouter, IsPythonRequest)
{
    EXPECT_TRUE(is_python_request(MessageType::REQ_CREATE_FIGURE));
    EXPECT_TRUE(is_python_request(MessageType::REQ_SET_DATA));
    EXPECT_TRUE(is_python_request(MessageType::REQ_APPEND_DATA));
    EXPECT_TRUE(is_python_request(MessageType::REQ_SHOW));
    EXPECT_TRUE(is_python_request(MessageType::REQ_DISCONNECT));
    EXPECT_FALSE(is_python_request(MessageType::HELLO));
    EXPECT_FALSE(is_python_request(MessageType::RESP_FIGURE_CREATED));
}

TEST(ClientRouter, IsPythonResponse)
{
    EXPECT_TRUE(is_python_response(MessageType::RESP_FIGURE_CREATED));
    EXPECT_TRUE(is_python_response(MessageType::RESP_AXES_CREATED));
    EXPECT_TRUE(is_python_response(MessageType::EVT_WINDOW_CLOSED));
    EXPECT_FALSE(is_python_response(MessageType::HELLO));
    EXPECT_FALSE(is_python_response(MessageType::REQ_CREATE_FIGURE));
}

// ─── Hello with client_type ──────────────────────────────────────────────────

TEST(HelloClientType, EncodeDecodeRoundtrip)
{
    HelloPayload orig;
    orig.protocol_major = 1;
    orig.protocol_minor = 0;
    orig.agent_build = "test";
    orig.capabilities = 0;
    orig.client_type = "python";

    auto encoded = encode_hello(orig);
    auto decoded = decode_hello(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->client_type, "python");
    EXPECT_EQ(decoded->agent_build, "test");
    EXPECT_EQ(decoded->protocol_major, 1);
}

TEST(HelloClientType, EmptyClientType)
{
    HelloPayload orig;
    orig.client_type = "";  // legacy

    auto encoded = encode_hello(orig);
    auto decoded = decode_hello(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->client_type, "");
}

TEST(HelloClientType, BackwardCompatible)
{
    // Old encoder (no client_type) should still decode fine
    PayloadEncoder enc;
    enc.put_u16(TAG_PROTOCOL_MAJOR, 1);
    enc.put_u16(TAG_PROTOCOL_MINOR, 0);
    enc.put_string(TAG_AGENT_BUILD, "old-agent");
    enc.put_u32(TAG_CAPABILITIES, 0);
    auto data = enc.take();

    auto decoded = decode_hello(data);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->client_type, "");  // not present → empty
    EXPECT_EQ(decoded->agent_build, "old-agent");
}

// ─── Python request payload round-trips ──────────────────────────────────────

TEST(PythonPayloads, ReqCreateFigure)
{
    ReqCreateFigurePayload orig;
    orig.title = "Test Figure";
    orig.width = 800;
    orig.height = 600;

    auto encoded = encode_req_create_figure(orig);
    auto decoded = decode_req_create_figure(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->title, "Test Figure");
    EXPECT_EQ(decoded->width, 800u);
    EXPECT_EQ(decoded->height, 600u);
}

TEST(PythonPayloads, ReqDestroyFigure)
{
    ReqDestroyFigurePayload orig;
    orig.figure_id = 42;

    auto encoded = encode_req_destroy_figure(orig);
    auto decoded = decode_req_destroy_figure(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 42u);
}

TEST(PythonPayloads, ReqCreateAxes)
{
    ReqCreateAxesPayload orig;
    orig.figure_id = 1;
    orig.grid_rows = 2;
    orig.grid_cols = 3;
    orig.grid_index = 4;

    auto encoded = encode_req_create_axes(orig);
    auto decoded = decode_req_create_axes(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_EQ(decoded->grid_rows, 2);
    EXPECT_EQ(decoded->grid_cols, 3);
    EXPECT_EQ(decoded->grid_index, 4);
}

TEST(PythonPayloads, ReqAddSeries)
{
    ReqAddSeriesPayload orig;
    orig.figure_id = 10;
    orig.axes_index = 0;
    orig.series_type = "scatter";
    orig.label = "data points";

    auto encoded = encode_req_add_series(orig);
    auto decoded = decode_req_add_series(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 10u);
    EXPECT_EQ(decoded->axes_index, 0u);
    EXPECT_EQ(decoded->series_type, "scatter");
    EXPECT_EQ(decoded->label, "data points");
}

TEST(PythonPayloads, ReqRemoveSeries)
{
    ReqRemoveSeriesPayload orig;
    orig.figure_id = 5;
    orig.series_index = 2;

    auto encoded = encode_req_remove_series(orig);
    auto decoded = decode_req_remove_series(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 5u);
    EXPECT_EQ(decoded->series_index, 2u);
}

TEST(PythonPayloads, ReqSetData)
{
    ReqSetDataPayload orig;
    orig.figure_id = 1;
    orig.series_index = 0;
    orig.dtype = 0;
    orig.data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};

    auto encoded = encode_req_set_data(orig);
    auto decoded = decode_req_set_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_EQ(decoded->series_index, 0u);
    EXPECT_EQ(decoded->dtype, 0);
    ASSERT_EQ(decoded->data.size(), 6u);
    EXPECT_FLOAT_EQ(decoded->data[0], 1.0f);
    EXPECT_FLOAT_EQ(decoded->data[5], 6.0f);
}

TEST(PythonPayloads, ReqSetDataEmpty)
{
    ReqSetDataPayload orig;
    orig.figure_id = 1;
    orig.series_index = 0;
    // data is empty

    auto encoded = encode_req_set_data(orig);
    auto decoded = decode_req_set_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->data.empty());
}

TEST(PythonPayloads, ReqAppendData)
{
    ReqAppendDataPayload orig;
    orig.figure_id = 42;
    orig.series_index = 1;
    orig.data = {1.0f, 10.0f, 2.0f, 20.0f, 3.0f, 30.0f};

    auto encoded = encode_req_append_data(orig);
    auto decoded = decode_req_append_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 42u);
    EXPECT_EQ(decoded->series_index, 1u);
    ASSERT_EQ(decoded->data.size(), 6u);
    EXPECT_FLOAT_EQ(decoded->data[0], 1.0f);
    EXPECT_FLOAT_EQ(decoded->data[1], 10.0f);
    EXPECT_FLOAT_EQ(decoded->data[5], 30.0f);
}

TEST(PythonPayloads, ReqAppendDataEmpty)
{
    ReqAppendDataPayload orig;
    orig.figure_id = 1;
    orig.series_index = 0;
    // data is empty

    auto encoded = encode_req_append_data(orig);
    auto decoded = decode_req_append_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_TRUE(decoded->data.empty());
}

TEST(PythonPayloads, ReqAppendDataSinglePoint)
{
    ReqAppendDataPayload orig;
    orig.figure_id = 99;
    orig.series_index = 3;
    orig.data = {5.0f, 10.0f};

    auto encoded = encode_req_append_data(orig);
    auto decoded = decode_req_append_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 99u);
    EXPECT_EQ(decoded->series_index, 3u);
    ASSERT_EQ(decoded->data.size(), 2u);
    EXPECT_FLOAT_EQ(decoded->data[0], 5.0f);
    EXPECT_FLOAT_EQ(decoded->data[1], 10.0f);
}

TEST(PythonPayloads, ReqUpdateProperty)
{
    ReqUpdatePropertyPayload orig;
    orig.figure_id = 1;
    orig.axes_index = 0;
    orig.series_index = 2;
    orig.property = "color";
    orig.f1 = 1.0f;
    orig.f2 = 0.5f;
    orig.f3 = 0.0f;
    orig.f4 = 1.0f;
    orig.bool_val = false;
    orig.str_val = "red";

    auto encoded = encode_req_update_property(orig);
    auto decoded = decode_req_update_property(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_EQ(decoded->series_index, 2u);
    EXPECT_EQ(decoded->property, "color");
    EXPECT_FLOAT_EQ(decoded->f1, 1.0f);
    EXPECT_FLOAT_EQ(decoded->f2, 0.5f);
    EXPECT_EQ(decoded->str_val, "red");
}

TEST(PythonPayloads, ReqShow)
{
    ReqShowPayload orig;
    orig.figure_id = 7;

    auto encoded = encode_req_show(orig);
    auto decoded = decode_req_show(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 7u);
}

TEST(PythonPayloads, ReqCloseFigure)
{
    ReqCloseFigurePayload orig;
    orig.figure_id = 99;

    auto encoded = encode_req_close_figure(orig);
    auto decoded = decode_req_close_figure(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 99u);
}

TEST(PythonPayloads, ReqReconnect)
{
    ReqReconnectPayload orig;
    orig.session_id = 42;
    orig.session_token = "abc123";

    auto encoded = encode_req_reconnect(orig);
    auto decoded = decode_req_reconnect(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->session_id, 42u);
    EXPECT_EQ(decoded->session_token, "abc123");
}

// ─── Python response payload round-trips ─────────────────────────────────────

TEST(PythonPayloads, RespFigureCreated)
{
    RespFigureCreatedPayload orig;
    orig.request_id = 10;
    orig.figure_id = 42;

    auto encoded = encode_resp_figure_created(orig);
    auto decoded = decode_resp_figure_created(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 10u);
    EXPECT_EQ(decoded->figure_id, 42u);
}

TEST(PythonPayloads, RespAxesCreated)
{
    RespAxesCreatedPayload orig;
    orig.request_id = 11;
    orig.axes_index = 3;

    auto encoded = encode_resp_axes_created(orig);
    auto decoded = decode_resp_axes_created(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 11u);
    EXPECT_EQ(decoded->axes_index, 3u);
}

TEST(PythonPayloads, RespSeriesAdded)
{
    RespSeriesAddedPayload orig;
    orig.request_id = 12;
    orig.series_index = 5;

    auto encoded = encode_resp_series_added(orig);
    auto decoded = decode_resp_series_added(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 12u);
    EXPECT_EQ(decoded->series_index, 5u);
}

TEST(PythonPayloads, RespFigureList)
{
    RespFigureListPayload orig;
    orig.request_id = 13;
    orig.figure_ids = {100, 200, 300};

    auto encoded = encode_resp_figure_list(orig);
    auto decoded = decode_resp_figure_list(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 13u);
    ASSERT_EQ(decoded->figure_ids.size(), 3u);
    EXPECT_EQ(decoded->figure_ids[0], 100u);
    EXPECT_EQ(decoded->figure_ids[1], 200u);
    EXPECT_EQ(decoded->figure_ids[2], 300u);
}

TEST(PythonPayloads, RespFigureListEmpty)
{
    RespFigureListPayload orig;
    orig.request_id = 14;

    auto encoded = encode_resp_figure_list(orig);
    auto decoded = decode_resp_figure_list(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->request_id, 14u);
    EXPECT_TRUE(decoded->figure_ids.empty());
}

// ─── Python event payload round-trips ────────────────────────────────────────

TEST(PythonPayloads, EvtWindowClosed)
{
    EvtWindowClosedPayload orig;
    orig.figure_id = 1;
    orig.window_id = 2;
    orig.reason = "user_close";

    auto encoded = encode_evt_window_closed(orig);
    auto decoded = decode_evt_window_closed(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 1u);
    EXPECT_EQ(decoded->window_id, 2u);
    EXPECT_EQ(decoded->reason, "user_close");
}

TEST(PythonPayloads, EvtFigureDestroyed)
{
    EvtFigureDestroyedPayload orig;
    orig.figure_id = 99;
    orig.reason = "timeout";

    auto encoded = encode_evt_figure_destroyed(orig);
    auto decoded = decode_evt_figure_destroyed(encoded);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->figure_id, 99u);
    EXPECT_EQ(decoded->reason, "timeout");
}

// ─── Large data transfer ─────────────────────────────────────────────────────

TEST(PythonPayloads, ReqSetDataLarge)
{
    ReqSetDataPayload orig;
    orig.figure_id = 1;
    orig.series_index = 0;
    orig.dtype = 0;

    // 100K points (200K floats for x,y interleaved)
    orig.data.resize(200000);
    for (size_t i = 0; i < orig.data.size(); ++i)
        orig.data[i] = static_cast<float>(i) * 0.001f;

    auto encoded = encode_req_set_data(orig);
    auto decoded = decode_req_set_data(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->data.size(), 200000u);
    EXPECT_FLOAT_EQ(decoded->data[0], 0.0f);
    EXPECT_FLOAT_EQ(decoded->data[199999], 199.999f);
}

// ─── DiffOp round-trip for new types ─────────────────────────────────────────

TEST(DiffOpRoundTrip, SetAxisXlabel)
{
    StateDiffPayload orig;
    orig.base_revision = 1;
    orig.new_revision = 2;
    DiffOp op;
    op.type = DiffOp::Type::SET_AXIS_XLABEL;
    op.figure_id = 42;
    op.axes_index = 0;
    op.str_val = "Time (s)";
    orig.ops.push_back(op);

    auto encoded = encode_state_diff(orig);
    auto decoded = decode_state_diff(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_XLABEL);
    EXPECT_EQ(decoded->ops[0].figure_id, 42u);
    EXPECT_EQ(decoded->ops[0].axes_index, 0u);
    EXPECT_EQ(decoded->ops[0].str_val, "Time (s)");
}

TEST(DiffOpRoundTrip, SetAxisYlabel)
{
    StateDiffPayload orig;
    orig.base_revision = 1;
    orig.new_revision = 2;
    DiffOp op;
    op.type = DiffOp::Type::SET_AXIS_YLABEL;
    op.figure_id = 42;
    op.axes_index = 1;
    op.str_val = "Amplitude";
    orig.ops.push_back(op);

    auto encoded = encode_state_diff(orig);
    auto decoded = decode_state_diff(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_YLABEL);
    EXPECT_EQ(decoded->ops[0].axes_index, 1u);
    EXPECT_EQ(decoded->ops[0].str_val, "Amplitude");
}

TEST(DiffOpRoundTrip, SetAxisTitle)
{
    StateDiffPayload orig;
    orig.base_revision = 5;
    orig.new_revision = 6;
    DiffOp op;
    op.type = DiffOp::Type::SET_AXIS_TITLE;
    op.figure_id = 1;
    op.axes_index = 0;
    op.str_val = "Sensor Data";
    orig.ops.push_back(op);

    auto encoded = encode_state_diff(orig);
    auto decoded = decode_state_diff(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_AXIS_TITLE);
    EXPECT_EQ(decoded->ops[0].str_val, "Sensor Data");
}

TEST(DiffOpRoundTrip, SetSeriesLabel)
{
    StateDiffPayload orig;
    orig.base_revision = 10;
    orig.new_revision = 11;
    DiffOp op;
    op.type = DiffOp::Type::SET_SERIES_LABEL;
    op.figure_id = 1;
    op.series_index = 2;
    op.str_val = "sin(x)";
    orig.ops.push_back(op);

    auto encoded = encode_state_diff(orig);
    auto decoded = decode_state_diff(encoded);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->ops.size(), 1u);
    EXPECT_EQ(decoded->ops[0].type, DiffOp::Type::SET_SERIES_LABEL);
    EXPECT_EQ(decoded->ops[0].series_index, 2u);
    EXPECT_EQ(decoded->ops[0].str_val, "sin(x)");
}

// ─── FigureModel unit tests ─────────────────────────────────────────────────

TEST(FigureModel, CreateFigureAndAddAxes)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test Figure");
    EXPECT_NE(fig_id, 0u);
    EXPECT_EQ(model.figure_count(), 1u);
    EXPECT_TRUE(model.has_figure(fig_id));

    auto axes_idx = model.add_axes(fig_id, 1, 1, 1);
    EXPECT_EQ(axes_idx, 0u);
}

TEST(FigureModel, AddSeriesAndSetData)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);
    auto series_idx = model.add_series(fig_id, "line1", "line");
    EXPECT_EQ(series_idx, 0u);

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    auto op = model.set_series_data(fig_id, series_idx, data);
    EXPECT_EQ(op.type, DiffOp::Type::SET_SERIES_DATA);
    EXPECT_EQ(op.data.size(), 4u);
}

TEST(FigureModel, AppendSeriesData)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);
    model.add_series(fig_id, "line1", "line");

    model.set_series_data(fig_id, 0, {1.0f, 2.0f});
    auto op = model.append_series_data(fig_id, 0, {3.0f, 4.0f});
    EXPECT_EQ(op.type, DiffOp::Type::SET_SERIES_DATA);
    ASSERT_EQ(op.data.size(), 4u);
    EXPECT_FLOAT_EQ(op.data[0], 1.0f);
    EXPECT_FLOAT_EQ(op.data[2], 3.0f);
}

TEST(FigureModel, SetAxisXlabel)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);

    auto op = model.set_axis_xlabel(fig_id, 0, "Time (s)");
    EXPECT_EQ(op.type, DiffOp::Type::SET_AXIS_XLABEL);
    EXPECT_EQ(op.str_val, "Time (s)");
}

TEST(FigureModel, SetAxisYlabel)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);

    auto op = model.set_axis_ylabel(fig_id, 0, "Amplitude");
    EXPECT_EQ(op.type, DiffOp::Type::SET_AXIS_YLABEL);
    EXPECT_EQ(op.str_val, "Amplitude");
}

TEST(FigureModel, SetAxisTitle)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);

    auto op = model.set_axis_title(fig_id, 0, "Sensor Data");
    EXPECT_EQ(op.type, DiffOp::Type::SET_AXIS_TITLE);
    EXPECT_EQ(op.str_val, "Sensor Data");
}

TEST(FigureModel, SetSeriesLabel)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);
    model.add_series(fig_id, "old_name", "line");

    auto op = model.set_series_label(fig_id, 0, "new_name");
    EXPECT_EQ(op.type, DiffOp::Type::SET_SERIES_LABEL);
    EXPECT_EQ(op.str_val, "new_name");
}

TEST(FigureModel, SetSeriesColor)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);
    model.add_series(fig_id, "s1", "line");

    auto op = model.set_series_color(fig_id, 0, 1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_EQ(op.type, DiffOp::Type::SET_SERIES_COLOR);
    EXPECT_FLOAT_EQ(op.f1, 1.0f);
    EXPECT_FLOAT_EQ(op.f2, 0.0f);
}

TEST(FigureModel, ApplyDiffOpXlabel)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);

    DiffOp op;
    op.type = DiffOp::Type::SET_AXIS_XLABEL;
    op.figure_id = fig_id;
    op.axes_index = 0;
    op.str_val = "Applied Label";
    EXPECT_TRUE(model.apply_diff_op(op));
}

TEST(FigureModel, ApplyDiffOpSeriesLabel)
{
    spectra::daemon::FigureModel model;
    auto fig_id = model.create_figure("Test");
    model.add_axes(fig_id, 1, 1, 1);
    model.add_series(fig_id, "orig", "line");

    DiffOp op;
    op.type = DiffOp::Type::SET_SERIES_LABEL;
    op.figure_id = fig_id;
    op.series_index = 0;
    op.str_val = "renamed";
    EXPECT_TRUE(model.apply_diff_op(op));
}

TEST(FigureModel, RevisionBumpsOnMutation)
{
    spectra::daemon::FigureModel model;
    auto r0 = model.revision();
    auto fig_id = model.create_figure("Test");
    auto r1 = model.revision();
    EXPECT_GT(r1, r0);

    model.add_axes(fig_id, 1, 1, 1);
    auto r2 = model.revision();
    EXPECT_GT(r2, r1);

    model.set_axis_xlabel(fig_id, 0, "x");
    auto r3 = model.revision();
    EXPECT_GT(r3, r2);
}

// ─── Message type range ──────────────────────────────────────────────────────

TEST(PythonMessageTypes, RangeCheck)
{
    // All Python message types should be in 0x0500-0x05FF
    auto check = [](MessageType t) {
        auto v = static_cast<uint16_t>(t);
        return v >= 0x0500 && v <= 0x05FF;
    };

    EXPECT_TRUE(check(MessageType::REQ_CREATE_FIGURE));
    EXPECT_TRUE(check(MessageType::REQ_DESTROY_FIGURE));
    EXPECT_TRUE(check(MessageType::REQ_CREATE_AXES));
    EXPECT_TRUE(check(MessageType::REQ_ADD_SERIES));
    EXPECT_TRUE(check(MessageType::REQ_REMOVE_SERIES));
    EXPECT_TRUE(check(MessageType::REQ_SET_DATA));
    EXPECT_TRUE(check(MessageType::REQ_UPDATE_PROPERTY));
    EXPECT_TRUE(check(MessageType::REQ_SHOW));
    EXPECT_TRUE(check(MessageType::REQ_CLOSE_FIGURE));
    EXPECT_TRUE(check(MessageType::REQ_APPEND_DATA));
    EXPECT_TRUE(check(MessageType::REQ_GET_SNAPSHOT));
    EXPECT_TRUE(check(MessageType::REQ_LIST_FIGURES));
    EXPECT_TRUE(check(MessageType::REQ_RECONNECT));
    EXPECT_TRUE(check(MessageType::REQ_DISCONNECT));
    EXPECT_TRUE(check(MessageType::RESP_FIGURE_CREATED));
    EXPECT_TRUE(check(MessageType::RESP_AXES_CREATED));
    EXPECT_TRUE(check(MessageType::RESP_SERIES_ADDED));
    EXPECT_TRUE(check(MessageType::RESP_SNAPSHOT));
    EXPECT_TRUE(check(MessageType::RESP_FIGURE_LIST));
    EXPECT_TRUE(check(MessageType::EVT_WINDOW_CLOSED));
    EXPECT_TRUE(check(MessageType::EVT_FIGURE_DESTROYED));
}
