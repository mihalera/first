// =============================================================================
//  The OSC ring's ball: the physics that both clocks step.
//
//  The ring used to move its ball by timing a hop from one landing spot to the
//  next. That read as a selector being advanced, not as a ball being thrown: the
//  motion was a different chord every time but the same duration, and the note was
//  chosen before the ball moved. The module is a bouncing ball, so the ball now
//  obeys the two things a ball in a room obeys - gravity and a wall - and the note
//  is chosen by WHERE the wall is hit rather than by a scheduler.
//
//  WHY THE KICK. A real ball dropped into a circle settles at the bottom, and
//  after a few seconds it is playing the same sector over and over. The ring is a
//  note generator, so the bounce that keeps it moving is part of the instrument:
//  every impact receives a tangential impulse, usually clockwise and occasionally
//  against the spin. Gravity provides the down, the wall reflects, and the spin
//  both keeps the ball off the floor and stops it settling into a periodic orbit
//  across the same two sectors. The impulse is drawn from a small generator kept
//  IN the state, so the editor and the engine make the same choices when the
//  clock changes hands.
//
//  WHY A FIXED STEP. The editor ticks at the panel's frame rate and the engine
//  ticks once per audio block; those are two different clocks with two different
//  deltas. Integrating with the delta that happened to arrive makes the same state
//  follow two different paths, and a fast ball can cross the wall between two
//  samples. So both callers hand in elapsed seconds and this advances in fixed
//  steps, carrying the remainder in the state: 240 steps a second, independent of
//  who is asking. The editor and the engine therefore agree about where the ball
//  is and which note the next impact plays, which is what makes handing the ball
//  over when the window closes invisible rather than a jump.
//
//  Coordinates are UNIT DISC coordinates: the centre is (0, 0), the wall is the
//  unit circle, and the ball's centre is kept inside it. The widget multiplies by
//  the radius it has and leaves room for the drawn ball. Y grows DOWN, like the
//  screen, so gravity is +y and the normal at the top is (0, -1).
//
//  No JUCE here on purpose: the processor's audio thread includes this, and a
//  harness can compile it without a toolkit.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace j37
{
namespace oscball
{

inline constexpr float gravity = 3.15f;            // disc radii per second squared
inline constexpr float restitution = 0.96f;
inline constexpr float tangentialKick = 0.60f;      // mostly clockwise, so the ball walks the ring
inline constexpr float tangentialDamping = 0.85f;   // and does not wind itself up forever
inline constexpr float maxTangentialSpeed = 1.80f;
inline constexpr float minInwardSpeed = 2.30f;      // leaves a grazing hit instead of rolling
inline constexpr float minBounceSpeed = 0.70f;      // a settled ball is not allowed
inline constexpr float maxSpeed = 3.80f;            // and neither is a runaway one
inline constexpr float maxAngularSpeed = 18.0f;     // radians per second
inline constexpr float angularDamping = 0.9995f;     // per fixed step, not per frame
inline constexpr float twoPi = 6.283185307179586f;
inline constexpr float fixedStep = 1.0f / 240.0f;
inline constexpr float maxFrame = 0.10f;            // a stalled frame does not teleport the ball
inline constexpr int maxSubsteps = 32;
inline constexpr int maxImpactsPerStep = 4;

struct State
{
    float x = 0.0f;
    float y = -0.62f;
    float vx = 0.85f;
    float vy = 0.0f;

    // How far the ball has turned, in radians, and how fast it is turning. The
    // spin is part of the state for the same reason the velocity is: the editor
    // draws the mark on the ball, the engine keeps the same rotation while no
    // editor exists, and the handover must not make the ball jump through a turn.
    float angle = 0.35f;
    float angularVelocity = 2.20f;

    // The part of a fixed step that has not been spent yet. It travels with the
    // state so the editor and the engine continue the same path when the clock
    // changes hands, rather than each rounding the same instant differently.
    float accumulator = 0.0f;

    // The spin's own generator. It is part of the state for the same reason the
    // accumulator is: a handover that reset it would change the next bounce.
    std::uint32_t randomState = 0x9e3779b9u;
};

struct Impact
{
    float normalX = 0.0f;
    float normalY = -1.0f;
    float speed = 0.0f;        // the INCOMING normal speed, before restitution
};

struct StepResult
{
    int impactCount = 0;
    Impact impacts[maxImpactsPerStep] {};
};

inline bool isFinite (float value) noexcept
{
    return std::isfinite (value);
}

inline bool isFinite (const State& state) noexcept
{
    return isFinite (state.x) && isFinite (state.y)
        && isFinite (state.vx) && isFinite (state.vy)
        && isFinite (state.angle) && isFinite (state.angularVelocity)
        && isFinite (state.accumulator);
}

inline float speedOf (const State& state) noexcept
{
    return std::sqrt (state.vx * state.vx + state.vy * state.vy);
}

/** A state no caller can make worse: a NaN or an escaped ball is put back on the
    launch position rather than being allowed to poison every later step. */
inline void sanitise (State& state) noexcept
{
    if (! isFinite (state))
    {
        state = State {};
        return;
    }

    const auto distance = std::sqrt (state.x * state.x + state.y * state.y);

    if (distance > 1.0f)
    {
        const auto scale = 1.0f / distance;
        state.x *= scale;
        state.y *= scale;
    }

    const auto speed = speedOf (state);

    if (speed > maxSpeed)
    {
        const auto scale = maxSpeed / speed;
        state.vx *= scale;
        state.vy *= scale;
    }

    state.accumulator = std::fmod (state.accumulator, fixedStep);

    state.angle = std::fmod (state.angle, twoPi);

    if (state.angle < 0.0f)
        state.angle += twoPi;

    state.angularVelocity = std::fmax (-maxAngularSpeed,
                                      std::fmin (maxAngularSpeed, state.angularVelocity));
}

/** The sector whose wedge the contact normal points into, 0 at the top and
    increasing clockwise - the same mapping the widget draws. */
inline int sectorForNormal (float normalX, float normalY, int sectorCount) noexcept
{
    if (sectorCount <= 0)
        return 0;

    constexpr auto halfPi = 1.5707963267948966f;

    const auto angle = std::atan2 (normalY, normalX) + halfPi;
    const auto step = twoPi / static_cast<float> (sectorCount);
    auto index = static_cast<int> (std::floor (angle / step + 0.5f));

    index %= sectorCount;

    if (index < 0)
        index += sectorCount;

    return index;
}

/** How hard the note under an impact is played. The input is the incoming normal
    speed in disc radii per second; the output multiplies the oscillator's level.
    The square root gives the quiet half of the range some loudness instead of
    leaving it all at the top. */
inline float gainForImpactSpeed (float speed) noexcept
{
    constexpr auto slowest = 0.25f;
    constexpr auto fastest = 3.00f;

    const auto t = std::fmin (1.0f, std::fmax (0.0f, (speed - slowest) / (fastest - slowest)));
    return 0.35f + 0.90f * std::sqrt (t);
}

namespace detail
{

inline float nextRandom (State& state) noexcept
{
    state.randomState = state.randomState * 1664525u + 1013904223u;
    return static_cast<float> ((state.randomState >> 8) & 0x00ffffffu) / 16777216.0f;
}

inline void stepFixed (State& state, StepResult& result) noexcept
{
    state.vy += gravity * fixedStep;
    state.x += state.vx * fixedStep;
    state.y += state.vy * fixedStep;

    state.angle += state.angularVelocity * fixedStep;
    state.angularVelocity *= angularDamping;

    if (state.angle >= twoPi || state.angle <= -twoPi)
        state.angle = std::fmod (state.angle, twoPi);

    const auto distanceSquared = state.x * state.x + state.y * state.y;

    if (distanceSquared < 1.0f)
        return;

    const auto distance = std::sqrt (distanceSquared);
    const auto normalX = distance > 1.0e-6f ? state.x / distance : 0.0f;
    const auto normalY = distance > 1.0e-6f ? state.y / distance : -1.0f;

    // The centre sits just inside the wall: the drawn ball's edge touches the
    // ring, and the next step starts from a real position rather than from a
    // coincidence of floating point.
    state.x = normalX * (1.0f - 1.0e-4f);
    state.y = normalY * (1.0f - 1.0e-4f);

    const auto outward = state.vx * normalX + state.vy * normalY;

    if (outward <= 0.0f)
        return;

    if (result.impactCount < maxImpactsPerStep)
    {
        auto& impact = result.impacts[result.impactCount++];
        impact.normalX = normalX;
        impact.normalY = normalY;
        impact.speed = outward;
    }

    // Reflect, then make the inward normal component at least minInwardSpeed:
    // a hit that only grazes the wall must leave it, or the ball would sit on the
    // rim and trigger a note on every step. The floor is also what keeps the ball
    // crossing the disc rather than settling into a small puddle at the bottom.
    const auto reflected = std::fmax (restitution * outward, minInwardSpeed);

    // Clockwise tangent: at the bottom it sends the ball left, at the left wall
    // up, at the top right. The ball therefore walks around the ring rather than
    // settling into the bottom of it. The damping matters as much as the kick:
    // adding a kick on every one of several hundred hits a minute is how a ball
    // winds itself up into a blur against the rim. An occasional counter-kick
    // breaks the two-sector orbit a purely clockwise spin settles into.
    const auto tangentX = -normalY;
    const auto tangentY = normalX;
    auto tangential = state.vx * tangentX + state.vy * tangentY;
    const auto direction = nextRandom (state) < 0.75f ? 1.0f : -1.0f;
    const auto kick = tangentialKick * (0.5f + nextRandom (state));
    tangential = tangential * tangentialDamping + direction * kick;
    tangential = std::fmax (-maxTangentialSpeed, std::fmin (maxTangentialSpeed, tangential));

    state.vx = normalX * -reflected + tangentX * tangential;
    state.vy = normalY * -reflected + tangentY * tangential;

    // Friction at the contact turns the ball. The tangent is the same one the
    // kick uses, so the drawn mark follows the direction the ball is actually
    // being sent around the ring rather than an unrelated animation.
    const auto spinTarget = -tangential * 0.85f;
    state.angularVelocity = std::fmax (-maxAngularSpeed,
                                       std::fmin (maxAngularSpeed,
                                                  0.55f * state.angularVelocity
                                                      + 0.45f * spinTarget));

    // A floor as well as a ceiling: the ring is an instrument, and a ball that
    // has lost its energy stops being one.
    auto speed = speedOf (state);

    if (speed < minBounceSpeed && speed > 1.0e-6f)
    {
        const auto scale = minBounceSpeed / speed;
        state.vx *= scale;
        state.vy *= scale;
    }
    else if (speed > maxSpeed)
    {
        const auto scale = maxSpeed / speed;
        state.vx *= scale;
        state.vy *= scale;
    }
}

} // namespace detail

/** Advances the ball by `seconds`, in fixed steps, and reports every wall hit it
    made on the way. Realtime-safe: no allocation, no lock, no JUCE. */
inline StepResult advance (State& state, float seconds) noexcept
{
    StepResult result;

    if (! isFinite (state))
        state = State {};

    if (! isFinite (seconds) || seconds <= 0.0f)
        return result;

    state.accumulator += std::fmin (seconds, maxFrame);

    int steps = 0;

    while (state.accumulator >= fixedStep && steps < maxSubsteps)
    {
        state.accumulator -= fixedStep;
        detail::stepFixed (state, result);
        ++steps;
    }

    if (steps == maxSubsteps)
        state.accumulator = 0.0f;

    return result;
}

} // namespace oscball
} // namespace j37
