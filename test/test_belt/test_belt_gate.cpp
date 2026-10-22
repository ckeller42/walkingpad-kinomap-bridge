#include <unity.h>

#include <vector>

#include "belt_gate.h"
#include "test_belt_gate.h"

namespace {

struct Ev {
    char kind;  // 'M' mode, 'S' start, 'P' stop, 'V' speed
    uint32_t t;
    uint8_t arg;
};

// Records every write with the time of the tick that produced it.
class FakeBelt : public belt::Belt {
public:
    uint32_t now = 0;
    std::vector<Ev> ev;
    wilink::Status status{};
    void setSpeed(uint8_t v) override { ev.push_back({'V', now, v}); }
    void setMode(wilink::Mode m) override { ev.push_back({'M', now, (uint8_t)m}); }
    void start() override { ev.push_back({'S', now, 0}); }
    void stop() override { ev.push_back({'P', now, 0}); }
    bool poll(wilink::Status& out) override { out = status; return true; }
    bool linkUp() const override { return true; }
    bool writesStatus = false;  // behave like BeltLink: a request is an FE02 write
    int ticks = 0;
    void tick(uint32_t) override { ++ticks; }
    bool haveReply = false;
    uint32_t replyAt = 0;
    bool requestStatus() override {
        if (!writesStatus) return false;
        ev.push_back({'R', now, 0});
        haveReply = true;  // the reply arrives immediately in this model
        replyAt = now;
        return true;
    }
    uint32_t statusAgeMs() const override {
        if (!writesStatus) return 0;
        return haveReply ? now - replyAt : UINT32_MAX;
    }
    int count(char k) const {
        int n = 0;
        for (auto& e : ev) n += e.kind == k;
        return n;
    }
};

constexpr uint32_t kGap = 1000;

void tickAt(belt::BeltGate& g, FakeBelt& f, uint32_t t) {
    f.now = t;
    g.tick(t);
}

void setStatus(FakeBelt& f, uint8_t state, uint8_t tenths) {
    f.status.beltState = state;
    f.status.tenthKmh = tenths;
}

void pollOnce(belt::BeltGate& g) {
    wilink::Status s{};
    g.poll(s);
}

}  // namespace

void test_gate_mode_then_start_are_at_least_a_gap_apart(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    g.setMode(wilink::Mode::Manual);
    g.start();
    for (uint32_t t = 0; t <= 3000; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_size_t(2, f.ev.size());
    TEST_ASSERT_EQUAL('M', f.ev[0].kind);
    TEST_ASSERT_EQUAL('S', f.ev[1].kind);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kGap, f.ev[1].t - f.ev[0].t);
}

