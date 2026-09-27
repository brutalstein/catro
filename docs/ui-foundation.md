# Native UI foundation

Catro's product shell is intentionally native and visually quiet. The design goal is not to render
the most effects; it is to keep UI cost predictable while leaving CPU/GPU budget to voice, capture,
encode, and the game.

## Layout contract

The Windows shell uses three stable regions:

1. **72 px global rail** — Home, Voice, Share, System, Settings.
2. **220–248 px context pane** — rooms, sources, or section-local navigation. It collapses below
   800 logical pixels and is also removed for the diagnostics workspace.
3. **Workspace** — the only large content subtree attached to the visual tree.

The title bar is 44 px. The launch size is 1280 × 820 logical pixels. No page is allowed to assume
that size; the workspace owns the remaining width.

The portable `apps/shell/ShellModel` is the stable navigation contract. Product backends should
bind to those sections instead of teaching the native shell about transport or media internals.

## Performance model

The shell follows these invariants:

- **No Mica, Acrylic, blur, backdrop, or decorative shadow passes.** Every major surface is a solid
  brush. This avoids a persistent composition/backdrop cost while a game is rendering.
- **No continuous UI animation.** Speaking/meter state may update later, but animation is never the
  source of truth.
- **Lazy workspace construction.** Home is built at launch. Voice, Share, System, and Settings are
  instantiated only on first visit. Only one page is attached to `ContentControl` at a time.
- **Constant-size global navigation.** Five destinations make selection/update cost O(1) with a
  fixed upper bound; there is no runtime navigation collection or reflection.
- **Resize policy is constant-time C++.** One width comparison selects 0/220/248 px for the context
  pane. There is no adaptive visual-state graph to evaluate on every layout transition.
- **Realtime media never executes in XAML/SwiftUI callbacks.** UI surfaces consume snapshots and
  commands; WASAPI/CoreAudio, Opus, jitter, capture, and transport remain below the presentation
  layer.
- **Dynamic people/room lists must virtualize.** When those models arrive, list cost must scale with
  visible rows, not total membership.
- **High-frequency diagnostics stay local to the page that needs them.** The existing audio meter is
  10 Hz and stops its timer when not running. Future speaking indicators should follow the same
  bounded-update rule.
- **No bitmap decoration is required.** The shell uses text, vector glyphs, borders, and flat fills,
  avoiding image decode/upload work at startup.

The practical objective is that idle shell cost approaches the cost of one static WinUI/SwiftUI
tree plus event dispatch. Media threads should not be able to distinguish whether Home, Voice, or
Share is visible except for bounded snapshot publication.

## Visual system

The theme deliberately avoids blue. The main palette is warm neutral with restrained semantic color:

- background: warm ivory / charcoal
- surface: off-white / warm graphite
- accent: muted terracotta
- positive: desaturated sage
- secondary emphasis: dusty rose and sand

Spacing uses 4/8/12/16/24/32 px steps. Cards use 12–18 px radii. Large shadows and glass effects are
not part of the product language.

Light, dark, and high-contrast resources share semantic keys, so product views do not carry literal
theme colors.

## Connection boundaries

The current surfaces are intentionally sparse but their integration points are fixed:

- **Home** — recent spaces, people, and local media summary.
- **Voice** — room selection, participant roster, mute/deafen/share controls, speaking state.
- **Share** — source picker, capability-derived quality plan, system/microphone audio policy.
- **System** — capability evidence and audio diagnostics.
- **Settings** — appearance, device defaults, and user-facing performance policy.

Room/account/network state must not be stored inside XAML controls. It belongs in plain/native model
objects and is projected into the shell. That keeps the UI replaceable and testable while the media
core remains independent.
