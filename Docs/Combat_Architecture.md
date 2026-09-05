# Combat Architecture and Combo Accounting

**Project:** `aether_test`
**Engine:** Unreal Engine 5.4.4
**Scope:** runtime combat flow, hit confirmation, combo HUD semantics, stamina/E chain, enemy damage effects, and QA guidance
**Status:** architecture audit of the current implementation; no combat behavior change in this document pass

## 1. Purpose

This document describes the combat slice as it exists in the checkout. It is intended to answer two practical questions:

1. What is the authoritative path from an animation notify to damage, effects, stamina, and HUD?
2. What exactly does the number shown as `COMBO xN` count?

The most important conclusion is that the current HUD number is a **confirmed-hit streak**, not a count of authored attack animations. It is incremented once for each attacker-to-victim hit that causes a real HP decrease. Multi-target hitboxes and multi-window attacks therefore produce more than one combo point from a single named attack.

## 2. Runtime ownership

The implementation deliberately keeps ownership small and explicit:

| Responsibility | Owner | Source / asset |
|---|---|---|
| Health, stamina, ASC, damage gate, combo streak, HUD creation | `ACombatCharacterBase` | `Source/aether_test/CombatCharacterBase.h/.cpp` |
| Animation-timed traces and per-window hit dedupe | `UANS_MeleeHitbox` | `Source/aether_test/ANS_MeleeHitbox.h/.cpp` |
| Combo input buffering and montage selection | Existing player Blueprint | `Content/ThirdPerson/Blueprints/BP_ThirdPersonCharacter.uasset` |
| Enemy chase/attack branch and reaction priority | Enemy Blueprint | `Content/Game/Combat/GAS/BP_Enemy.uasset` |
| Player HUD layout and event binding | `UCombatPlayerHUDWidget` | `Source/aether_test/CombatPlayerHUDWidget.h/.cpp` |
| Attribute/effect math | Gameplay Ability System | `Content/Game/Combat/GAS/*`, C++ ASC/AttributeSet |

There is no second combo counter in the hitbox notify and no widget-side polling counter. The player character is the single owner of the streak value.

## 3. Authoritative hit flow

```text
Montage plays
    |
    +-- ANS_MeleeHitbox::NotifyBegin
    |       - validates attacker/cooldown
    |       - validates player stamina gate/cost for this window
    |       - creates state and clears HitActors
    |
    +-- ANS_MeleeHitbox::NotifyTick (each frame in the authored window)
            - sweeps the hitbox socket from previous to current position
            - filters self, invalid actors, and target-type restrictions
            - skips an actor already hit by this notify activation
            - calls Attacker->ApplyDamageToTarget(Victim, Damage, ...)
                    |
                    +-- applies the attacker's Damage GameplayEffect
                    +-- compares target HP before/after
                    +-- returns false when HP did not actually decrease
                    +-- returns true only for a confirmed damage event
                            |
                            +-- player attacker: RegisterConfirmedHit()
                            +-- optional reaction callback
                            +-- optional poison/extra effect
                            +-- optional stamina gain
                            +-- optional launch/knockback

NotifyEnd
    - removes the per-mesh hitbox state
```

`ApplyDamageToTarget()` is the single confirmation gate. Anything that is supposed to represent a successful hit is downstream of this gate. This is why capsule contact, a failed GameplayEffect, a dead target, or a zero-damage configuration must not increment combo or apply poison.

## 4. Exact combo semantics

### 4.1 Increment rule

The increment path is:

```text
ApplyDamageToTarget()
    HP after < HP before
        -> IsPlayerControlled()
            -> RegisterConfirmedHit()
                -> ++ComboCount
                -> OnComboChanged.Broadcast(ComboCount)
```

Relevant implementation points:

- `CombatCharacterBase.cpp`: `ApplyDamageToTarget()` calls `RegisterConfirmedHit()` only after the HP-before/after check succeeds.
- `CombatCharacterBase.cpp`: `RegisterConfirmedHit()` increments the integer and broadcasts `OnComboChanged`.
- `CombatCharacterBase.h`: `ComboCount` and `OnComboChanged` belong to the character, not to an individual enemy.
- `CombatPlayerHUDWidget.cpp`: the widget binds to the observed player character's `OnComboChanged` delegate and renders `COMBO x{count}`.

