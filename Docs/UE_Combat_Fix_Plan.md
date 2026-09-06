# UE Combat Fix Plan

**Project:** `aether_test`
**Engine:** Unreal Engine 5.4.4
**Scope:** combat feel, Dive multi-hit, E knockback, four-hit stamina budget, poison hit validation, dodge cancel/i-frames, HUD and camera feedback
**Deadline strategy:** ship the smallest change that improves the playtest without replacing the proven Blueprint combo graph.

## 0. Current baseline

The current moveset is already working through `BP_ThirdPersonCharacter`:

```text
Ground A1 → A2 → A3 → A4
                         ├─ Launcher L2 → Skill S4 → S5 → S6 → S7
                         └─ Jump → Air/Dive D3
```

The shared native pieces are:

- `ACombatCharacterBase`: ASC, Health/Stamina, damage, launch reaction, combo count, HUD and auto-facing.
- `UANS_MeleeHitbox`: socket sweep, per-notify dedupe, damage, poison and vertical launcher.
- `ANS_ComboWindow`: existing Blueprint asset that opens/closes the combo input window.
- `BP_ThirdPersonCharacter`: existing input buffer and lane switching (`GroundCombo`, `SkillCombo`, `AirCombo`, `BufferedInput`, `ComboIndex`).

## 1. Decisions and non-goals

### Keep for this submission

- Existing Blueprint combo state machine and montage arrays.
- Montage-driven timing; hit logic stays on animation notify strips.
- GAS for attributes/effects, but not a new ability framework for every move.
- One cleanup path for every interruption.

### Defer

- A complete `CombatComponent` rewrite.
- One `UCombatMoveData` asset per attack.
- Multiplayer prediction/replication.
- Cinematic camera choreography per move.
- Large VFX or animation import work.

The existing `UANS_MeleeHitbox` properties are already a useful editor-facing data layer. A second data-asset system before the deadline would duplicate data and create two sources of truth.

## 2. Target flow

```text
Input
 ├─ Attack / Skill / Launcher
 │   └─ Existing BP buffer + combo window
 │       └─ Montage
 │           ├─ ANS_ComboWindow
 │           └─ ANS_MeleeHitbox
 │               └─ Damage → reaction → displacement → feedback
 └─ Dodge (highest priority)
     └─ ResetCombo → stop montage → dodge motion
         └─ ANS_InvulnerabilityWindow → State.Invulnerable
```

## 3. Phase 1 — reusable hit/displacement primitive

**Files:**

- `Source/aether_test/ANS_MeleeHitbox.h/.cpp`
- `Source/aether_test/CombatCharacterBase.h/.cpp`

### Changes

Add to `UANS_MeleeHitbox`:

```text
HorizontalKnockback: float, default 0
KnockbackLiftZ: float, default 0
KnockbackDirection: AwayFromAttacker | AttackerForward
bPlayHitReaction: bool, default true
```

Add to `ACombatCharacterBase`:

```text
ApplyCombatKnockback(Direction, Strength, LiftZ)
```

`ApplyDamageToTarget()` remains the single damage gate. The notify only applies poison, launch or knockback after it returns `true`; the helper itself owns the optional hit-reaction callback.

### Compatibility rules

- `HorizontalKnockback == 0` means old behavior.
- `LaunchZ > 0` continues to mean vertical launcher.
- `bOnlyHitPlayers` keeps the existing enemy attack cooldown behavior.
- Existing montage notify strips require no property edits for this phase.
- `ExtraEffectOnHit` is evaluated only after the target's HP is confirmed to decrease; overlap/contact alone cannot apply a hit effect.

### Deferred feedback/performance pass

Camera shake is handled by the native Phase 7 feedback path; this phase only adds the reusable displacement primitive and the per-window reaction toggle.

The native world-sweep optimization is intentionally deferred until the behavior is visually verified. The current test wave is small, so changing trace plumbing before tuning hit timing would increase risk without a measurable win.

Do not optimize target search or introduce a registry yet: auto-facing only scans when its current target becomes invalid, and the test wave is small.

### Acceptance

- Build game target succeeds.
- A1–A4 damage and combo count are unchanged.
- L2 still launches vertically and stops its launch montage on landing.
- Poison still applies only after confirmed damage.
- Horizontal displacement works on an enemy without affecting player movement.

## 4. Phase 2 — Dive D3 multi-hit

**Asset:** `Content/Game/Combat/Montages/AM_Dive_D3.uasset`

Use four short, non-overlapping `ANS_MeleeHitbox` strips. The current notify already dedupes targets per activation, so four strips provide four deterministic hit opportunities without adding a new multi-hit framework.

