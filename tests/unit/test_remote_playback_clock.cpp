// Multiproc timeline sync: the window agent's timeline transport travels to
// the app as DiffOp::SET_ANIM_PLAYBACK and drives the app-side animation
// clock (ui/app/remote_playback_clock.hpp).

#include <gtest/gtest.h>

#include "ipc/codec.hpp"
#include "ipc/message.hpp"
#include "ui/app/remote_playback_clock.hpp"

using namespace spectra;

// ─── RemotePlaybackClock (app side) ─────────────────────────────────────────

TEST(RemotePlaybackClock, UnsyncedUntilFirstUpdate)
{
    RemotePlaybackClock c;
    EXPECT_FALSE(c.synced());
    c.apply(RemotePlaybackState::Playing, 0.0, 10.0);
    EXPECT_TRUE(c.synced());
}

TEST(RemotePlaybackClock, PlayingExtrapolatesFromLastUpdate)
{
    RemotePlaybackClock c;
    c.apply(RemotePlaybackState::Playing, 2.0, 100.0);
    EXPECT_DOUBLE_EQ(c.time(100.0), 2.0);
    EXPECT_DOUBLE_EQ(c.time(100.5), 2.5);
    EXPECT_TRUE(c.playing());
}

TEST(RemotePlaybackClock, PausedAndStoppedHoldThePlayhead)
{
    RemotePlaybackClock c;
    c.apply(RemotePlaybackState::Paused, 3.25, 100.0);
    EXPECT_DOUBLE_EQ(c.time(105.0), 3.25);
    EXPECT_FALSE(c.playing());

    c.apply(RemotePlaybackState::Stopped, 0.0, 106.0);
    EXPECT_DOUBLE_EQ(c.time(200.0), 0.0);
}

TEST(RemotePlaybackClock, ScrubBackwardWhilePlayingJumpsBack)
{
    RemotePlaybackClock c;
    c.apply(RemotePlaybackState::Playing, 5.0, 100.0);
    EXPECT_DOUBLE_EQ(c.time(101.0), 6.0);
    c.apply(RemotePlaybackState::Playing, 0.5, 101.0);   // scrub / loop wrap
    EXPECT_DOUBLE_EQ(c.time(101.0), 0.5);
    EXPECT_DOUBLE_EQ(c.time(101.25), 0.75);
}

TEST(RemotePlaybackClock, NeverRunsBackwardsOrNegative)
{
    RemotePlaybackClock c;
    c.apply(RemotePlaybackState::Playing, -1.0, 100.0);
    EXPECT_DOUBLE_EQ(c.time(100.0), 0.0);
    EXPECT_DOUBLE_EQ(c.time(99.0), 0.0);   // clock read before the update
}

// ─── PlaybackSendFilter (agent side) ────────────────────────────────────────

TEST(PlaybackSendFilter, SendsFirstFrameAndStateChanges)
{
    PlaybackSendFilter f;
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Stopped, 0.0, 0.0));
    EXPECT_FALSE(f.should_send(100, RemotePlaybackState::Stopped, 0.0, 0.016));
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Playing, 0.0, 0.032));
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Paused, 0.016, 0.048));
}

TEST(PlaybackSendFilter, SteadyPlaybackIsNotResent)
{
    PlaybackSendFilter f;
    double             t = 0.0, ph = 0.0;
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Playing, ph, t));
    int sent = 0;
    for (int i = 0; i < 50; ++i)   // 0.8 s of 60 fps, below the resync interval
    {
        t += 1.0 / 60.0;
        ph += 1.0 / 60.0;
        sent += f.should_send(100, RemotePlaybackState::Playing, ph, t) ? 1 : 0;
    }
    EXPECT_EQ(sent, 0);
}

TEST(PlaybackSendFilter, PeriodicResyncWhilePlaying)
{
    PlaybackSendFilter f;
    double             t = 0.0, ph = 0.0;
    f.should_send(100, RemotePlaybackState::Playing, ph, t);
    int sent = 0;
    for (int i = 0; i < 180; ++i)   // 3 s
    {
        t += 1.0 / 60.0;
        ph += 1.0 / 60.0;
        sent += f.should_send(100, RemotePlaybackState::Playing, ph, t) ? 1 : 0;
    }
    EXPECT_GE(sent, 2);
    EXPECT_LE(sent, 3);
}

TEST(PlaybackSendFilter, JumpWhilePlayingIsSent)
{
    PlaybackSendFilter f;
    f.should_send(100, RemotePlaybackState::Playing, 4.0, 10.0);
    EXPECT_FALSE(f.should_send(100, RemotePlaybackState::Playing, 4.016, 10.016));
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Playing, 0.5, 10.032));   // scrub/loop
}

TEST(PlaybackSendFilter, ScrubWhilePausedIsSent)
{
    PlaybackSendFilter f;
    f.should_send(100, RemotePlaybackState::Paused, 1.0, 0.0);
    EXPECT_FALSE(f.should_send(100, RemotePlaybackState::Paused, 1.0, 0.5));
    EXPECT_TRUE(f.should_send(100, RemotePlaybackState::Paused, 1.5, 0.6));
}

TEST(PlaybackSendFilter, FigureSwitchIsSent)
{
    PlaybackSendFilter f;
    f.should_send(100, RemotePlaybackState::Playing, 1.0, 0.0);
    EXPECT_TRUE(f.should_send(101, RemotePlaybackState::Playing, 1.016, 0.016));
}

// ─── Wire format ────────────────────────────────────────────────────────────

TEST(RemotePlaybackWire, SetAnimPlaybackRoundTrips)
{
    ipc::StateDiffPayload d;
    ipc::DiffOp           op;
    op.type         = ipc::DiffOp::Type::SET_ANIM_PLAYBACK;
    op.figure_id    = 101;
    op.series_index = static_cast<uint32_t>(RemotePlaybackState::Paused);
    op.f1           = 2.5;
    d.ops.push_back(op);

    auto back = ipc::decode_state_diff(ipc::encode_state_diff(d));
    ASSERT_TRUE(back.has_value());
    ASSERT_EQ(back->ops.size(), 1u);
    EXPECT_EQ(back->ops[0].type, ipc::DiffOp::Type::SET_ANIM_PLAYBACK);
    EXPECT_EQ(back->ops[0].figure_id, 101u);
    EXPECT_EQ(back->ops[0].series_index, static_cast<uint32_t>(RemotePlaybackState::Paused));
    EXPECT_DOUBLE_EQ(back->ops[0].f1, 2.5);
}