Enemy attacks do not increase the player's combo because the registration call is guarded by `IsPlayerControlled()` on the attacker. A player hit on an enemy does increase the player's counter.

### 4.2 Unit of counting: victim per notify window

`UANS_MeleeHitbox` owns a `FHitboxState` for each active skeletal mesh. At `NotifyBegin`, `State.HitActors` is reset. During that one notify activation, each victim actor can be processed at most once. When the notify ends, the state is removed; a later notify window can hit the same victim again.

Therefore the practical unit is:

```text
one player attacker
× one living victim
× one authored ANS_MeleeHitbox activation
× real HP decrease
= one combo increment
```

This is intentionally different from “one animation montage equals one combo point.” A single notify can hit several enemies, and a single montage can contain several non-overlapping notify windows.

### 4.3 Why `COMBO x21` is consistent with nine named attacks

If three enemies are inside a hitbox and a window damages all three, that one window contributes three points. A total of 21 means that 21 attacker-to-victim confirmations occurred before the combo timeout; it does not mean that 21 animation montages played.

For example, seven successful windows hitting all three enemies produce:

```text
7 windows × 3 victims = 21 confirmed hits
```

Other distributions are also valid: some windows may hit one or two enemies, some may whiff, and Dive may contain multiple windows. If all nine authored attacks had exactly one hit window and all three enemies were hit every time, the number would be 27; seeing 21 simply indicates that the actual confirmed target/window distribution was lower than that maximum (or that the streak reset/started partway through the sequence).

The observed value is therefore expected under the current implementation. It is not evidence that the counter is accidentally incrementing on mere overlap.

### 4.4 Reset behavior

`RegisterConfirmedHit()` clears and restarts `ComboResetTimer` after every confirmed hit. `ComboResetDelay` is currently two seconds by default. When that timer expires, `ResetCombo()` sets the value to zero and broadcasts the update so the HUD collapses the combo text.

The counter is a continuous hit streak: spacing attacks less than the reset delay keeps the streak alive; waiting longer starts the next streak at one.

## 5. Multi-hit attacks and target movement

The current design supports the desired game-feel without a second combat framework:

- **Ground attacks:** normally one hitbox activation per authored contact window.
- **Dive D3:** four short activations; the same enemy may contribute up to four confirmed hits if it remains in the sweep path and survives.
- **E chain S4–S7:** each montage owns its own window and stamina cost. S6/S7 add horizontal displacement after confirmation; the displacement does not create an extra combo point by itself.
- **Multiple enemies:** every distinct victim that loses HP in the same window receives its own confirmation and combo increment.
- **Dead/invalid victims:** rejected before the effect and cannot receive later poison, launch, or combo increments.

The `HitActors` set prevents repeated `NotifyTick` frames from dealing damage continuously to the same victim inside one activation. It does not prevent a later authored activation from hitting that victim again, which is the mechanism used for deliberate multi-hit attacks.

## 6. Damage, poison, and contact safety

The post-fix order is important:

```text
trace overlap
    -> ApplyDamageToTarget
        -> confirmed HP decrease?
            no  -> stop; no combo, poison, stamina gain, launch, or knockback
            yes -> allow authored secondary effects
```

Enemy `ExtraEffectOnHit` (the poison GameplayEffect) is applied only in the `true` branch after the damage gate. The enemy hitbox is restricted to player targets and its authored attack notify is delayed into the telegraph/contact portion of the attack. The old debug poison input and contact-triggered poison path are removed.

This keeps collision overlap, damage confirmation, and secondary effects separate:

- proximity alone is not an attack;
- an overlap without HP loss is not a combo point;
- poison is a property of a confirmed enemy hit, not of touching an enemy capsule.

## 7. Stamina and E-chain contract

The current E behavior is a four-window budget:

