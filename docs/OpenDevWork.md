# Open development work

This is the current non-QoL backlog for the exported 0.1 source tree. The accepted
human-led campaign playthrough is complete; that does not establish every branch,
combat combination or original-runtime comparison. The historical plan ended at
P4.10. No separate Phase 5 has been approved.

QoL proposals are kept in [OpenQOLWork](OpenQOLWork.md). This document records
work to investigate or validate, not permission to bypass existing guards.

## Parked combat gaps

The Cxx identifiers preserve the original combat-gap audit references. Six cases
remain parked after the shared combat implementation/hygiene passes.

| ID | Open problem | Required investigation and completion criteria |
| --- | --- | --- |
| **C07 - inherited scratch ownership** | Some native callbacks read pending spell/level/item/elemental scratch they do not initialise. Full producer and cross-action/load ownership is not established. Ordinary physical conditional-1C and quiescent 1C expiry retain a historical zero-pending projection; that is not a proven native reset. | Recover the actual producers, caller lifetimes and reset/load boundaries, or obtain an explicit compatibility policy. Carry authenticated context through the shared services. Keep genuinely context-dependent outcomes held; never substitute the current attacker, an arbitrary spell or guessed zero flags. |
| **C09 - blocked Slow Poison revival** | A native null-node retry reads physical far address 0000:0003 in the interrupt vector table. Its result is environment-dependent. Normal successful revival already works. | Capture the native environment value/behaviour or define a deliberate replacement rule. Validate blocked placement, HP/status/body ownership, spell costs, failure rollback and successful continuation. |
| **C10 - unsafe native list traversal** | Recursive active-node removal and a genuinely freed saved successor depend on native pointer/memory lifetime. | Recover a reproducible native case or agree a safe compatibility rule. Preserve callback-before-unlink ordering, duplicate identities and atomic failure. Safe captured-null tail appends during Dispel are already supported. |
| **C12 - effect-owned ammunition** | The no-node handler87 depletion case and handler89 Spiritual Hammer do not establish arbitrary item-owned effect cleanup. | Find a supplied/runtime witness, authenticate the item/effect link, and recover reversal/unlink order. Validate the last shot, equipment binding, retained effect ownership, ground items, revisions and rollback. |
| **C14 - weak allied sweep** | A selected weak ally can be absent from the opposing frozen sweep roster, leaving the native chosen index undefined. | Recover runtime evidence or choose an explicit compatibility rule before replacing the current atomic hold. Ordinary sweep, confirmed friendly attacks, movement bumps and proven NPC allegiance changes already work. |
| **C16 - wider special encounter mutation** | Destructive prepared selection, temporary-participant removal and broader special/duel contexts lack authenticated caller bindings and campaign witnesses. | Recover actor identity, prepared/live roster ownership, selection and suspended-caller lifetime. Test removal, empty/last-active cases, F12 rollback, ECL suffix continuation and save boundaries. High-bit CPIC wrapping and shared art-slot replacement are already implemented. |

Start in the current implementations rather than reproducing old encounter fixes:
[attack service](../src/combat/attack_service.cpp),
[magic service](../src/combat/magic_service.cpp),
[entry preparation](../src/combat/actor_preparation.cpp),
[shared health](../src/combat/health_service.cpp), and
[exploration orchestration](../src/world/exploration_session.cpp).

### Existing compatibility decisions to preserve

These are delivered policies, not additional open bugs under C07:

- Petrifying gaze and dragon/mummy entry fear use their normal non-elemental
  saving-throw categories without inherited fire/cold-resistance bonuses.
- The authenticated castle thorn helper uses ordinary poison saves. Other
  scripted-save contexts retain the shared flag-invariance proof and a real hold
  when unknown flags could change the result.
- Dragon breath uses its own targeting metadata for the resistance-level check,
  including reflection. Do not infer unknown resident scratch from a pending
  spell identifier or damage dice.
- Failed saves still apply their real native effects. Saves, damage, resources,
  RNG and retained suffixes are not skipped to manufacture success.

These choices are explicit compatibility policies; they are not proof that every
native scratch producer has been recovered.

## Other conditional gameplay and service coverage

- [ ] **Finish the research-only palette/presentation follow-up.** The second
  title splash has a native palette-register pulse, while the Graveyard has
  scripted flash-sky/redraw/normal-sky/redraw publications. Preserve these as
  ordered, actually presented states rather than allowing a pump to replace the
  intermediate frame before SDL sees it. Recover timing/colour interpretation
  where necessary; no new flash dwell has been chosen. Kuto's recovered helper
  is state-dependent background selection, not proof of a repeating oscillator.
  This was investigated, not implemented or approved as a generic weather loop.
