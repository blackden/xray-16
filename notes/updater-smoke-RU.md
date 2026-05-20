# Прокликать обновлятор (issue #39) — пошагово

Что мы проверяем: кнопка «Проверить обновления» в Options → Игра реально
ходит за манифестом, сравнивает версию, скачивает .app.zip, показывает
правильные диалоги. Боевой intranet-сервер пока не нужен — гоняем против
локального `python -m http.server` на 127.0.0.1.

Перед началом:

- Ветка: `issue-39-updater-button` (она уже в `github-backup`)
- Исполняемый: `bin/arm64/Mixed/xr_3da` (свежий из сборки этой сессии)
- Локализационные ключи (`ui_st_conn_new_patch`, `ui_st_conn_no_new_patch`,
  `ui_st_patch_download_error`, `ui_st_patch_download_success`) уже есть
  в стоковой `gamedata`, ничего добавлять не надо

---

## Шаг 0. Убедиться что свежий xr_3da собран

```sh
cd ~/fedorov_tech/xray-16
git checkout issue-39-updater-button
cmake --build build --parallel
```

Должно собраться чисто. Если у тебя дефолтная сборка под другой config —
дай знать, я подскажу как пересобрать.

---

## Шаг 1. Поднять локальный сервер с фикстурами

В **первом терминале**:

```sh
mkdir -p /tmp/xr-updater && cd /tmp/xr-updater

# Фейковый zip (содержимое не важно, проверяется только транспорт)
printf 'fake app zip body' > dummy.app.zip

# Манифест: версия НА СЕРВЕРЕ новее чем у клиента
cat > manifest-stable.ltx <<'EOF'
[update]
version   = 1.6.fork.test-new
channel   = stable
asset_url = http://127.0.0.1:8000/dummy.app.zip
sha256    = 0000000000000000000000000000000000000000000000000000000000000000
size      = 17
notes     = smoke test scenario A
EOF

python3 -m http.server 8000
```

Сервер должен сказать `Serving HTTP on 0.0.0.0 port 8000 ...` и висеть.
**Этот терминал не закрываем до конца теста.**

Проверь что сервер вообще отвечает — в третьем терминале:

```sh
curl -s http://127.0.0.1:8000/manifest-stable.ltx
```

Должен вывалить содержимое манифеста.

---

## Шаг 2. Запустить движок

Во **втором терминале**:

```sh
cd ~/fedorov_tech/xray-16
./bin/arm64/Mixed/xr_3da -fsltx $COP_LTX_FILE_PATH
```

(Если переменная `COP_LTX_FILE_PATH` у тебя не настроена — подставь
явный путь к `fsgame.ltx` от CoP, как в `.vscode/launch.json`.)

Окно игры открылось — переходим к проверкам.

---

## Шаг 3. **Сценарий 0** — проверка плумбинга версии (без клика)

В логе движка (открывается из главного меню «Дневник» → «Лог», или
`tail -f ~/Library/Logs/OpenXRay/*.log`) должна быть строка:

```
Fork version: 1.6.fork.dev
```

рядом со стандартной `OpenXRay ... build NNNN ...`. **Это подтверждает 39.a
без какого-либо клика.** Если её нет — что-то пошло не так с
`XRAY_FORK_VERSION`, дай знать.

---

## Шаг 4. **Сценарий A** — есть обновление

1. Главное меню → **Опции** → таб **Игра** (gameplay)
2. Жми **«Проверить обновления»** (внизу таба)
3. Ожидаемое:
   - В логе появляется `updater: checking http://127.0.0.1:8000/manifest-stable.ltx`
   - Через ~1 секунду — `updater: current=1.6.fork.dev, manifest=1.6.fork.test-new, channel=stable`
   - Появляется диалог **`msg_box_new_patch`** (две кнопки **Yes / No**),
     текст: «Доступна новая версия игры. Загрузить?» (или похожий перевод
     ключа `ui_st_conn_new_patch`)
4. Жми **Yes**
5. Ожидаемое:
   - В логе `updater: downloading http://127.0.0.1:8000/dummy.app.zip -> .../pending.app.zip`
   - Через мгновение (файл крошечный) — `updater: download complete -> ... (sha256=0000..., size=17)`
   - Появляется диалог **`msg_box_patch_download_success`** (Yes/No,
     `ui_st_patch_download_success`)
6. Проверь что файл реально лёг:

```sh
ls -la ~/Library/Application\ Support/GSC\ Game\ World/S.T.A.L.K.E.R.\ -\ Call\ of\ Pripyat/updates/pending.app.zip
```

(точный путь зависит от того как у тебя настроен `$app_data_root$`, может
быть `~/.local/share/GSC...` или симлинк — смотри по тому что движок
выводит в `application root:` при старте)

7. Жми **Yes** на success-диалоге. В логе:
   - `updater: restart requested; pending update at .../pending.app.zip`
   - Игра НЕ перезапускается сама — атомарный swap пока вне скоупа этого
     MVP, об этом отдельным follow-up issue. Главное что pending.app.zip
     лежит на диске.

✅ **Сценарий A прошёл.**

---

## Шаг 5. **Сценарий B** — нет обновления

1. Не закрывая игру, отредактируй манифест на сервере:

```sh
sed -i '' 's/test-new/dev/' /tmp/xr-updater/manifest-stable.ltx
cat /tmp/xr-updater/manifest-stable.ltx  # убедись что теперь version = 1.6.fork.dev
```

