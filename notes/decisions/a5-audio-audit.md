# A.5 — Audio audit

**Date:** 2026-05-27
**Issue:** [#127](https://git.fedorov.tech/ragnar/xray-16/issues/127)
**Branch:** `issue-127-a5-audio-audit`
**Status:** Гейт зелёный. Шаг закрыт без code changes. Найдены follow-up'ы — открыты отдельными issues.

## TL;DR

- **A.5 strict gate (`grep SDL_audio* пусто`) выполнен:** 0 матчей в `src/`.
- `SDL_Init` поднимает только `SDL_INIT_VIDEO [| SDL_INIT_GAMECONTROLLER]` — аудио-подсистема SDL не используется (`src/xrEngine/x_ray.cpp:229`).
- xrSound целиком на OpenAL напрямую (`SoundRender_CoreA.cpp`).
- **Bundled P2 finding (EFX/HRTF):** в текущей сборке мы линкуемся к Apple `/System/Library/Frameworks/OpenAL.framework` (deprecated с macOS 10.15). Brewfile установил `openal-soft` keg-only, но `find_package(OpenAL)` резолвит system framework первым. Результат: HRTF + EFX/EAX недоступны, spatial audio — простой amplitude panning. **Это объясняет user's P2 «пространственное аудио на macOS работает не как надо».**

## Premise audit (foreground)

```bash
$ grep -rE "SDL_audio|SDL_OpenAudio|SDL_INIT_AUDIO|SDL_AudioSpec|SDL_MixAudio|SDL_PauseAudio" src/
# (no matches)

$ grep -nE "SDL_Init|SDL_InitSubSystem" src/xrEngine/x_ray.cpp
228:    ZoneScopedN("SDL_Init");
229:    u32 flags = SDL_INIT_VIDEO;
230:    if (!strstr(commandLine, "-no_gamepad"))
231:        flags |= SDL_INIT_GAMECONTROLLER;
236:    R_ASSERT3(SDL_Init(flags) == 0, ...);
```

## Apple-side (linkage) — отчёт apple-platform

### Какой backend линкуется

```
$ otool -L /Applications/OpenXRay-Dev.app/Contents/MacOS/xr_3da | grep -i openal
    /System/Library/Frameworks/OpenAL.framework/Versions/A/OpenAL
        (compatibility version 1.0.0, current version 1.0.0)
```

Apple's deprecated framework. `libopenal*.dylib` нигде в bundle нет. Линкер запёк абсолютный путь напрямую (`/System/...`), не `@rpath`.

### Почему system framework выигрывает у openal-soft

- `cmake/XRay.Compiler.GNULike.cmake:160` — `find_package(OpenAL REQUIRED)` без hint, без `NO_DEFAULT_PATH`.
- `src/xrSound/CMakeLists.txt:134` — `target_link_libraries(... OpenAL::OpenAL)`.
- CMake's built-in `FindOpenAL.cmake` ищет `/System/Library/Frameworks` первым → попадает в Apple framework немедленно.
- Brew `openal-soft` формула — `keg_only :provided_by_macos`. Установлен в `/opt/homebrew/Cellar/openal-soft/1.25.2/`, но **НЕ** симлинкуется в `/opt/homebrew/lib`. Без явного `OPENAL_ROOT=/opt/homebrew/opt/openal-soft` CMake его не видит.
- `find_package(OpenAL REQUIRED)` стоит в общем `if (NOT WIN32)`, не в `if(APPLE)` — Linux получает openal-soft через pkg-config (нет конкурирующего framework), macOS получает Apple framework.

### Apple OpenAL.framework deprecation status

- Deprecated с **macOS 10.15 Catalina** (Oct 2019). Apple рекомендует `AVAudioEngine` + `PHASE`.
- Build silent: в `~/Library/Logs/OpenXRay/ship-*.log` **нет** deprecation warning'ов для OpenAL (видимо headers смягчили attribute или флаги подавляют).
- Что отсутствует vs OpenAL Soft 1.25.2:
  - **EFX/EAX** — `ALC_EXT_EFX` не экспонируется, нет reverb/occlusion/effect slots.
  - **HRTF** — нет HRTF processing path. Spatialization = vector-based amplitude panning.
  - **`AL_EXT_FLOAT32`** — Apple экспонирует `AL_EXT_float32` (lowercase); xrSound правильно проверяет оба (`SoundRender_CoreA.cpp:84-85`).
  - **`AL_SOFT_*`** family — нет.

### HRTF / spatial — runtime impact

Mono sources с `AL_POSITION` работают, но через простой panning без HRIR convolution:

- Нет «в front vs behind» elevation cue.
- Нет «above vs below» cue.
- «Сзади игрока» использует ту же panning curve что «перед игроком» того же azimuth — различимо только через environment cues (reverb, occlusion) — а их у нас нет (no EFX).
- Distance attenuation (`AL_DISTANCE_MODEL`, rolloff) работает корректно — in-spec OpenAL 1.1.

Для headphone users — реальная regression vs Windows/Linux (где openal-soft + HRTF by default с 1.18). Для speaker users impact меньше.

### Brewfile состояние

- `Brewfile:11` — `brew "openal-soft"` декларирован.
- `brew --prefix openal-soft` → `/opt/homebrew/opt/openal-soft` (1.25.2, установлен).
- HRTF data файл шипится: `share/openal/hrtf/Default HRTF.mhr`.
- **Платим установку, не пользуемся.** Брэйкер: keg-only.

## xrSound-side (code) — отчёт cpp-engineer

### alcCreateContext attributes

```cpp
// src/xrSound/SoundRender_CoreA.cpp:60
pContext = alcCreateContext(pDevice, nullptr);
```

**`nullptr` attribute list.** Ни `ALC_FREQUENCY`, ни `ALC_MONO_SOURCES`, ни `ALC_HRTF_SOFT`. Полагаемся на driver defaults.

На Apple framework: 44.1 kHz / 32 sources / stereo / no HRTF. **Запроса HRTF нет нигде в xrSound** — даже если бы линковался openal-soft, мы бы HRTF не включили без явного atttribute pair.

### OpenALDeviceList::SelectBestDevice (`src/xrSound/OpenALDeviceList.cpp:178-214`)

Фильтрует устройства по `name == m_defaultDeviceName`, ранжирует по `ALC_MAJOR_VERSION/MINOR_VERSION`. EAX/EFX флаги собираются на `:62-71`, **никогда не консультируются в ranking'е**, только в diagnostic logging (`!MASTER_GOLD`) и на init gate `SoundRender_CoreA.cpp:91`.

Apple framework возвращает `1.1` для всех. Ranking degenerate.

### EFX / EAX runtime path

`SoundRender_EffectsA_EAX.h:5`:
```cpp
#if __has_include(<eax/eax.h>)
#   define XR_HAS_EAX
class CSoundRender_EffectsA_EAX : public CSoundRender_Effects { ... };
#endif
```

На macOS `<eax/eax.h>` отсутствует → `XR_HAS_EAX` undefined → class не компилируется. Init block `SoundRender_CoreA.cpp:89-100` под `#if defined(XR_HAS_EAX)` → noop. **`m_effects == nullptr` навсегда.**

**Нет non-EAX EFX backend.** `CSoundRender_Effects` — pure virtual interface, единственная имплементация — `EAX`. Нет `ALC_EXT_EFX` / `AL_EFFECT_REVERB` пути с использованием стандартного openal-soft EFX. Флаг `addedDevice.props.efx` (`OpenALDeviceList.cpp:71`) собирается, **никогда не используется** чтобы инстанциировать backend.

### `snd_efx` cvar plumbing

```cpp
// src/xrEngine/xr_ioc_cmd.cpp:869
CMD3(CCC_Mask, "snd_efx", &psSoundFlags, ss_EFX);
```

Default: `ss_Hardware | ss_EFX | ss_UseFloat32` (`SoundRender_Core.cpp:15-18`).

Runtime gate:
```cpp
// SoundRender_Core.cpp:195
if (!psSoundFlags.test(ss_EFX) || !m_effects)
    return;
```

**На macOS, где `m_effects == nullptr` permanently, `snd_efx 1` silently ignored** — нет error, нет лога, нет warning. Cvar появляется в console autocomplete, принимает assign, но эффект unreachable. **Это user-facing bug** — пользователь думает что включил EFX, а ничего не происходит. Открыт follow-up issue.

### HRTF / spatial code в эмиттерах

`SoundRender_TargetA.cpp:152-156`:
```cpp
A_CHK(alSourcef (pSource, AL_REFERENCE_DISTANCE, src.min_distance));
A_CHK(alSourcef (pSource, AL_MAX_DISTANCE,       src.max_distance));
A_CHK(alSource3f(pSource, AL_POSITION,           pos.x, pos.y, -pos.z));
A_CHK(alSourcei (pSource, AL_SOURCE_RELATIVE,    m_pEmitter->b2D));
A_CHK(alSourcef (pSource, AL_ROLLOFF_FACTOR,     psSoundRolloff));
```

Mono/stereo selection правильный (`SoundRender_TargetA.cpp:47-53`); stereo OGG'и форсятся в 2D в `SoundRender_Scene.cpp:180-203`. Никаких `AL_DIRECT_CHANNELS_SOFT`, никаких HRTF hints.

**`alDistanceModel` нигде в `src/` не вызывается** — полагаемся на default `AL_INVERSE_DISTANCE_CLAMPED`. На обоих backend'ах identical default.

`AL_VELOCITY` всегда `(0,0,0)` (`SoundRender_CoreA.cpp:79, 156`) — Doppler effectively disabled. Стандартно для X-Ray.

### Apple-conditional code в xrSound

**Ноль `XR_PLATFORM_APPLE` / `__APPLE__` / `TARGET_OS_MAC` в `src/xrSound/`.** Единственное упоминание macOS — comment `// second is macOS` про `AL_EXT_float32` lowercase variant (`SoundRender_CoreA.cpp:85`).

→ macOS audio surface **полностью implicit**. Любая divergence между Apple framework и openal-soft surface'ится как runtime behavior delta без grep'абельного маркера.

### Lifecycle / shutdown

Init order (`x_ray.cpp:270-307`):
1. `Engine.Sound.CreateDevicesList()` (`alcOpenDevice` + close на каждое устройство для probe — повторяющиеся open/close на Apple framework мягко подозрительно но не верифицировано).
2. Wait task → `Engine.Sound.Create()` → `SoundRender->_initialize()`.

Shutdown order (`x_ray.cpp:340-365`):
1. `:348` `destroy_persistent` (game + level teardown).
2. `:352` `xr_delete(pInput)`.
3. `:361` `Engine.Sound.Destroy()` → `CSoundManager::Destroy` → `SoundRender->_clear()` (`SoundRender_CoreA.cpp:128-147`):
   - sources delete → `m_effects` delete (noop на macOS) → targets delete → `alcMakeContextCurrent(nullptr)` → `alcDestroyContext` → `alcCloseDevice` → `pDeviceList` delete.
4. `:363` `Device.Destroy()`.

**Sound destroyed BEFORE Device** — инверсия spatial-DB hazard'а. Sound не держит spatial-DB ref'ов → safe.

`~CSoundRender_Scene` skip-on-shutdown patch (commit `16cffe997`):
```cpp
// SoundRender_Scene.cpp:14-26
if (g_bShuttingDown) { s_emitters.clear(); return; }
```

Гейтится `g_bShuttingDown`, не `g_bStaticDestruction`. Безопасно потому что scene destruction внутри `Engine.Sound.Destroy()` (до static destruction), sources в `_clear` рвут pointer-back chain первыми, spatial-DB не вовлечена.

**Latent fragility:**
- `GEnv.Sound = nullptr` precedes `_clear` и `xr_delete` в `Sound.cpp:41-44`. Любой post-`Destroy` доступ → null deref. Не observed bug, но invariant хрупкий.
- `CSoundRender_EffectsA_EAX` member'ы `m_is_supported`/`m_is_deferred` не value-initialised в header (`:13-17`). Ctor early-return'ит до их assign. Currently masked `initialized()` short-circuit (`Core.cpp:195`). Future caller который skipnет check → читает uninit memory. **Vanilla bug, upstream PR candidate.**

## Действия — итог

### A.5 закрыт без правок кода

Гейт зелёный. SDL audio не используется. xrSound на OpenAL напрямую. PR — этот audit-документ + `engine-map.md` + roadmap update.

### Follow-up issues открыты

| Severity | Issue | Что | Эффект |
|---|---|---|---|
| P2 | [#128](https://git.fedorov.tech/ragnar/xray-16/issues/128) | `snd_efx` silently no-ops на macOS | Лог `Msg("* SOUND: EFX backend absent — snd_efx cvar has no effect")` один раз при init если `m_effects == nullptr`. ~5 строк. Опционально hook на cvar-change. |
| P3 | [#129](https://git.fedorov.tech/ragnar/xray-16/issues/129) | openal-soft migration plan | Переключиться на keg-only openal-soft → HRTF + EFX. Требует: `OPENAL_ROOT` injection в `if(APPLE)` в `cmake/XRay.Compiler.GNULike.cmake`, новый `SoundRender_EffectsA_EFX.cpp` backend (нет в codebase), `ALC_HRTF_SOFT` attribute, dylib packaging в `scripts/mac/package_app.sh`. **Большой шаг, не блокер A.6.** |
| P3 | TBD (upstream PR кандидат, пока не подавал) | `CSoundRender_EffectsA_EAX` uninit member'ы | Value-initialize `m_is_supported{}`, `m_is_deferred{}` в header. Vanilla bug, не Apple-specific. Подождём пока запушим A-track, чтобы не путать апстрим. |

### Не делать

- ❌ Не пытаться откатить EFX-default-on. Cvar silently игнорируется — не падает.
- ❌ Не переходить на openal-soft в этом же PR — это отдельная работа с C-side migration (EFX backend impl) и dylib packaging.

## Landmines для playbook (добавлены в engine-map / gotchas)

1. **`find_package(OpenAL REQUIRED)` на macOS silently выбирает Apple framework over Brew openal-soft** — даже если Brewfile установил openal-soft. Brew keg-only by design. Любой fix требует `OPENAL_ROOT` инjection в `if(APPLE)`.
2. **xrSound имеет ноль Apple-conditional кода.** Divergence Apple framework vs openal-soft surface'ится без grep'абельного маркера. Audits / migrations должны знать.

## Links

- A.4 timing (предыдущий шаг): [#123](https://git.fedorov.tech/ragnar/xray-16/issues/123), PR #126
- Spec: `docs/superpowers/specs/2026-05-25-native-shell-roadmap.md` § A.5
- P2 origin: `notes/strategy/roadmap.md:74`
- Apple OpenAL deprecation: Apple Developer Docs (macOS 10.15 release notes)