Use a stable center socket (`pelvis` if available) and align each strip to a visible contact during the aerial spin.

### Starting values

| Tick | Damage | Radius | Horizontal KB | Lift Z | Reaction |
|---|---:|---:|---:|---:|---|
| D3-1 | 3 | 85 | 140 | 20 | off |
| D3-2 | 3 | 90 | 160 | 25 | off |
| D3-3 | 3 | 95 | 180 | 35 | off |
| D3-impact | 6 | 130 | 500 | 100 | on |

All four use `AttackerForward`. The first three small impulses carry enemies along the Dive path; the final impact creates the large game-feel displacement.

### Acceptance

- One full-health enemy receives four confirmed hits.
- The enemy travels with the Dive instead of being left behind.
- The last hit travels farther than the first three.
- Multiple enemies can be hit by the same strip.
- A dead enemy receives no later hit, poison or reaction.

## 5. Phase 3 — E3/E4 knockback only

Mapping:

```text
E1 = AM_Skill_S4
E2 = AM_Skill_S5
E3 = AM_Skill_S6
E4 = AM_Skill_S7
```

Set notify properties:

| Montage | Horizontal KB | Lift Z | Direction |
|---|---:|---:|---|
| S4 / E1 | 0 | 0 | — |
| S5 / E2 | 0 | 0 | — |
| S6 / E3 | 350 | 60 | AttackerForward |
| S7 / E4 | 600 | 120 | AttackerForward |

Keep existing damage values unless visual testing shows a balance problem. S6 should create space but leave S7 able to connect; S7 is the final displacement.

## 6. Phase 4 — E requires one full combo budget

Use `GetStamina()`, `GetMaxStamina()` and the existing `TryPayFullStamina()` entry gate; do not add another resource system. The helper name is kept for Blueprint compatibility, but it now checks the four-hit budget without spending it up front.

### Rule

- Ground attacks and launcher keep their current stamina cost.
- `SkillComboTotalCost = 50`, split across four E hit windows (`12.5` each).
- The first E input requires at least `50` stamina; it does not spend stamina at the entry gate.
- E1–E4 each pay `12.5` when their authored hitbox window begins. E1 additionally requires the `50`-stamina gate.
- E hitboxes do not grant stamina. Therefore a full bar supports two complete E chains.
- Normal confirmed hits restore `+8` stamina, clamped to the max value. The player starts at `50%` stamina.
- A buffered E checks the gate when it is consumed at the combo window, not when it was first pressed.

### Blueprint change

The current character Blueprint has two first-entry paths, so gate both:

1. the idle `IA_Skill` path (`bIsAttacking == false`) before it sets `CurrentCombo = SkillCombo`;
2. the buffered `AdvanceSkill` path (`bSkillCombo == false`) before it sets `CurrentCombo = SkillCombo`.

At each branch that first enters `SkillCombo`:

```text
TryPayFullStamina()
    True  → bSkillCombo = true → ComboIndex = -1 → AdvanceSkill
    False → clear BufferedInput → keep current chain
```

When `bSkillCombo` is already true, `AdvanceSkill` continues without another full-bar check; the four notify windows own the gradual charge.

HUD should display `E READY` while stamina is at least `50` (and `E x2` when the bar is full, if there is room).

### Poison regression fix

- The enemy `ReceiveActorBeginOverlap` path is disabled and has no poison effect reference.
- The old `IA_Posion` mapping and its Enhanced Input event that directly called `ApplyEffectToSelf(GE_PoisonDoT)` were removed; contact with an enemy therefore cannot poison the player.
- Enemy attacks may still assign `GE_PoisonDoT` through `ExtraEffectOnHit`, but it is now reached only after `ApplyDamageToTarget()` confirms real HP loss.

## 7. Phase 5 — dodge cancel and i-frames

### Input and motion

Add `IA_Dodge` to `IMC_Default`, preferably on Left Shift. Space remains Jump because Jump is a valid launcher-to-air-chain input.

`IA_Dodge` must not be blocked by `bIsAttacking` or `bIsComboWindowOpen`. It only fails when dead, already dodging or on cooldown.

```text
IA_Dodge
 → ResetCombo
 → Stop current montage (0.03–0.05s blend)
 → choose movement direction
 → play AM_Dodge
 → apply short directional motion
 → finish/cleanup
```

Direction priority:

1. Current movement input in camera space.
2. Actor forward when there is no movement input.
3. Backward direction when the player holds backward while soft-locked to an enemy.

The content folder currently has no dedicated dodge/roll animation. Use a temporary short montage only if an existing movement clip reads clearly; do not spend the submission window importing a new animation.

Starting values:

```text
Dodge duration: 0.24s
I-frame window: 0.03s → 0.19s
Cooldown:       0.45s
Distance:       280–350uu
```