| Event | Stamina behavior |
|---|---|
| Initial E entry | requires the full 50-point budget; the legacy upfront cost in `AdvanceSkill` was removed |
| S4, S5, S6, S7 notify begin | spends 12.5 per authored window |
| Normal confirmed player hit | restores the configured normal-hit amount, clamped to max |
| E hit window | does not refund stamina |
| Starting state | player begins at 50% stamina |

The first E window also performs the minimum/full-budget check. A failed player stamina window exits without stopping the montage, allowing the normal completion/interruption cleanup path to clear attacking state. This is the guard against the animation-lock symptom seen earlier.

## 8. HUD contract

`UCombatPlayerHUDWidget` builds its UMG hierarchy natively and binds once to the observed player character:

```text
OnHealthChanged  -> HP bar/text
OnStaminaChanged -> stamina bar/text + E READY label
OnComboChanged   -> COMBO xN text
```

The widget does not calculate damage, inspect hitboxes, or poll every frame. It only renders the character-owned values. Consequently, the HUD's combo number has the same semantics as `ACombatCharacterBase::ComboCount`: confirmed target hits in the current streak.

## 9. QA interpretation guide

Use the following observations when validating a recording or PIE session:

| Observation | Interpretation |
|---|---|
| One hitbox window touches three living enemies and HP drops on all three | combo increases by three |
| The trace overlaps an actor but its HP does not change | no combo, no poison, no stamina refund |
| Same enemy is hit in a later Dive/E notify | a new combo point is valid |
| Same enemy is found by several trace frames in one notify | `HitActors` dedupe prevents duplicate points |
| Enemy attacks the player | does not add to the player's combo counter |
| No confirmed hit for two seconds | combo resets to zero |
| A hit kills the enemy | the killing confirmation counts; later windows reject the dead actor |

For a deterministic three-enemy check, record the combo value before the attack, then count confirmed HP transitions per enemy per notify window. The expected final value is:

```text
starting combo + number of successful (attacker, victim, notify-window) confirmations
```

Do not compare the HUD directly with the number of montage names in the combo list; those are different quantities.

## 10. Extension guidance (not part of this pass)

If a future design needs both notions, keep the current confirmed-hit streak and add a separate semantic value rather than changing its meaning:

- `ComboCount`: confirmed target-hit streak, current behavior.
- `AttackSequenceCount`: authored player attack steps accepted by the combo state machine.
- Optional per-window telemetry: attack/montage id, notify id, victim id, damage result.

That separation would allow a designer-facing “9-step chain” display while retaining the responsive multi-target hit streak used by the current HUD. It is intentionally deferred because changing the existing counter would alter working combat feel and is not required for the submission.

## 11. Verification record

The implementation pass preceding this audit was built for both game and editor targets on UE 5.4.4. PIE traces verified that enemy damage begins at the authored attack contact window rather than on capsule contact, and that the player E path reaches cleanup instead of remaining permanently attacking when a stamina window fails.

This document records the source-level combo audit and the expected interpretation of the observed `COMBO x21`. No combat code, Blueprint graph, montage timing, or HUD behavior was changed to produce this documentation.

## 12. Planned VFX trigger contract

The imported `Easy_Impact_Frames` pack is currently content only; no montage notify or gameplay wiring has been added yet. The pack is mounted under `/Game/Vefects/Easy_Impact_Frames` and contains 200 `.uasset` files, including Niagara systems in `VFX/Frames/Particles`, their materials/textures, demo maps, a demo character, and helper Blueprints. The first candidate to preview is `NS_Impact_Frame_01`; the `Always`, `Static`, `Distortion`, and `Advanced` variants should remain explicit designer choices rather than being auto-selected by combat code.

Use two separate notify lanes:

```text
Montage notify track
├─ Timed Niagara notify/state
│  └─ slash, arc, trail, dust: spawn at an authored socket and play on every attack
└─ ANS_MeleeHitbox with optional ImpactVFX
   └─ trace → ApplyDamageToTarget == true → spawn impact at Hit.ImpactPoint
```

### Author-time effects

