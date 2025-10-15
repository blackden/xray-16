# OpenXRay

> English version: [README.md](README.md)

[![Open for everyone](misc/media/OpenXRayCover.png)](https://github.com/OpenXRay)

**OpenXRay** — улучшенная версия движка X-Ray, который используется в серии игр S.T.A.L.K.E.R. от GSC Game World.

Проект разрабатывается сообществом и не связан с GSC Game World. Тем не менее студия знает о проекте и поддерживает старания сообщества по развитию игр серии.

Инструкции по установке находятся на странице [How to install and play](https://github.com/OpenXRay/xray-16/wiki/%5BEN%5D-How-to-install-and-play).

## Поддерживаемые игры

- Call of Chernobyl 1.4.22.
- Call of Pripyat 1.6.02.
- Clear Sky 1.5.10 (возможны мелкие ошибки, но игру можно пройти полностью, см. [#382](https://github.com/OpenXRay/xray-16/issues/382)).

Shadow of Chernobyl пока **не поддерживается** (см. [#392](https://github.com/OpenXRay/xray-16/issues/392)). \
Legends of the Zone/Enhanced Edition не поддерживаются и вряд ли будут поддерживаться (см. [#1865](https://github.com/OpenXRay/xray-16/issues/1865)).

## Основные отличия от оригинального X-Ray

- Поддержка 64 бит.
- Улучшенная производительность.
- Исправление оригинальных ошибок.
- Новые возможности для модмейкеров.
- Работа на Linux, macOS, *BSD и поддержка архитектур ARM, ARM64, E2K (Эльбрус 2000), PPC64LE.

Подробное сравнение есть в таблице [Differences from original X-Ray](https://github.com/OpenXRay/xray-16/wiki/%5BEN%5D-Differences-from-original-X‐Ray).

## Цели проекта

1. Очистить код, повысить производительность и исправить ошибки оригинального движка.
2. Сделать движок максимально совместимым с оригиналом.
   1. Стремиться к 99% совместимости и идентичному поведению, где это возможно.
3. Поддерживать все три игры серии: SOC/CS/COP.
4. Создать удобную платформу для модостроителей:
   1. Добавить граф планов/рендера для тех, кто хочет расширять графические возможности.
   2. Добавить новые инструменты скриптинга, разработки и отладки.
   3. Предоставить новый SDK с дополнительными возможностями.
5. Улучшить опыт игроков за счёт новых графических, геймплейных и других опций, которые по умолчанию отключены (сохраняем близость к оригиналу).

## Как помочь проекту

### Сообщество

[![Discord](https://img.shields.io/discord/410170555619082240?label=Discord)](https://discord.gg/sjRMQwv)

Играйте и наслаждайтесь, [сообщайте об ошибках](https://github.com/OpenXRay/xray-16/issues/new/choose) и предлагайте улучшения.

Присоединяйтесь к нашему [Discord](https://discord.gg/sjRMQwv), подписывайтесь на [YouTube](https://www.youtube.com/OpenXRay), заходите в [VK](https://vk.com/openxray), оставляйте комментарии и делитесь впечатлениями. \
Вы также можете поставить звёздочку репозиторию или поддержать наш Discord.

### Модостроение

Используйте OpenXRay как платформу для своих проектов!

Обязательно соблюдайте официальное лицензионное соглашение и правила создания фан-контента: \
<https://www.gsc-game.com/eula/> \
<https://www.gsc-game.com/guidelines/>

### Разработка

[![GitHub Actions Build Status](https://github.com/OpenXRay/xray-16/actions/workflows/cibuild.yml/badge.svg)](https://github.com/OpenXRay/xray-16/actions/workflows/cibuild.yml)
[![Contributors](https://img.shields.io/github/contributors/OpenXRay/xray-16.svg?label=Contributors)](https://github.com/OpenXRay/xray-16/graphs/contributors)

Присоединяйтесь к разработке: отправляйте pull request'ы, участвуйте в обсуждениях и ревью!

Это площадка для обмена идеями и совместной работы над кодом. Помните:

- Мы стремимся сохранять близость к оригиналу. Если добавляете игровые изменения, делайте их опциональными и не нарушайте совместимость с оригинальными ресурсами (`gamedata`, `.db*`, `.xdb` и т.д.). Можно вносить неигровые улучшения, исправлять ошибки и повышать производительность.
- Крупные изменения лучше обсуждать заранее.

Полезные ссылки на [Issues](https://github.com/OpenXRay/xray-16/issues):

- Метка [good first issue](https://github.com/OpenXRay/xray-16/issues?q=is%3Aissue+is%3Aopen+sort%3Aupdated-desc+label%3A%22Good+first+issue%22) — хороший способ познакомиться с кодом.
- Метка [help wanted](https://github.com/OpenXRay/xray-16/issues?q=is%3Aissue+is%3Aopen+sort%3Aupdated-desc+label%3A%22Help+wanted%22) — более сложные задачи.

Обязательно загляните на нашу [вики](https://github.com/OpenXRay/xray-16/wiki):

- [Сборка и настройка на Windows](https://github.com/OpenXRay/xray-16/wiki/%5BEN%5D-How-to-build-and-setup-on-Windows).
- [Сборка и настройка на Linux и других платформах](https://github.com/OpenXRay/xray-16/wiki/%5BEN%5D-How-to-build-and-setup-on-Linux-and-MacOS).
- Быстрый старт для macOS ARM64: см. `docs/build-macos.ru.md`.

Ветка `dev` является основной и используется для разработки. Все pull request'ы направляйте в неё. Учтите, что иногда ветка может быть нестабильной; редкие force-push'и возможны.

Кодовая база основана на X-Ray 1.6.02 (Call of Pripyat) и была существенно переработана.

### Финансовая поддержка

[![Financial Contributors](https://opencollective.com/openxray/tiers/badge.svg?label=Financial%20contributors)](https://opencollective.com/openxray) [![Sponsors](https://img.shields.io/github/sponsors/openxray?color=brightgreen&label=Sponsors)](https://github.com/sponsors/OpenXRay) [![Patreon](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fwww.patreon.com%2Fapi%2Fcampaigns%2F5950725&query=data.attributes.patron_count&suffix=%20Patrons&color=success&label=Patreon&style=flat)](https://patreon.com/openxray)

Вы можете поддержать проект финансово через:

- [Boosty](https://boosty.to/openxray) — большая часть команды находится в России; если можете, используйте Boosty, так как вывод средств из ряда сервисов для нас ограничен.
- [GitHub Sponsors](https://github.com/sponsors/OpenXRay), [Patreon](https://patreon.com/openxray), [Open Collective](https://opencollective.com/openxray) — эти средства идут на поддержку разработчиков за пределами России, а также на оплату сервисов (GitHub, AppVeyor и т.д.).
- BTC: 363ZUoWcQe9fDvRPK9Kee2YuPdyhSFQpr2
- ETH: 0x45a4fe8566e76946591e1eeabf190aa09b1cdb66
- TRX: TGx7QAhTPsRcwnb4mwCtNDU7NF6kuoACpt
- Если хотите использовать другую криптовалюту, напишите @xottab_duty в [нашем Discord](discord.gg/sjRMQwv).

Спасибо за поддержку!

## Благодарности

- [GSC Game World](https://gsc-game.com/) — за создание S.T.A.L.K.E.R. и поддержку сообщества.
- Loxotron — за публикацию исходников движка.
- [Все участники OpenXRay](https://github.com/OpenXRay/xray-16/graphs/contributors) — за вклад в проект:
  - Первая команда OpenXRay (2014–2017):
    - [nitrocaster](https://github.com/nitrocaster) — основатель проекта.
    - [Kaffeine](https://github.com/Kaffeine) — порт на Linux, рефакторинг, общее улучшение.
    - [CrossVR](https://github.com/CrossVR) (Armada651) — создание OpenGL-рендера, работа над системой сборки, поддержка проекта.
    - [andrew-boyarshin](https://github.com/andrew-boyarshin) — система сборки.
    - [CasualDev242](https://github.com/CasualDev242) (Swartz27) — работа над рендером.
    - [awdavies](https://github.com/awdavies) — сопровождение проекта.
  - Вторая команда OpenXRay (2017–н.в.):
    - [Xottab_DUTY](https://github.com/Xottab-DUTY) — текущий лидер проекта.
    - [intorr](https://github.com/intorr) — качество кода, утечки памяти, оптимизации.
    - [eagleivg](https://github.com/eagleivg) — основная часть работ по Linux-порту.
    - [q4a](https://github.com/q4a) — Linux-порт.
    - [SkyLoader](https://github.com/SkyLoaderr) — улучшения OpenGL-рендера, стабилизация.
    - [qweasdd136963](https://github.com/qweasdd136963) — поддержка [OXR_COC](https://github.com/qweasdd136963/OXR_CoC), новые функции, рефакторинг, исправления.
    - [JohnDoe_71Rus](https://github.com/johndoe71rus) — регулярное тестирование.
    - [Chip_exe](https://github.com/007exe) — Linux-порт, сопровождение AUR-пакета, тестирование.
    - [a1batross](https://github.com/a1batross) — Linux-порт.
    - [The Sin!](https://github.com/FreeZoneMods) — новые возможности, рефакторинг, исправления.
    - [Zegeri](https://github.com/Zegeri) — Linux-порт, качество кода.
    - [drug007](https://github.com/drug007) — Linux-порт.
    - [vTurbine](https://github.com/vTurbine) — мультипоточность рендера, оптимизации.
    - [Zigatun](https://github.com/Zigatun) — порт на ARM.
    - [Masterkatze](https://github.com/Masterkatze) — система сборки, исправления.
    - [Chugunov Roman](https://github.com/ChugunovRoman) — [порт Call of Chernobyl на OpenXRay](https://github.com/ChugunovRoman/xray-16), расширения для модмейкеров.
    - [yohjimane](https://github.com/yohjimane) — новые функции, исправления.
  - Другие участники:
    - [alexgdi](https://github.com/alexgdi) — инфраструктура, внешние зависимости.
    - [Shoker](https://github.com/ShokerStlk) — новые функции, исправления.
    - [Alundaio](https://github.com/revolucas) — полезные улучшения, оптимизации.
    - [NeoAnomaly](https://github.com/NeoAnomaly) — отладочные возможности на Windows.
    - [RainbowZerg](https://github.com/RainbowZerg) — рендер, исправления.
    - [FozeSt](https://github.com/FozeSt) — помощь с исправлениями.
    - [justtails](https://github.com/justtails) (mrnotbadguy) — поддержка геймпадов, исправления.
    - [devnexen](https://github.com/devnexen) — поддержка FreeBSD, переносимость.
    - [vamit611](https://github.com/vamit611) — качество кода.
    - [ZeeWanderer](https://github.com/ZeeWanderer) — система сборки.
    - [GeorgeIvlev](https://github.com/GeorgeIvlev) — система сборки, исправления.
    - [r-a-sattarov](https://github.com/r-a-sattarov) — переносимость, поддержка E2K.
    - [TmLev](https://github.com/TmLev) — качество кода, Docker.
    - [Plotja](https://github.com/Plotja) — новые геймплейные функции, исправления, портирование.
    - [jjdredd](https://github.com/jjdredd) — разнообразные улучшения.
    - [dimhotepus](https://github.com/dimhotepus) — качество кода.
    - [HeapRaid](https://github.com/HeapRaid) — очистка рендера, улучшения.
    - [OPNA2608](https://github.com/OPNA2608) — пакет NixOS, переносимость.
    - [kosumosu](https://github.com/kosumosu) — портируемость, E2K, рендер-фичи.
    - [Graff46](https://github.com/Graff46) — скриптовые возможности.
    - [vertver](https://github.com/vertver) — поддержка macOS.
    - [Lnd-stoL](https://github.com/Lnd-stoL) — поддержка macOS.
    - [GermanAizek](https://github.com/GermanAizek) — качество кода, поиск и исправление ошибок.
    - [dasehak](https://github.com/dasehak) — поддержка FreeBSD, исправления.
    - [Hrust](https://github.com/Hrusteckiy) — UI, поддержка CS/SOC, исправления.
    - [johncurley](https://github.com/johncurley) — EFX, исправления, переносимость.
    - [v2v3v4](https://github.com/v2v3v4) — физика, помощь с движком, демонстрация возможностей форка.
    - [Neloreck](https://github.com/Neloreck) — расширение Lua-скриптинга.
    - [sobkas](https://github.com/sobkas) — качество кода, исправления.
    - [AMS21](https://github.com/AMS21) — CMake, качество кода, инфраструктура.
    - [olefirenque](https://github.com/olefirenque) — многопоточность, оптимизация.
    - [tsmp](https://github.com/tsmp) — производительность, оптимизация.
  - Люди, чья работа использовалась:
    - [Im-Dex](https://github.com/Im-dex/xray-162) — поддержка x64, улучшения движка.
    - [tamlin-mike](https://github.com/tamlin-mike) — система сборки.
    - [Vincent](https://github.com/0xBADEAFFE) — Linux-порт.
    - [abramcumner](https://github.com/abramcumner) — исправления и улучшения.
    - [Morrey](https://github.com/morrey) (nouverbe, [viventaje](https://github.com/viventaje)) — DX12-рендер, поддержка Clear Sky, мод Return to Clear Sky.
    - [avoitishin](https://github.com/avoitishin) — расширение скриптовых возможностей, исправления.
  - Финансовые спонсоры:
    - [nitrocaster](https://github.com/nitrocaster), Lukas Friedrich, Luke Jones, NekoIt, Igor Polyakov,
    - Incognito, PJ, RazDva, astral jellybean, Kirill Reprintsev,
    - [John Curley](https://github.com/johncurley), The ParaziT, [clayne](https://github.com/clayne), [sobkas](https://github.com/sobkas), MANfromMOON,
    - Valevicor, Nac, Midiy, Vadim Balashов, Jacob Arms,
    - CatWMuttonChops, Reed777, Interpreter_, nexusasx10, [Egor Olefirenko](https://github.com/olefirenque),
    - Igor Zharenko, SLF, Dmitriy Terletskiy, Alex Brodskiy, Neizvestniy Chelovek,
    - LinuxNerd, [tyabus](https://github.com/tyabus), [Sevenfortyseven](https://github.com/Sevenfortyseven), 777yur0k, ItzVladik.
- Отдельные проекты и их участники:
  - [Oxygen](https://github.com/xrOxygen) — наши друзья, помогли новыми фичами, оптимизациями и советами.
  - [Shoker Weapon Mod](https://github.com/ShokerStlk/xray-16-SWM) — впервые реализовали 3D прицелы (PiP) и множество улучшений вооружения.
  - [OGSR](https://github.com/OGSR/OGSR-Engine) — большая работа над Shadow of Chernobyl.
  - [Call of Chernobyl](https://github.com/revolucas/CoC-Xray) — полезные функции, исправления, оптимизации.
    - [Anomaly](https://www.moddb.com/mods/stalker-anomaly) — расширение движка, новые возможности, повышение качества.
  - [Lost Alpha](https://www.moddb.com/mods/lost-alpha) — за работу над возрождением ранней концепции игры.
    - Lost Alpha DC — за продолжение развития Lost Alpha.
  - [Living Zone](https://vk.com/projektx) — за расширение границ возможностей движка.
  - [OpenXRay Gunslinger](https://www.moddb.com/mods/openxray-gunslinger) — новые функции и впечатляющий оружейный пакет.
  - [IX-Ray](https://github.com/ixray-team) — за стремление к технологическому прогрессу.
- Компании:
  - [CoderGears](https://www.cppdepend.com) — предоставили бесплатную Pro-лицензию на CppDepend — мощный инструмент для C/C++. \
    [![CppDepend logo](https://www.cppdepend.com/images/cppdependlogo.png)](https://www.cppdepend.com)
  - [PVS-Studio LLC](https://pvs-studio.com/pvs-studio/?utm_source=website&utm_medium=github&utm_campaign=open_source) — предоставили бесплатную лицензию на PVS-Studio, статический анализатор для C, C++, C#, Java.

Если мы используем вашу работу, а вы не упомянуты здесь или в [списке участников](https://github.com/OpenXRay/xray-16/graphs/contributors), напишите нам — мы обязательно добавим вас. Или создайте pull request с пополнением списка :)