### I-frame implementation

Create `ANS_InvulnerabilityWindow`:

```text
NotifyBegin → add State.Invulnerable
NotifyEnd   → remove State.Invulnerable
```

Add `State.Invulnerable` to `DefaultGameplayTags.ini`. `ApplyDamageToTarget()` rejects damage when the target ASC owns this tag, so the same guard blocks damage, poison, launch and hit reaction.

Failsafes:

- Death removes the tag.
- Dodge cleanup removes the tag.
- Interrupted dodge cannot leave movement locked.
- `ResetCombo()` restores movement/root-motion state exactly once.

### Acceptance

Test dodge during A1–A4, S4–S7, L2 and D3. It must cancel immediately, avoid enemy damage during the window and allow attack/movement again after the montage.

### Perfect dodge analysis (deferred; no implementation in this pass)

Normal dodge i-frames and perfect dodge are different results. The normal window prevents damage; the perfect sub-window is a narrow portion near the start of that window and is awarded only when an enemy `ANS_MeleeHitbox` actually reaches the dodging player. Pressing dodge near an enemy without contact must not count.

Keep the decision in the incoming-hit resolver, not in the input event:

```text
outside i-frames → normal damage/reaction
normal i-frames  → reject damage
perfect window   → reject damage + emit one PerfectDodge event
```

The existing `UANS_MeleeHitbox` socket sweep is the right contact source. A future native resolver should return a small result enum (damaged / invulnerable / perfect dodge) before poison, launch, hit reaction, stamina refund, camera cue, or VFX are dispatched. Consume the perfect response once per dodge activation, but keep the rest of the i-frame window valid against additional hitboxes. Cleanup must cover montage interruption, death, landing, and destruction.

## 8. Phase 6 — HUD

Current native HUD is anchored bottom-left. Move it to top-left so it is visible in the expected review position:

```text
Anchor:    top-left
Alignment: 0, 0
Position:  24, 24
Z-order:   10
```

Keep event-driven updates. Do not add a Tick binding. Add `E READY` to the native layout and update it from the stamina delegate.

## 9. Phase 7 — camera feedback

### Required

Trigger camera shake only on confirmed hit:

```text
A1–A3 / Dive tick 1–3: light
A4 / E3:               medium
E4 / Dive impact:      heavy
```

Use the notify's `CameraShakeScale`, so each hit is tuned on the animation timeline. A whiff must not shake the camera.

### Native implementation

- `UCombatHitCameraShake` is a self-contained native shake pattern with a short `0.13s` pulse and no asset dependency.
- `UANS_MeleeHitbox::CameraShakeScale` defaults to `0.35`; `ACombatCharacterBase::PlayCombatCameraShake()` ignores non-local characters and is called only after confirmed damage.
- While a live combat target exists, `ACombatCharacterBase` interpolates the existing SpringArm to `520uu` and the follow camera to `86` FOV. Losing the target restores the captured defaults; SpringArm collision testing remains enabled.
- The combat follow is stabilized on the existing SpringArm: the native path raises `TargetOffset.Z` by `40uu`, enables position/rotation lag (`8`/`10`), caps lag distance at `90uu`, enables lag substepping, and disables pawn roll inheritance so jump/root-motion/airborne turns do not copy 1:1 into the view. World collision testing remains enabled.
- Build and runtime shake-start smoke test are green. Standard PIE single-block A/B test confirms an enemy no longer collapses the SpringArm: the same blocker keeps the camera at about `400uu` instead of `154uu`; combat target framing reaches about `520uu` with FOV `86`. Follow stabilization also passes the PIE smoke check, but chaotic continuous combos can still zoom into an enemy, so Phase 7 remains open for target-switching/occlusion diagnosis.

### Optional if time remains

Use a small spring-arm interpolation while a combat target exists:

```text
Normal arm length: 400–450uu
Combat arm length: 500–540uu
Normal FOV:        90
Combat FOV:        84–87
```

Use FOV punch and shake for E4/Dive impact. Avoid per-move cinematic camera angles until the core combat is stable.

### VFX integration (first pass implemented)

The new content is mounted under `/Game/Vefects/Easy_Impact_Frames` and contains 200 `.uasset` files, including Niagara systems in `VFX/Frames/Particles` and their dependencies. The first pass wires `NS_Impact_Frame_01` as confirmed-hit VFX on 13 player hitbox windows across 10 montages, and uses `NS_Impact_Frame_01_Always` for the S7 action notify.

