# A.7.3 — openal-soft + EFX reverb + HRTF migration (2026-05-29, gitea #129)

## Decision

Полная миграция macOS xrSound с deprecated `/System/Library/Frameworks/OpenAL.framework`
на Brew-keg `openal-soft` (`/opt/homebrew/opt/openal-soft`) + revival
EFX-реверб backend'а + opt-in HRTF.

**CMake** (`cmake/XRay.Compiler.GNULike.cmake`): Apple-блок инжектит
`OPENAL_ROOT` (плюс `OpenAL_ROOT` mixed-case под CMake 3.27+ CMP0144) и
`CMAKE_FIND_FRAMEWORK NEVER` перед `find_package(OpenAL)` — keg-only
formula теперь резолвится без `brew link --force`.

**EFX backend** (`SoundRender_EffectsA_EFX.{h,cpp}`): возвращён в дерево
с upstream-коммита `d215214c` (2022-11-10, OGSR port), сразу проапгрейжен
до `AL_EFFECT_EAXREVERB` (вместо initial `AL_EFFECT_REVERB`). Selection
в `SoundRender_CoreA.cpp` — параллельные `#if XR_HAS_EAX` / `#if XR_HAS_EFX`
блоки с `!m_effects` short-circuit; EAX выигрывает на Windows, EFX
активируется на маке (где `XR_HAS_EAX` undef из-за отсутствия EAX
Externals). Gated через `__has_include(<efx.h>)`.

**Aux slot wiring** (`SoundRender_TargetA.{h,cpp}` + `CoreA.cpp`): новый
параметр `ALuint slot` в `CSoundRender_TargetA` ctor; в `_initialize()`
каждый source привязывается к slot'у через
`alSource3i(AL_AUXILIARY_SEND_FILTER, slot, 0, AL_FILTER_NULL)`.
**Без этого binding'а** aux slot живёт пустым, effect attached, но ни
один source не подаёт сигнал в wet path — это был основной landmine
первой версии PR (effect был, реверба не было).

**Env defaults** (`SoundRender_Environment.cpp`): `set_default()`,
`set_identity()`, `clamp()` распакованы из `#if defined(XR_HAS_EAX)`
обёрток. EAXLISTENER_DEFAULT_* и EAXLISTENER_MIN/MAX_* теперь
hardcoded числовыми константами — EAX 2.0 spec значения универсальны,
зависимость от Creative SDK headers искусственная. До этого fix'а
default-constructed `CSoundRender_Environment` имел все поля = 0 →
`set_listener` подавал нули в `AL_EAXREVERB_*` → металлический ringing.

**Lifecycle через `snd_efx` cvar**: новый virtual `detach()` в
`CSoundRender_Effects` (default no-op, override в EFX backend через
`alAuxiliaryEffectSloti(slot, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL)`).
`SoundRender_Core::update_listener` теперь при `ss_EFX` off вызывает
`detach()` вместо early-return — иначе previously-committed effect
оставался прикреплён к slot'у и продолжал играть, делая cvar fake.

**Debug preset switcher**: новый int cvar `snd_efx_preset` (0..5).
`0` = env-data driven (vanilla CoP, default). `1..5` = hardcoded
preset из `<efx-presets.h>` (BATHROOM, AUDITORIUM, HANGAR,
STONECORRIDOR, CAVE) — переключается live в консоли. Этот cvar
доказал что EFX pipeline работает end-to-end во время smoke сессии,
когда env-driven результат казался subtle и не было понятно — backend
broken или env-data слабые.

