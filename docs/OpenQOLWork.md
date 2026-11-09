# Open quality-of-life work

All five currently open QoL proposals are listed here. None is implemented merely
by appearing in this document. They remain separate from the
[non-QoL development gaps](../docs/OpenDevWork.md).

The current category uses one launch flag, **--qol**. Preserve standard behaviour
when it is absent. Camp Fix, two-hour wilderness steps, the exploration training
indicator and the scanned in-game Journal are already implemented; they are not
open items below.

## QOL-01 - Keep training eligibility colours in Train

**Request:** retain the existing level-up eligibility name/label colour while
inside the Train menu when QoL is enabled.

- Use EGA light blue **#5555FF**, matching the exploration training indicator.
- Base the hint on enough XP for an uncapped available class, regardless of gold.
  Actual training still checks school, eligibility, fee and its native rules.
- Reuse the existing read-only eligibility query. Rendering must not clip XP,
  prepare training, spend money, consume RNG or introduce a saved flag.
- Preserve selected-row visibility and existing status-colour precedence.
- Check entry to Train, character changes, successful training, cancellation,
  class/race caps, low gold and QoL-off presentation.

Start with [TrainingService](../src/party/training_service.cpp),
[exploration labels](../src/ui/exploration_screen.cpp) and the training/menu
consumers. This is an extension of the delivered exploration hint.

## QOL-02 - Distinguish level-drained characters by name colour

**Request:** characters who have lost levels should have a distinct name/label
colour with QoL enabled. **Orange was proposed**, not finalised.

- Determine the authoritative existing level-drain state; do not add another
  persistent flag that can become stale after Restoration or loading.
- Agree the exact shade and precedence against red/yellow status, white selection
  and blue training eligibility before implementation.
- Define which party/name displays should share the rule, and keep it consistent.
- Check repeated drains, restored levels, training, death/inactivity and save/load.

Open decision: exact colour and precedence. No new restoration or level rules are
implied by this presentation request.

## QOL-03 - Highlight modified character statistics

**Request:** highlight stat values currently modified by an active spell buff or
equipped-item effect. Restore normal styling when the modifier expires or the
item is removed.

- Separate the unmodified value from the effective displayed value through the
  existing equipment/effect calculations; do not guess by comparing unrelated
  cached fields.
- Identify the supported stat displays and shared rendering rule, including
  combat versus exploration where applicable.
- Agree highlight colour, status/selection precedence and whether reductions
  should use a different indication from increases.
- Check stacking, expiry, replacement, curse/removal restrictions, Restoration,
  save/load and QoL-off behaviour. Highlighting must not replay equipment effects
  or alter the underlying calculation.

Open decisions: colour policy, presentation scope and positive/negative modifiers.

## QOL-04 - Journal read/unread persistence and list colours

**Request:** assess feasibility of storing a separate read/unread state and
colour-coding the unlocked Journal list.

- Keep discovery/unlock history distinct from reading history. The current
  99-known-record bitset occupies 13 bytes; do not overwrite it to track reads.
- Identify safe storage and its interaction with the current schema6 format,
  source bank ownership, older-save defaults and disabled QoL.
- Define when a record becomes read: opening a page, actual presentation, or
  viewing the final page. Decide what missing/invalid image pages should do.
- Decide whether reopening a record can mark it unread, and agree list colours
  and selection precedence.
- Preserve old-generation isolation, F12/checkpoint rollback, discovery receipt
  ownership and source-authenticated unlock behaviour.

This remains a feasibility/design item. No save-format change, assumed free bytes
or read-completion rule has been authorised. The approved image-only Journal does
not already provide read/unread state.

## QOL-05 - Investigate the Slums clearance stall

**Request:** review the source design where ordinary wandering encounters,
including the Old Rope Guild, stop after 15 while clearance needs 25 qualifying
victories. A cautious player may exhaust wandering encounters without a practical
reason to risk resting in the Slums, even though Rest interruptions remain.

- Verify exactly which fixed, wandering and Rest encounters increment the
  qualifying victory count, and whether remaining fixed battles reliably bridge
  the gap on real campaign routes.
- Distinguish a port bug from recovered original behaviour before proposing a
  change. Record the relevant source predicates, counters and quest flags.
- If warranted, propose a clearly labelled optional QoL rule that allows
  continued clearance progress without deliberate unsafe resting.
- Agree the policy before changing chances, encounter limits, counters, victory
  qualification or clearance flags. Check both optional and original behaviour,
  repeat encounters and save/load once a policy is approved.

This is research, not approval to alter encounter rates or manufacture victories.

## Acceptance for future QoL work

Changes must retain the single --qol category switch unless a different interface
is explicitly requested. Presentation should stay read-only. Rule-changing options
must be identified as host upgrades, with their costs, time, effects, RNG and
persistence consequences tested. User decisions still open above should be parked
rather than silently chosen during unrelated gap fixes.