The S7 montage now has a one-shot `Play Niagara Effect` notify at the authored contact time. It is allowed to run on a whiff and owns the Niagara system's finite lifetime. Confirmed-hit VFX is separate: `UANS_MeleeHitbox::ConfirmedHitVFX` spawns only after `ApplyDamageToTarget()` returns `true`, at the `FHitResult` impact point/normal, once per victim per active notify window. A whiff or rejected hit does not spawn the confirmed effect.

### E4/S7 finisher cinematic (first pass implemented)

Start only on the first confirmed S7 hit. The native implementation snapshots living enemies within `900uu`, slows them to `0.18`, and tracks original dilation per actor. `BeginFinisherCinematic()` temporarily owns the existing camera control rotation and eases a `110°` orbit over `0.9s`, targeting `390uu` arm length and `76°` FOV. `UpdateCombatCamera()` yields while active. `EndFinisherCinematic()` restores dilation and camera state on normal completion, montage change, death, and `EndPlay`. PIE smoke verified active state, Niagara spawn, and dilation restoration; controller-driven hit feel still needs manual tuning.

## 10. Verification loop

After each phase:

1. Compile the game target while the editor is open.
2. Save dirty Blueprint/montage packages.
3. Run the smallest relevant PIE test.
4. Check Output Log for warnings/errors.
5. Record the result here before the next phase.

Final matrix:

| Test | Expected |
|---|---|
| 49 stamina + E | No skill, no cost |
| 50 stamina + E | E1 starts, then four hit windows spend 12.5 each |
| 100 stamina + E twice | Two complete E chains; the second ends at zero |
| E1/E2 | No horizontal displacement |
| E3/E4 | Forward knockback, E4 stronger |
| Dive D3 | Four hit events, final impact strongest |
| Dodge during attack | Immediate cancel, no idle wait |
| Enemy attack during i-frame | No HP loss |
| I-frame expiry | Damage works normally |
| Death during dodge | No stuck invulnerability/movement lock |
| Three-enemy wave | No visible trace/log/performance spike |

### Combo counter audit (2026-09-05)

The HUD label `COMBO xN` is backed by `ACombatCharacterBase::ComboCount`. The authoritative path is:

```text
ANS_MeleeHitbox::NotifyTick
  -> ApplyDamageToTarget(Victim)
  -> confirmed HP decrease
  -> RegisterConfirmedHit()
  -> OnComboChanged
  -> UCombatPlayerHUDWidget::HandleComboChanged
```

`RegisterConfirmedHit()` is called only for a player-controlled attacker and only after the target's HP is lower than before the GameplayEffect is applied. `UANS_MeleeHitbox` keeps a `HitActors` set per active notify, so one victim is deduped for that window; the set is reset at the next notify. A single window that hits three enemies therefore contributes three combo points. A multi-window Dive can count the same enemy once per successful window. `ComboResetDelay` is two seconds by default.

This means `COMBO x21` after a nine-step attack sequence against three enemies is consistent with 21 confirmed attacker-victim-window hits (for example, seven windows hitting all three). It is not a count of animation montages and is not incremented by capsule overlap, poison, knockback, or a failed damage effect. No code change was made for this audit; the detailed ownership and QA explanation is in `Docs/Combat_Architecture.md`.

## 11. Time budget and cut line

```text
Hitbox/knockback primitive: 60–90 min
Dive + E montage tuning:   60 min
Full-stamina gate + HUD:   30–45 min
Dodge + i-frames:          90–120 min
Camera shake/framing:      45–60 min
Regression/build/video:    60–90 min
```

If time drops below four hours, ship in this order: dodge cancel, Dive multi-hit, E3/E4 knockback, full-stamina gate, HUD, camera shake. Defer advanced camera framing and all architectural refactors.

## Status

- [x] Plan written from current UE source and Blueprint audit.
- [x] Phase 1: horizontal knockback + reaction toggle primitive (game/editor targets build green).
- [x] Phase 2: Dive D3 four hit windows (asset configured; PIE feel check pending).
- [x] Phase 3: E3/E4 knockback (S6/S7 notify data configured; PIE feel check pending).
- [x] Phase 4: four-hit stamina budget (legacy `AdvanceSkill` cost removed; notify data configured; S4 50-stamina lock path runtime-checked).
- [x] Poison/contact regression guard (debug input removed; enemy hit delayed to authored contact; extra effect requires confirmed damage).
- [ ] Phase 5: dodge cancel + i-frames.
- [ ] Phase 6: HUD placement/readiness indicator (native layout + E READY text ready; PIE visual check pending).
- [ ] Phase 7: native camera shake + combat framing implementation (single-block A/B green; chaotic continuous-combo stress case still reproduces enemy zoom; finisher orbit/VFX first pass added, visual polish pending).
- [ ] Phase 8: final regression and delivery documentation.