**HRTF**: `ALC_HRTF_SOFT=ALC_TRUE` + `ALC_HRTF_ID_SOFT=ALC_DONT_CARE_SOFT`
attribute при `alcCreateContext`. Статус читается через
`alcGetIntegerv(ALC_HRTF_STATUS_SOFT)` и логируется (7 case'ов).
Если backend rejects attribute list — graceful fallback на nullptr
attrs (Apple framework как раз не понимает SOFT extensions).

**Packaging** (`scripts/mac/package_app.sh`): `Default HRTF.mhr`
копируется в `.app/Contents/Resources/openal/hrtf/`, launcher ставит
`ALSOFT_LOCAL_PATH`. `libopenal.1.dylib` подтягивается существующим
`otool -L` dep-walker'ом (Brew path `/opt/homebrew/...` → `@rpath/`).

## Why

A.5 audit ([`a5-audio-audit.md`](a5-audio-audit.md)) поймал что
движок линкуется к Apple framework (deprecated с macOS 10.15, без
EFX, без HRTF), хотя Brewfile уже декларировал `openal-soft`. Cvar
`snd_efx 1` был silent no-op — `m_effects == nullptr` навсегда,
потому что единственный backend (`CSoundRender_EffectsA_EAX`) gated
по `<eax/eax.h>` которого нет на Apple. Issue #128 уже добавил
init-time warning (`8acc15610`), но это симптоматическое лечение —
root cause требовал миграции на openal-soft и оживления EFX backend.

`AL_EFFECT_EAXREVERB` вместо initial `AL_EFFECT_REVERB`: первый —
basic mono реверб который технически работает, но звучит slabo
на vanilla CoP env data. Второй — расширенный preset (то же количество
параметров что используется в Windows-EAX-версии CoP), даёт recognizable
character реверба с теми же env-данными. Upgrade недорогой (rename
enum'ов + добавить extended defaults в constructor), env-data layout
не меняется.

Hardcoded EAX constants в Environment defaults — потому что значения
универсальны (EAX 2.0 spec), но именно константы exposed только через
Creative SDK headers. Зависимость искусственная, hardcode чище для
fork'а который Windows не поддерживает.

`snd_efx_preset` debug cvar — добавили во время smoke сессии когда
env-driven реверб казался subtle и было непонятно — backend broken
или env-data CoP уровней так настроены. BATHROOM preset через cvar
дал немедленное доказательство что pipeline (effect → slot → sources
via aux send) полностью функционирует, и проблема была лишь в том
что vanilla CoP env-data действительно записаны с тонким реверов
для outdoor зон. Cvar остался как valuable debug tool для будущих
env tuning sessions — default 0 = production mode без overhead.

## Trade-off

- **Single global aux slot.** Все звуки (music streams, UI клики,
  voice NPC, weapon fire, ambient) идут через один реверб. Upstream
  pattern из `d215214c`. На vanilla env-data незаметно (реверб тихий),
  но на любом hardcoded preset'е (например BATHROOM через `snd_efx_preset 1`)
  реверб слышен и на music/UI что architecturally questionable.
  Решается отдельной задачей — source category routing
  (`CSoundRender_Emitter::type` для music/UI bypass aux). Деферим
  как P3 follow-up (task #15).
- **HRTF default-on, без cvar.** Если игрок на колонках, HRTF может
  размазать spatial. openal-soft автодетектит наушники (видно в логе:
  `HRTF auto-enabled (headphones detected)` vs `HRTF disabled`), но
  explicit user override недоступен. polish-task.
- **AL_INVALID_VALUE на `alGetSourcei(AL_SOURCE_STATE)`**: openal-soft
  строже чем Apple framework к source state queries в transitional
  state. Логируется как `! update:: source state check failed (0x40963)`
  в `SoundRender_TargetA.cpp:104`. Не функциональная регрессия —
  ранний return пропускает обновление target'а на кадр. Открыт
  task #11.

## Revisit if

- User feedback что нужно extended EAXREVERB params (DecayLFRatio,
  Density, HFReference, EchoTime/EchoDepth, Modulation*, ReflectionsPan,
  ReverbPan) — порт mature `d7c4d3047^` backend + env file format
  bump 0x0004→0x0005 + backward-compat read path.
- Brew formula changes location (e.g. drop `/opt/openal-soft` symlink) →
  расширить `OPENAL_ROOT` discovery в CMake.
- Apple ships native spatial audio API (AVAudioEngine reverb / PHASE)
  worth migrating to → отдельная phase B audio task. Создать
  `audio-engineer` agent на старте (task #14).
- HRTF off-by-default request от user → добавить `snd_hrtf` cvar
  который ре-создаёт context с `ALC_HRTF_SOFT=ALC_FALSE`.

## Pipeline confirmed via debug preset switcher

Во время smoke сессии:
- env-driven реверб (`snd_efx_preset 0`) звучал subtle — было непонятно
  работает ли backend или env-data слабые
- `snd_efx_preset 1` (BATHROOM) — явно слышен на всём (шаги, выстрелы,
  voice, music, UI) → end-to-end pipeline доказан
- `snd_efx 0` → реверб моментально пропадает (detach работает)
- `snd_efx 1` → реверб возвращается

Это полное functional доказательство A.7.3.

## References

- A.5 audit: [`a5-audio-audit.md`](a5-audio-audit.md)
- Upstream EFX history:
  - add: `d215214c` (2022-11-10, OGSR port, initial AL_EFFECT_REVERB)
  - remove: `d7c4d3047` (2024-05-06, «unfinished + SteamAudio better»)
  - mature: `d7c4d3047^` (с env format bump, не портировали — деферим)
- Brew openal-soft formula: 1.25.2 (2026-era), keg-only at
  `/opt/homebrew/opt/openal-soft`
- Issue #128 (warn when snd_efx no effect): `8acc15610`
- Issue #129 (this work): branch `issue-129-openal-soft-efx`
- Follow-ups:
  - task #11 — AL_INVALID_VALUE openal-soft strictness в source state queries
  - task #15 — source category routing (music/UI bypass реверба)
  - task #14 — audio-engineer agent при старте phase B audio