(Python http.server читает файлы при каждом запросе, перезапускать
не надо.)

2. Снова: Опции → Игра → «Проверить обновления»
3. Ожидаемое:
   - В логе `updater: current=1.6.fork.dev, manifest=1.6.fork.dev, ...`
   - Появляется диалог **`msg_box_no_new_patch`** («Новых обновлений нет»)

✅ **Сценарий B прошёл.**

---

## Шаг 6. **Сценарий C** — сервер отвалился

1. В **первом терминале** жми `Ctrl+C` — гасим python.
2. В игре снова жми «Проверить обновления».
3. Ожидаемое:
   - В логе `! updater: manifest fetch failed` (или похожий error от ghttp)
   - Появляется диалог **`msg_box_patch_download_error`**

4. Подними python обратно: в первом терминале снова `python3 -m http.server 8000`.

✅ **Сценарий C прошёл.**

---

## Шаг 7. **Сценарий D** — манифест битый

1. Сломай манифест:

```sh
echo 'this is not a valid ltx' > /tmp/xr-updater/manifest-stable.ltx
```

2. В игре: «Проверить обновления»
3. Ожидаемое:
   - В логе `! updater: manifest is malformed (length=...)`
   - Диалог **`msg_box_patch_download_error`**

4. Восстанови манифест:

```sh
cat > /tmp/xr-updater/manifest-stable.ltx <<'EOF'
[update]
version   = 1.6.fork.test-new
channel   = stable
asset_url = http://127.0.0.1:8000/dummy.app.zip
sha256    = 0000000000000000000000000000000000000000000000000000000000000000
size      = 17
notes     = smoke test
EOF
```

✅ **Сценарий D прошёл.**

---

## Шаг 8. **Сценарий E** — отмена в процессе

Этот сценарий хочется быстро, но dummy-файл слишком мал чтобы успеть
кликнуть Cancel. Сделай большой файл:

```sh
# 200 МБ нулей — успеешь нажать Cancel
dd if=/dev/zero of=/tmp/xr-updater/dummy.app.zip bs=1m count=200
# Обнови size в манифесте — иначе ghttp может не дать прогресс
sed -i '' 's/^size.*/size      = 209715200/' /tmp/xr-updater/manifest-stable.ltx
```

1. В игре: «Проверить обновления» → Yes на NewPatch
2. Пока крутится прогресс — жми **Cancel** (если в UI нет кнопки, используй
   консоль (`~`): `cancel_download` — впрочем такой команды нет, и в
   текущем MVP кнопка Cancel внутри XML патча тоже не выведена в UI;
   проверка отмены через `CancelDownload()` будет проще из консоли в
   будущем follow-up)

⚠️ **Если кнопка Cancel недоступна — этот сценарий пропускаем.** Это
лекго добавить как `cancel_download` console-команду отдельным
follow-up issue, не критично для MVP.

---

## Шаг 9. **Сценарий F** — консольное переопределение URL

1. Открой консоль в игре (клавиша `~` / тильда)
2. Введи:

```
updater_manifest_url http://127.0.0.1:8000/nonexistent.ltx
check_for_updates
```

3. Ожидаемое: `msg_box_patch_download_error` (404 от python, манифест не
   получен)
4. Верни обратно:

```
updater_manifest_url http://127.0.0.1:8000/manifest-stable.ltx
```

✅ **Сценарий F прошёл.** Это критично — он подтверждает что в проде
достаточно будет один раз набрать `updater_manifest_url
http://updates.vg.lan/...` и сохранить, без пересборки клиента.

---

## Если что-то не сходится

- **Кнопка «Проверить обновления» не реагирует** — проверь что в логе
  есть `updater: checking ...`. Если нет — Lua-callback не доходит до
  console-команды; возможно user_ctl.script / mm_opt_main.script
  кэшируется. Удали `~/Library/Application Support/.../shaders_cache`.
- **`msg_box_new_patch` не появляется, но в логе всё хорошо** — XML-шаблон
  не нашёлся в `message_box_16.xml`. Все три (`msg_box_new_patch`,
  `msg_box_patch_download_error`, `msg_box_patch_download_success`) уже в
  vanilla `gamedata`, но если ты в SoC-моде (пока не поддерживается) или
  с кастомным mod-пакетом — могло перетереть.
- **`Fork version: ...` не показывается** — `XRAY_FORK_VERSION` не дошёл
  до бинаря. Чекни что сборка свежая (`stat bin/arm64/Mixed/xr_3da`).

---

## Когда всё прошло

Дай знать какие сценарии прошли, какие нет, и что выглядит странно. После
этого мерж в `macos/blackden/master`:

```sh
git checkout macos/blackden/master
git merge --no-ff issue-39-updater-button -m "Merge issue-39-updater-button: in-game updater MVP (closes #39)"
```

И закрытие issue #39 в Gitea (я могу через MCP).

---

## Что после мержа

В backlog уйдут как отдельные follow-up issues:

1. **SHA256 verification** — поле в манифесте уже читается, но не
   проверяется. Нужен OpenSSL/CommonCrypto на macOS. P2.
2. **Atomic .app swap** — сейчас pending.app.zip просто лежит в файловой
   системе, заменяет руками пользователь. P2 после того как реально
   появится intranet-сервер.
3. **Manifest signing** — P3, только если канал когда-то выйдет за VPN.
4. **Cancel-кнопка в UI** или `cancel_download` console-команда — мелкий
   P3 follow-up.