- [ ] **Reconcile service/callback coverage against this source snapshot.**
  Assigned ECL dispatcher branches are not evidence that every service mode and
  participant context works. Audit typed requests and the matching rescue,
  equipment, hazard, expiry, cleanup, elemental damage and terminal callbacks.
  Classify input-validation errors separately from missing implementation.
  Use concrete supplied/captured witnesses before enabling an additional path.
- [ ] **Complete remaining character-library/file-management behaviour.**
  Review item-bearing campaign-character export/removal, NPC handling in the
  character-management UI and permanent character deletion. Keep ordinary
  roster Remove distinct from exporting campaign inventory or deleting files.
  Recovered persistent ECL party removal is already supported and must not be
  rescheduled as a missing feature.
- [ ] **Exercise additional campaign/native caller contexts.**
  Extend coverage of destructive selection, cross-map invocation ownership,
  nested services, special CALL/duel participants and terminal cleanup where a
  guard still names an unresolved context. Validate the actual source caller,
  not only a synthetic helper with invented parameters.

Some older maintenance notes listed combat-icon editing, picture preferences,
ordinary camp work, training, weapon resistance, sweep, mummy fear, dragon breath
and ending return as missing. Their recovered implementations are now present.
Do not reopen those whole features without a new failing case. For example,
[Encamp](../src/modes/encamp_mode.cpp) has icon and picture-preference consumers.

## Validation and release closeout

- [ ] **Broaden the route/combination matrix.** Cover newly created parties and
  original imports, all regional travel, conditional transports, fixed/random/
  Rest encounters, shops/shrines/NPCs, training, outdoor/interior combat,
  flight/defeat, equipment/effects, save/restart and the ending. Keep natural,
  assisted and direct-source-point tests separately labelled.
- [ ] **Compare representative behaviour with the original DOS runtime.**
  Prioritise ambiguous movement, encounter predicates, callbacks, dialogue
  pacing, animation/missile presentation and sound. A port screenshot or a
  decompiler listing alone is not an original-runtime observation.
- [ ] **Reconcile implementation and evidence inventories.** Retire stale
  milestone assertions and name each supported subset, remaining guard and
  owning component. Do not infer a completion percentage from opcode counts or
  assertion totals. Carry sufficient source identity/offsets with new evidence.
- [ ] **Publish a portable regression harness.** Historical workspace suites
  depend on private DOS fixtures, source snapshots, generated strings and
  investigation artifacts. They are not copied into this repository. Curate
  asset-independent tests and an explicit local-asset test interface before
  advertising a public full-suite command. The current clean-tree validation
  establishes compilation, metadata consistency and installed startup only.
- [ ] **Validate additional toolchains/platforms.** The release application is
  validated with Windows x64/MinGW. MSVC runtime units compile, but a fully linked
  application with a matching SDL SDK and its deployment should be exercised.
  Other hosts need setup/input/storage/audio validation and a deliberate Journal
  extraction approach; Windows WIC and GOG registry discovery are Windows-only.
- [ ] **Verify dependency/release combinations.** Establish supported SDL SDK
  versions and compiler/runtime combinations, including fresh-machine installs,
  paths with spaces/Unicode, runtime DLL redistribution and source-only rebuilds.
  The existing snapshot was tested with SDL headers/runtime 3.5.0; arbitrary
  SDL/compiler combinations are not certified by the CMake package lookup.
- [ ] **Choose the project-wide source license.** No project LICENSE has been
  selected for this export. Preserve the existing toml++ and SDL notices.

## Explicitly deferred parity work

These were outside Phase 4's required acceptance, rather than uncompleted
prerequisites for the accepted campaign run:

- [ ] Original DOS save export and original-runtime round-trip verification.
- [ ] Exact Borland RNG sequence reproduction.
- [ ] Cross-compiler RNG-state/save portability. Schema6 currently identifies
  and rejects a foreign standard-library MT19937 representation; it does not
  silently reinterpret GNU saves as MSVC saves or vice versa.
- [ ] Full original-hardware waveform, timing and pixel parity. Current host
  pacing/readability policies and SDL software checks do not establish that.

## Rules for addressing a gap

1. Reproduce the concrete failing state with immutable source assets and record
   the source profile, actual caller and relevant actor/item/effect identities.
2. Recover native behaviour or document an approved compatibility choice.
3. Implement a shared behavioural service where the source shares behaviour;
   avoid a map, monster name or encounter-specific bypass.
4. Stage mutations and preserve exact RNG/resource/counter ordering. Late
   failure must not partly publish party, effect, map or save state.
5. Add meaningful checks for the failing combination, rollback and continuation;
   use fresh-process save checks when persistence/ownership changes.

Private research and proprietary fixtures remain in the development workspace;
this source export does not publish them or relabel them as portable evidence.
