// SPDX-License-Identifier: MIT

// Unit tests for the length analysis's loop finder (src/rip/loop_finder.h), with made-up tracks. Each event takes 1 ms.

#include "rip/loop_finder.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "check.h"

using namespace gcsf::rip;

namespace
{

using Verdict = LoopFinder::Verdict;

// Commands by their first byte. A note changes the sound. The others don't.
constexpr uint8_t kNote = 0x3c;
constexpr uint8_t kReadRegister = 0xda;
constexpr uint8_t kJumpIf = 0xc8;
constexpr uint8_t kWait = 0xf0;

// The tracks: the sound's root track, and one that opens after it.
constexpr uint32_t kRoot = 0x1000;
constexpr uint32_t kLater = 0x2000;

// Feeds events to a LoopFinder as a sound that has played all along.
class Feeder
{
public:
    explicit Feeder(LoopFinder& finder) : finder_(finder)
    {
    }

    // `track` runs the command at `at`.
    void Command(uint32_t track, uint32_t at, uint8_t command)
    {
        Take({TrackEvent::Kind::kCommand, track, at, 0, 0, command, frame_});
    }

    // `track` jumps back to `to` from the jump command that ends at `at`. Returns the jump's verdict.
    Verdict Jump(uint32_t track, uint32_t at, uint32_t to)
    {
        return Take({TrackEvent::Kind::kJump, track, at, to, 0, 0xc7, frame_});
    }

    uint64_t Frame() const
    {
        return frame_;
    }

private:
    Verdict Take(const TrackEvent& event)
    {
        const Verdict verdict = finder_.Take(event, {true, frame_});
        frame_ += 32;

        return verdict;
    }

    LoopFinder& finder_;
    uint64_t frame_ = 0;
};

// One pass of a loop that polls a register: a read, the conditional jump out to 0x22 (not taken), a wait, and the jump
// back. The pass changes nothing.
Verdict PollPass(Feeder& feeder, uint32_t track)
{
    feeder.Command(track, 0x12, kReadRegister);
    feeder.Command(track, 0x17, kJumpIf);
    feeder.Command(track, 0x1c, kWait);

    return feeder.Jump(track, 0x22, 0x12);
}

// The polling loop's last pass: the condition holds, and the conditional jump leaves the loop for 0x22.
void LeavePoll(Feeder& feeder, uint32_t track)
{
    feeder.Command(track, 0x12, kReadRegister);
    feeder.Command(track, 0x17, kJumpIf);
}

void TestLoop()
{
    // A note and a wait, then the jump back to the note.
    LoopFinder finder(kRoot);
    Feeder feeder(finder);
    std::vector<Verdict> verdicts;

    for (int pass = 0; pass < 2; pass++)
    {
        feeder.Command(kRoot, 0x10, kNote);
        feeder.Command(kRoot, 0x12, kWait);
        verdicts.push_back(feeder.Jump(kRoot, 0x18, 0x10));
    }

    GCSF_CHECK(verdicts == (std::vector{Verdict::kNone, Verdict::kLoops}));
}

void TestLoopAroundPoll()
{
    // A note, a loop that polls three times, a wait, then the jump back to the note. The polling loop's jumps back
    // neither hide the note from the outer pass nor start the outer loop's count again.
    LoopFinder finder(kRoot);
    Feeder feeder(finder);
    std::vector<Verdict> polls;
    std::vector<Verdict> loops;

    for (int pass = 0; pass < 2; pass++)
    {
        feeder.Command(kRoot, 0x10, kNote);
        for (int poll = 0; poll < 3; poll++)
        {
            polls.push_back(PollPass(feeder, kRoot));
        }

        LeavePoll(feeder, kRoot);
        feeder.Command(kRoot, 0x22, kWait);
        loops.push_back(feeder.Jump(kRoot, 0x28, 0x10));
    }

    GCSF_CHECK(polls == std::vector(6, Verdict::kNone));
    GCSF_CHECK(loops == (std::vector{Verdict::kNone, Verdict::kLoops}));
}

void TestPollBetweenPasses()
{
    // A loop's first pass, then polling outside the loop, then the loop again. The polling starts the loop's count
    // again, so the loop loops at the second jump back after the polling.
    LoopFinder finder(kRoot);
    Feeder feeder(finder);
    std::vector<Verdict> polls;
    std::vector<Verdict> loops;
    const auto pass = [&feeder, &loops]
    {
        feeder.Command(kRoot, 0x30, kNote);
        feeder.Command(kRoot, 0x32, kWait);
        loops.push_back(feeder.Jump(kRoot, 0x38, 0x30));
    };

    pass();
    polls.push_back(PollPass(feeder, kRoot));
    polls.push_back(PollPass(feeder, kRoot));
    LeavePoll(feeder, kRoot);
    pass();
    pass();

    GCSF_CHECK(polls == std::vector(2, Verdict::kNone));
    GCSF_CHECK(loops == (std::vector{Verdict::kNone, Verdict::kNone, Verdict::kLoops}));
}

void TestPollHolds()
{
    // A note, then polling for 12 s. The polling loop's passes aren't moves, so the sound holds 10 s after the note.
    LoopFinder finder(kRoot);
    Feeder feeder(finder);
    std::vector<Verdict> polls;
    feeder.Command(kRoot, 0x10, kNote);

    for (int poll = 0; poll < 2'400; poll++)
    {
        polls.push_back(PollPass(feeder, kRoot));
    }

    const long long early_ms = FramesToMs(feeder.Frame());
    const bool held_early = finder.Holds(feeder.Frame());
    for (int poll = 0; poll < 600; poll++)
    {
        polls.push_back(PollPass(feeder, kRoot));
    }

    const long long late_ms = FramesToMs(feeder.Frame());
    const bool held = finder.Holds(feeder.Frame());

    GCSF_CHECK(early_ms < 10'000 && late_ms > 10'000);
    GCSF_CHECK(!held_early);
    GCSF_CHECK(held);
    GCSF_CHECK(polls == std::vector(3'000, Verdict::kNone));
}

void TestLaterTrack()
{
    // A later track loops while the root track plays notes, then while the root track polls. Its loop counts once the
    // root track has settled, from its first pass that starts after the root track's last move.
    LoopFinder finder(kRoot);
    Feeder feeder(finder);
    std::vector<Verdict> while_moving;
    std::vector<Verdict> polls;
    std::vector<Verdict> while_polling;

    for (int pass = 0; pass < 3; pass++)
    {
        feeder.Command(kRoot, 0x08 + 2 * static_cast<uint32_t>(pass), kNote);
        feeder.Command(kLater, 0x40, kNote);
        feeder.Command(kLater, 0x42, kWait);
        while_moving.push_back(feeder.Jump(kLater, 0x48, 0x40));
    }

    for (int pass = 0; pass < 2; pass++)
    {
        polls.push_back(PollPass(feeder, kRoot));
        feeder.Command(kLater, 0x40, kNote);
        feeder.Command(kLater, 0x42, kWait);
        while_polling.push_back(feeder.Jump(kLater, 0x48, 0x40));
    }

    GCSF_CHECK(while_moving == std::vector(3, Verdict::kNone));
    GCSF_CHECK(polls == std::vector(2, Verdict::kNone));
    GCSF_CHECK(while_polling == (std::vector{Verdict::kNone, Verdict::kLoops}));
}

} // namespace

int main()
{
    TestLoop();
    TestLoopAroundPoll();
    TestPollBetweenPasses();
    TestPollHolds();
    TestLaterTrack();

    if (failures)
    {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    std::printf("loop finder tests passed\n");

    return 0;
}
