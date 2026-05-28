# A.7.3 — openal-soft EFX backend migration (2026-05-29, gitea #129)

## Decision

Переключили macOS-сборку xrSound с deprecated
`/System/Library/Frameworks/OpenAL.framework` на Brew-keg
`openal-soft` (`/opt/homebrew/opt/openal-soft`). CMake-level: Apple-блок
в `cmake/XRay.Compiler.GNULike.cmake` инжектит `OPENAL_ROOT` и
`CMAKE_FIND_FRAMEWORK NEVER` перед `find_package(OpenAL)` —
keg-only formula теперь резолвится без `brew link --force`. xrSound-level:
вернули upstream EFX backend из коммита `d215214c` (initial
`AL_EFFECT_REVERB` версия, до extended EAXREVERB), gated через
`__has_include(<efx.h>)`. Selection в `SoundRender_CoreA.cpp` — параллельные
`#if XR_HAS_EAX` / `#if XR_HAS_EFX` блоки с `!m_effects` short-circuit,
EAX выигрывает на Windows когда оба доступны. HRTF включается через
`ALC_HRTF_SOFT=ALC_TRUE` attribute при `alcCreateContext`, статус
логируется. Packaging: `Default HRTF.mhr` копируется в
`.app/Contents/Resources/openal/hrtf/`, launcher ставит
`ALSOFT_LOCAL_PATH` чтобы openal-soft нашёл данные внутри бандла.
`libopenal.1.dylib` подтягивается существующим dep-walker'ом в
`scripts/mac/package_app.sh` (Brew path `/opt/homebrew/...` → `@rpath/`).

## Why

A.5 audit ([`notes/decisions/a5-audio-audit.md`](a5-audio-audit.md))
поймал что движок линкуется к Apple framework (deprecated с macOS 10.15,
без EFX, без HRTF), хотя Brewfile уже декларировал `openal-soft`. Cvar
`snd_efx 1` был silent no-op — `m_effects == nullptr` навсегда, потому
что единственный backend (`CSoundRender_EffectsA_EAX`) gated по
`<eax/eax.h>` который отсутствует на Apple. Issue #128 уже добавил
init-time warning (`8acc15610`), но это симптоматическое лечение — root
cause требовал миграции на openal-soft и оживления EFX backend.

Минимально-viable восстановление — порт **первой** версии EFX backend'а
из upstream (`d215214c`), а не зрелой (`d7c4d3047^`). Зрелая зависит от
env file format bump 0x0004→0x0005 с новыми полями (DecayLFRatio, Density,
HFReference, EchoTime, ReflectionsPan, ReverbPan и др.) — это многосуточный
рефакторинг `SoundRender_Environment.{h,cpp}` + content compatibility path
для существующих env-файлов. Базовый `AL_EFFECT_REVERB` даёт audible
«звук в помещении» в vanilla CoP interior зонах — этого достаточно для
playable experience.

## Trade-off

- **Нет extended EAXREVERB параметров** (DecayLFRatio, Density,
  HFReference, EchoTime, EchoDepth, ModulationTime, ModulationDepth,
  DecayHFLimit advanced, ReflectionsPan vector, ReverbPan vector).
  Reverb звучит «корректно» в interior зонах vanilla CoP, но это mono
  reverb без directionality, без per-source occlusion filter. Деферим в
  будущий P3 polish issue.
- **Single global aux slot.** Upstream pattern, не пытаемся per-source
  aux send. Все звуки идут через один reverb окружения. Это и есть
  vanilla CoP model.
- **`AUXILIARY_SEND_AUTO=AL_FALSE`** — движок драйвит reverb gain
  явно через environment system, не позволяем openal-soft'у double-dip
  через auto-attenuation.
- **HRTF default-on, без cvar.** Если игрок хочет выключить (например
  на колонках), сейчас нет UI gate — это polish-task, не critical.
  openal-soft автодетектит наушники и сам management'ит
  ENABLED/DISABLED.

## Revisit if

- User feedback что reverb fidelity недостаточна для play experience
  → port mature `d7c4d3047^` backend + env file format bump.
- Brew formula changes location (например switch на `/opt/homebrew/Cellar`
  без `/opt/openal-soft` симлинка) → расширить `OPENAL_ROOT` discovery.
- Apple ships new spatial audio API (CoreAudio AVAudioEngine reverb)
  которая лучше EFX → миграция на native backend.
- HRTF off-by-default request от user → добавить `snd_hrtf` cvar
  который ре-создаёт context с `ALC_HRTF_SOFT=ALC_FALSE`.

## References

- A.5 audit: [`a5-audio-audit.md`](a5-audio-audit.md)
- Upstream EFX history:
  - add: `d215214c` (2022-11-10, OGSR port, initial AL_EFFECT_REVERB)
  - remove: `d7c4d3047` (2024-05-06, «unfinished + SteamAudio better»)
- Brew openal-soft formula: 1.25.2 (2026-era), keg-only at
  `/opt/homebrew/opt/openal-soft`
- Issue #128 (warn when snd_efx no effect): `8acc15610`
- Issue #129 (this work): branch `issue-129-openal-soft-efx`
