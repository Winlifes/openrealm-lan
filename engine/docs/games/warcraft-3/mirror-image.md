# Mirror Image combat ratios

The supplied 3.0.1.24371 `Units/AbilityData.slk` defines AOmi with DataB=0.2
and DataC=2 at ranks 1–3. Its `Scripts/common.j` identifies OMI2 as damage
dealt percent and OMI3 as damage taken percent. These are ratios, not
additive status bonuses: a stock image deals 20% normal attack damage and
receives 200% damage. A custom zero outgoing ratio remains zero.

The no-target spell pipeline creates the copy, publishes summon events and
retains its creating alias in the existing `summon_ability` field. The image
copies the learned rank at creation; later learning on the real Hero does
not change existing images. Normal attacks apply the outgoing ratio after
their attack bonuses. The common damage receiver applies the incoming
ratio before Mana Shield. Units without this exact Mirror Image identity
keep their existing combat policy.

No new wire or edict fields are introduced. Native/browser save envelopes
159/20 reject older worlds containing images without this creating identity;
JASS snapshot 13 is unchanged. ROC/TFT regressions exercise the actual
spell order, attack-hit and damage paths, then repeat after whole-world
WriteGame/ReadGame. They cover stock data, an alias with different ratios,
zero outgoing damage, independent Hero ranks and unaffected real Heroes.

This closes the damage-ratio gap. It does not establish exhaustive retail
parity for every image cast animation, item copy or ability restriction.
