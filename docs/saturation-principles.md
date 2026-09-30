# Saturation: six principles, not one curve

> This is the detail page for the saturation engine. The
> [README](../README.md#saturation-six-principles-not-one-curve) carries the summary
> and links here.

A tape machine is **one** of the ways analogue electronics bend a signal, and for a
long time this plugin was built around that one curve. The shaper is now a blend of
the six mechanisms a real chain uses, and they are genuinely different shapes:

| Principle | Mechanism | Character |
| --- | --- | --- |
| **TAPE** | Magnetic hysteresis, with a **memory** term - the medium's state depends on where it has been | Gentle at low level; the asymmetry is what makes the even harmonics |
| **VALVE** | Thermionic: a soft, strongly asymmetric knee with a wide transition | Even-dominant, and it compresses rather than clips - it thickens before it distorts |
| **CASSETTE** | Narrow gauge, low bias: a **hard, early knee** with very limited headroom and a low-frequency bump | "Everything is louder and smaller" |
| **AMP** | A guitar amplifier's input stage: a high-gain, nearly symmetric **cascade** | Clips hard, strong odd harmonics - the one that bites |
| **TRANSFORMER** | An iron core's flux lagging whatever drives it, with saturation on the peaks | A gentle, level-dependent compression with a soft top-end loss - the "iron" in a signal path |
| **DIGITAL** | A converter's quantisation: a held code between sample instants and a hard level ceiling | The one solid-state mechanism: bit depth (16 / 12 / 8-bit), bit crush or sample-and-hold |

Blending them is not a gimmick: a real chain **is** this. A guitar goes into an amp,
the amp into a desk and a tape machine, a valve preamp sits somewhere in the path, a
transformer couples the output, and the whole thing may end up on a cassette - or be
the last thing a converter sees before it is digitised.

**BLEND** sweeps the weighting across the six in a fixed order (tape → valve →
cassette → amp → transformer → digital) - the order is the signal path rather than a
ranking, five machines you overload by pushing level into them and then the converter
that replaces all of them - so the control has one direction the ear can learn.
**SHAPE** decides how concentrated it is: low picks one principle at a time, high
spreads the weighting so all six contribute and the result reads as one compound
machine.

Every curve is normalised to **unity slope at the origin** - see
[Unity-slope normalisation](../README.md#unity-slope-normalisation) for what that
guarantees - so the blend cannot change the level, only the shape.

Default BLEND is 0 % - pure tape, which is exactly what every earlier build did.

## Per-principle model lists

Each principle also has its own **model list** (VALVE TYPE, AMP TYPE, TRANSFORMER
TYPE, DIGITAL TYPE, VINYL TYPE), and each list has an **OFF** entry that removes that
principle outright. When one is off its share of the blend is redistributed across the
principles that remain, so switching a mechanism off never leaves the blend short of
gain - the same unity-slope property that keeps DRIVE honest keeps the blend whole.
