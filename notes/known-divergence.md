# Known divergence: macOS GL vs vanilla DX11

Чисто render-engine отличия macOS-GL-бекенда от DX11-референса. Каждая
запись — поведение, которое мы НЕ привели к pixel-perfect parity с
vanilla, либо потому что fix дорогой / откладывается, либо потому что
причина в Apple GL 4.1 и обходится конфигом.

Каждый пункт — actionable: что отличается, почему, что мы сделали,
куда копать если возвращаться.

---

## bloom-threshold default (Apple GL only)

**Symptom**: на vanilla cvars (`r2_ls_bloom_threshold 0.00001` — engine
default) сцены с сильным контрастом света (Yanov station с арочными
окнами, бункер ученых с лампами) пересвечиваются до чистого белого на
стенах/потолке. На DX11 с тем же threshold — мягкий атмосферный bloom
без блюминга.

**Investigated** (2026-05-20, issue #19):

- Tonemap полностью не виноват: `r2_tonemap off` и `r2_tonemap_amount
  0` не убирают пересвет (user A/B).
- `r2_tonemap_middlegray` влияет слабо — adaptation тоже.
- `r2_ls_bloom_threshold 0.5` полностью убирает пересвет.
- `0.05` даёт vanilla-comparable bloom без blow-out (user-verified).

**Root cause**: shader-side различие в bloom pipeline. Файлы
`res/gamedata/shaders/gl/bloom_build.ps`, `bloom_filter.ps`,
`bloom_luminance_*.ps` структурно идентичны DX vanilla, но runtime
behavior расходится. Возможные источники (не investigated точечно):

- Sampler binding (DX явно использует `smp_rtlinear`, GL — default,
  настраивается через `r_Sampler_clf`).
- Precision: DX vanilla использует half-precision (`1.h`, `half4`),
  GL — full float. На bright HDR values это даёт более широкий
  диапазон до saturation.
- HDR pixel format clipping в промежуточных RT (rt_Bloom_1/2 формат).
- ClearRT clear-value семантика для R32F / GL_RGBA16F (LUM pool
  init'ится `0x7f7f7f7f`).

**Mitigation** (commit TBD, branch issue-19-lighting-divergence):

- В `src/Layers/xrRender/xrRender_console.cpp:203` дефолт
  `ps_r2_ls_bloom_threshold` поднят до `0.05f` под
  `#if defined(XR_PLATFORM_APPLE)`. Vanilla `0.00001f` сохранён для
  не-Apple платформ.
- `XXX YANOV_BLOOM` маркер в коде объясняет почему.
- Пользователь может override'ить в `user.ltx`.

**To investigate later**:

- Дописать `XXX` маркеры в shader sources с инструкцией компиляции
  и сравнения output.
- Сделать debug-вывод значений `b_params`, `tm_scale`,
  `f_luminance_adapt` в engine лог.
- Запатчить bloom_build.ps чтобы проверить является ли он причиной
  (заменить `hi = dot(avg, 1) - b_params.x` на принудительный `0` —
  должно ли bloom стать прозрачным; если да — pipeline OK, проблема
  в дальнейших фазах).

**Upstream-кандидат**: возможно, если найдём root cause. Сейчас —
не PR-кандидат (Apple-specific workaround).

---

## (template for future entries)

## title

**Symptom**: …

**Investigated**: …

**Root cause**: …

**Mitigation**: …

**To investigate later**: …