void test_gate_stop_after_start_drops_queued_start_and_goes_on_next_clear_tick(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    tickAt(g, f, 0);
    g.setMode(wilink::Mode::Manual);
    tickAt(g, f, 20);  // mode written at 20
    g.start();         // queued, gap not clear
    tickAt(g, f, 100);
    g.stop();
    for (uint32_t t = 120; t < 1020; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_INT(0, f.count('S'));
    TEST_ASSERT_EQUAL_INT(0, f.count('P'));  // still inside the gap
    tickAt(g, f, 1020);
    TEST_ASSERT_EQUAL_INT(1, f.count('P'));
    TEST_ASSERT_EQUAL_UINT32(1020, f.ev.back().t);
}

void test_gate_ignored_stop_is_resent_until_poll_shows_stopped(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    setStatus(f, 1, 50);  // running, and the belt ignores our stops
    tickAt(g, f, 0);
    pollOnce(g);
    g.stop();
    tickAt(g, f, 20);
    TEST_ASSERT_EQUAL_INT(1, f.count('P'));
    for (uint32_t t = 1000; t <= 10000; t += 1000) {
        tickAt(g, f, t);
        pollOnce(g);
    }
    // First stop at 20 ms; polls at 4 s (>= 3 s since the stop) queue the
    // retry, sent on the next tick (5 s); the next is queued at 8 s, sent at
    // 9 s. A resend on every gap would give ~10 here.
    TEST_ASSERT_EQUAL_INT(3, f.count('P'));
    TEST_ASSERT_EQUAL_UINT32(5000, f.ev[1].t);
    TEST_ASSERT_EQUAL_UINT32(9000, f.ev[2].t);
    // Belt finally reports stopped: no further stops.
    setStatus(f, 4, 0);
    tickAt(g, f, 11000);
    pollOnce(g);
    int sent = f.count('P');
    for (uint32_t t = 12000; t <= 30000; t += 1000) {
        tickAt(g, f, t);
        pollOnce(g);
    }
    TEST_ASSERT_EQUAL_INT(sent, f.count('P'));
}

void test_gate_state_zero_also_confirms_stop(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    setStatus(f, 1, 50);
    tickAt(g, f, 0);
    pollOnce(g);
    g.stop();
    tickAt(g, f, 20);
    setStatus(f, 0, 0);
    pollOnce(g);
    for (uint32_t t = 1000; t <= 20000; t += 1000) {
        tickAt(g, f, t);
        pollOnce(g);
    }
    TEST_ASSERT_EQUAL_INT(1, f.count('P'));
}

void test_gate_two_speeds_in_one_gap_only_newest_written(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    tickAt(g, f, 0);
    g.setMode(wilink::Mode::Manual);
    tickAt(g, f, 20);  // opens a gap
    g.setSpeed(30);
    g.setSpeed(45);
    for (uint32_t t = 40; t <= 3000; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_INT(1, f.count('V'));
    TEST_ASSERT_EQUAL_UINT8(45, f.ev.back().arg);
}

void test_gate_stop_while_start_pending_clears_the_start(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    g.setMode(wilink::Mode::Manual);
    g.start();
    g.setSpeed(40);
    g.stop();
    for (uint32_t t = 0; t <= 5000; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_INT(0, f.count('M'));
    TEST_ASSERT_EQUAL_INT(0, f.count('S'));
    TEST_ASSERT_EQUAL_INT(0, f.count('V'));
    TEST_ASSERT_EQUAL_INT(1, f.count('P'));
}

void test_gate_spacing_survives_millis_wraparound(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    const uint32_t t0 = 0xFFFFFFFFu - 400;  // gap straddles the wrap
    tickAt(g, f, t0);
    g.setMode(wilink::Mode::Manual);
    tickAt(g, f, t0 + 20);
    g.start();
    uint32_t t = t0 + 40;
    for (int i = 0; i < 100; ++i, t += 20) tickAt(g, f, t);  // wraps past 0
    TEST_ASSERT_EQUAL_size_t(2, f.ev.size());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kGap, f.ev[1].t - f.ev[0].t);
    TEST_ASSERT_LESS_THAN_UINT32(kGap + 100, f.ev[1].t - f.ev[0].t);
}

void test_gate_forwards_tick_even_inside_the_gap(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    g.setMode(wilink::Mode::Manual);
    tickAt(g, f, 0);    // writes
    tickAt(g, f, 20);   // inside the gap: early return
    tickAt(g, f, 40);
    TEST_ASSERT_EQUAL_INT(3, f.ticks);
}

void test_gate_status_request_shares_the_gap_with_commands(void) {
    FakeBelt f;
    f.writesStatus = true;
    belt::BeltGate g(f, kGap);
    tickAt(g, f, 0);
    g.setMode(wilink::Mode::Manual);
    tickAt(g, f, 20);          // mode written at 20
    pollOnce(g);               // status owed
    for (uint32_t t = 40; t <= 3000; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_size_t(2, f.ev.size());
    TEST_ASSERT_EQUAL('M', f.ev[0].kind);
    TEST_ASSERT_EQUAL('R', f.ev[1].kind);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kGap, f.ev[1].t - f.ev[0].t);
}

void test_gate_queued_command_outranks_status_request(void) {
    FakeBelt f;
    f.writesStatus = true;
    belt::BeltGate g(f, kGap);
    tickAt(g, f, 0);
    f.haveReply = true; f.replyAt = 0;   // fresh enough to merely be "owed"
    g.setSpeed(30);
    tickAt(g, f, 2000);
    f.now = 2000;
    pollOnce(g);                         // age 2000: refresh owed, not urgent
    tickAt(g, f, 2020);
    TEST_ASSERT_EQUAL('V', f.ev[0].kind);          // command first
    for (uint32_t t = 2040; t <= 3200; t += 20) tickAt(g, f, t);
    TEST_ASSERT_EQUAL_size_t(2, f.ev.size());
    TEST_ASSERT_EQUAL('R', f.ev[1].kind);          // request next gap
}

// Steady state, as main.cpp drives it: tick every 20 ms, poll every 1000 ms,
// and a fake whose status request really is an FE02 write (SimBelt's is not).
// The single-poll tests could not see a request that starves every command.
static void runSteady(belt::BeltGate& g, FakeBelt& f, bool commandEverySecond,
                      uint32_t speedAtMs) {
    uint8_t v = 30;
    for (uint32_t t = 0; t <= 12000; t += 20) {
        tickAt(g, f, t);
        if (t % 1000 == 0) pollOnce(g);
        if (commandEverySecond ? (t % 1000 == 500) : (t == speedAtMs)) g.setSpeed(v += 1);
    }
}

static void assertSpaced(const FakeBelt& f) {
    for (size_t i = 1; i < f.ev.size(); ++i)
        TEST_ASSERT_GREATER_OR_EQUAL_UINT32(kGap, f.ev[i].t - f.ev[i - 1].t);
}

void test_gate_steady_state_polling_does_not_starve_commands(void) {
    FakeBelt f;
    f.writesStatus = true;
    belt::BeltGate g(f, kGap);
    runSteady(g, f, false, 4500);
    TEST_ASSERT_EQUAL_INT(1, f.count('V'));           // the speed reached the belt
    for (auto& e : f.ev)
        if (e.kind == 'V') {
            TEST_ASSERT_GREATER_OR_EQUAL_UINT32(4500, e.t);
            TEST_ASSERT_LESS_THAN_UINT32(4500 + 1100, e.t);
        }
    TEST_ASSERT_GREATER_OR_EQUAL_INT(4, f.count('R'));  // and status kept flowing
    assertSpaced(f);
}

void test_gate_constant_commands_cannot_starve_status_forever(void) {
    FakeBelt f;
    f.writesStatus = true;
    belt::BeltGate g(f, kGap);
    runSteady(g, f, true, 0);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, f.count('V'));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(2, f.count('R'));
    assertSpaced(f);
}

void test_gate_stop_outranks_status_request(void) {
    FakeBelt f;
    f.writesStatus = true;
    belt::BeltGate g(f, kGap);
    tickAt(g, f, 0);
    pollOnce(g);                  // no reply yet: urgent
    g.stop();
    tickAt(g, f, 20);
    TEST_ASSERT_EQUAL('P', f.ev[0].kind);
}

void test_gate_stop_always_writes_even_when_last_poll_showed_idle(void) {
    FakeBelt f;
    belt::BeltGate g(f, kGap);
    setStatus(f, 0, 0);
    tickAt(g, f, 0);
    pollOnce(g);
    g.stop();
    tickAt(g, f, 20);
    TEST_ASSERT_EQUAL_INT(1, f.count('P'));
}