Slash streaks, weapon trails, anticipation flashes, and ground dust that describe the action belong directly on the montage timeline. A `UAnimNotifyState_TimedNiagaraEffect`-style notify is appropriate for an effect that follows a socket for the notify duration; a one-shot `UAnimNotify_PlayNiagaraEffect`-style notify is appropriate for a burst that owns its own finite Niagara lifetime. These effects must not be made dependent on whether the hitbox finds a target.

### Confirmed-hit effects

An impact frame is not an unconditional one-shot notify, because it would play on a whiff. The planned integration is an optional `UNiagaraSystem* ImpactVFX` on `UANS_MeleeHitbox`. After the existing `ApplyDamageToTarget()` gate returns `true`, the notify spawns the system at the confirmed `FHitResult` impact point, oriented from the hit normal; if the trace does not provide a useful point, fall back to the victim's capsule/mesh location. The spawn occurs once per victim per active hitbox window because `HitActors` already owns that dedupe. A miss, dead target, rejected damage, or invulnerable target produces no impact frame, poison, launch, or hit-confirm VFX.

This keeps visual timing on the animation while keeping truth in gameplay: the notify says *when an attack can connect*, and the hit resolver says *whether it actually connected*. Impact systems should be finite/auto-deactivating; infinite systems belong to a separate stateful effect with an explicit cleanup path.

## 13. E4/S7 finisher cinematic — analysis only

The final E attack is `S7` in the current `[S4, S5, S6, S7]` skill lane. The cinematic should begin only after the S7 hit window confirms at least one real victim. A whiff must finish as an ordinary attack: no time dilation, orbit, impact frame, or finisher camera shake.

Proposed sequence:

1. Keep the S7 `ANS_MeleeHitbox` as the sole damage authority and guard the cinematic with one `bFinisherCinematicStarted` flag per montage activation.
2. On the first confirmed hit, snapshot nearby living enemies inside a bounded radius. Temporarily slow only those enemies through a tracked per-actor time-dilation/paused-combat state; avoid global time dilation in the first pass so input, HUD, and the player camera remain responsive.
3. Enter a dedicated finisher camera mode. Save the normal SpringArm/control-rotation state, drive a short orbit around the player with eased yaw and fixed torso-height framing, then blend back before restoring normal combat camera ownership. The regular `UpdateCombatCamera()` must not fight the scripted orbit.
4. Layer the effects by truth: authored slash/charge effects on the timeline, confirmed impact frame at each valid victim, and a separate floor-traced stomp ring at the landing point. Apply the largest shake/FOV punch only after confirmation and keep it short so it does not compete with the orbit.
5. On montage completion, interruption, death, or actor destruction, restore enemy time dilation, camera ownership, control rotation, and any finisher flag exactly once.

The first QA pass should use one enemy and a debug orbit before adding multi-enemy slow motion. Acceptance is: whiff has no cinematic, one confirmed hit starts one orbit, multiple victims do not restart it, enemies outside the radius remain normal, and every interruption leaves no slow-motion or camera state behind.

## 14. Perfect dodge — analysis only

The existing Phase 5 plan describes a normal dodge with movement plus a broad invulnerability window. Perfect dodge is a narrower result inside that dodge, not a second input: an enemy hitbox must actually reach the player during the perfect sub-window. Merely pressing dodge near an enemy is not enough.

Use three conceptual results in the incoming-hit resolver:

```text
Outside dodge i-frames  → apply damage/reaction normally
Inside normal i-frames  → reject damage silently
Inside perfect window   → reject damage + emit one PerfectDodge event
```

`UANS_MeleeHitbox` is the correct source of contact because it already performs the authored socket sweep and owns per-window dedupe. It should ask the victim's native dodge state to resolve the hit before applying damage; the perfect-dodge response can then trigger a short counter opportunity, small camera cue, and optional time pulse without allowing damage, poison, launch, or hit reaction through. Consume the perfect response once per dodge activation while continuing to block every valid hit during the remaining i-frames.

The perfect sub-window should initially sit near the front of the existing `0.03s → 0.19s` i-frame range, then be tuned from recorded enemy contact times. Required cleanup cases are the same as normal dodge: montage interruption, death, landing, and actor destruction must remove the state and restore movement exactly once. This feature remains design/implementation work for a later pass.
