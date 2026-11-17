# Saturation: nine principles, not one curve

> This is the detail page for the saturation engine. The
> [README](../README.md#saturation-nine-principles-not-one-curve) carries the summary
> and links here.

A tape machine is **one** of the ways analogue electronics bend a signal, and for a
long time this plugin was built around that one curve. The shaper is now a blend of
the nine mechanisms a real chain uses, and they are genuinely different shapes:

| Principle | Mechanism | Character |
| --- | --- | --- |
| **TAPE** | Magnetic hysteresis, with a **memory** term - the medium's state depends on where it has been | Gentle at low level; the asymmetry is what makes the even harmonics |
| **VALVE** | Thermionic: a soft, strongly asymmetric knee with a wide transition | Even-dominant, and it compresses rather than clips - it thickens before it distorts |
| **PENTODE** | A sharp-cutoff pentode: an early knee and a steep, odd-dominant edge | The tight end of a valve amp - it bites without the fizz |
| **TRIODE** | A low-mu triode: the softest knee in the set | Even-harmonic warmth at any level - the same valve, asked politely |
| **TRANSISTOR** | Germanium conduction: asymmetric, with a crossover notch around zero | Even harmonics and a grain on the low orders - the solid-state edge |
| **FOLD** | A wavefolder: the curve turns back instead of flattening | Folds the signal rather than clipping it; odd-dominant and level-dependent |
| **GLASS** | The pure second-order term, `x + a*x^2` - and nothing else | Adds the **second harmonic alone**: it thickens the sound without adding edge |
| **TRANSFORMER** | An iron core's flux lagging whatever drives it, with saturation on the peaks | A gentle, level-dependent compression with a soft top-end loss - the "iron" in a signal path |
| **DIGITAL** | A converter's quantisation: a held code between sample instants and a hard level ceiling | The one solid-state mechanism: bit depth (16 / 12 / 8-bit), bit crush or sample-and-hold |

Blending them is not a gimmick: a real chain **is** this. A guitar goes into a valve
amp - a pentode input stage into a triode output - past a germanium stage, into a desk
and a tape machine; a transformer couples the output, a wavefolder or a glassy
second-order stage colours it somewhere in between, and the whole thing may be the last
thing a converter sees before it is digitised.

**The nine SOURCE knobs** are the weights, one per principle, and they are shares: TAPE
at 60 with VALVE at 40 is a 60/40 blend. Shares are normalised, so the order they are
listed in is the order the signal would meet them rather than a ranking - five machines
you overload by pushing level into them and then the converter that replaces all of
them. Turning all nine to zero is the dry signal, sample for sample: the amount fades
the blend against the input rather than scaling the drive, because every curve here
saturates at any drive at all.

Every curve is normalised to **unity slope at the origin** - see
[Unity-slope normalisation](../README.md#unity-slope-normalisation) for what that
guarantees - so the blend cannot change the level, only the shape.

Default is TAPE at 100 % and everything else at 0 % - pure tape, which is exactly
what every earlier build did.

## Per-principle model lists

Each principle also has its own **model list** (TAPE TYPE, VALVE TYPE, TRANSFORMER
TYPE, DIGITAL TYPE, VINYL TYPE), and each list has an **OFF** entry that removes that
principle outright. When one is off its share of the blend is redistributed across the
principles that remain, so switching a mechanism off never leaves the blend short of
gain - the same unity-slope property that keeps DRIVE honest keeps the blend whole.
