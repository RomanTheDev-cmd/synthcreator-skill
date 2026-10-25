# 2026-10-22 — (Codex) Независимая проверка AU до исправления

## Выявленные проблемы

- **Подтверждённый дефект PowerBass.** Source/PluginProcessor.cpp, prepareToPlay не сообщает latency. Сразу после AudioUnitInitialize свойство AU Latency равно 0; после AudioUnitRender — 4/6/6 отсчётов при oversampling 2x/4x/8x, на 44,1/48/96 кГц. Хост может первично настроить компенсацию по неверному числу. Минимальное исправление: выбрать текущий oversampling и сообщить его задержку при prepare, сохранив обновление при смене. Проверка: wrapper RED и native AU query до/после render. Исправляет основной агент.
- **PowerKick: устаревшее сообщение хосту.** Source/PluginProcessor.cpp вызывает setLatencySamples только при prepare; engine переключает activeOs в renderChunk. Native AU всегда сообщает 6 отсчётов при 2x/4x/8x. Основной агент независимо подтвердил RED: engine при 2x сообщает 4, wrapper — 6. Минимальное исправление: актуализировать latency после выбора режима и при prepare. Проверка: wrapper и установленный AU должны сообщать текущую задержку движка.
- **Незначительная точность default параметров PowerBass.** auval предупреждает для Attack (740224584), FM Decay (1370062643), Filter Env Decay (2126032876). Отдельный AU roundtrip показал различие advertised default и set/get на один float ULP: −2,98023223876953125e−8, +2,98023223876953125e−8 и +1,490116119384765625e−8 нормализованной шкалы. Загруженные значения совпадают с roundtrip. Крупное расхождение default/preset этим не подтверждено. Не следует менять звучание/пресеты ради этих предупреждений.

## Выполненные запуски

- auval strict + stress 20 секунд: оба установленных AU завершили проверку с кодом 0 и AU VALIDATION SUCCEEDED. PowerKick без предупреждений, PowerBass с тремя выше.
- Существующие dev wrapper executables: PowerKick PASS 50/50, PowerBass PASS 46/46, коды 0. Хеши бинарников сохранены. Это shared-code dev-тесты, отдельно от установленного AU.
- Пользовательские Documents/PowerKick/Presets и Documents/PowerBass/Presets до/после wrapper-процессов совпадают по составу и SHA-256. Установленные AU/VST3 также совпадают до/после read-only проверки.
- Native AU harness без аудиоустройства: AudioComponentInstanceNew, MusicDeviceMIDIEvent, AudioUnitRender. Частоты 44,1/48/96 кГц; блоки 16/64/128/512/1024; oversampling 2x/4x/8x; C2 velocity 100/127 и 72/127, note-off и повторная атака. После секунды офлайн-тишины **90/90 сравнений waveform совпадают побайтно**: 72 сравнения буферов с 512 и 18 переносов всей MIDI-партии на 17 отсчётов. NaN/Inf не обнаружено, пик в этих default-сценариях <0,79.
- au_parameters.cpp независимо прочитал ParameterInfo и проверил roundtrip default на отдельном экземпляре. Presets не сохранялись.

## Контроль ложного замечания

Холодный PowerKick точно переносил onset на 17 отсчётов, но выровненные waveform различались максимум на 0,0037–0,0045. Это не доказательство ошибки MIDI: startup EQ устанавливает коэффициенты от начала processing. Контроль после 1 секунды тишины дал побайтное совпадение на всех частотах/OS. Исходный отрицательный результат сохранён в au-probe-before.jsonl, уточнённые — *warmed*. Конкретный вклад каждого фильтра отдельно не измерялся; объяснение основано на коде и контрольном эксперименте.

## Не проверено и границы

Запрос AU Latency доказывает сообщаемое хосту число, но не абсолютную задержку аудиокарты или групповую задержку фильтров. Onset синтезатора не обязан совпадать с PDC: ранние отсчёты минимально-фазового IIR возможны до средней задержки.

Звук на слух, live MIDI/controller, CoreAudio round-trip, реальные deadline и CPU в открытой DAW не проверены. Не открывались аудиоустройство, Logic и текущие DAW-проекты; звук вслух не воспроизводился. В native AU проверен default, а не все заводские пресеты. VST3 через внешний host и настоящая Windows не проверены.

pluginval не найден в PATH и стандартных /Applications, ~/Applications, /opt/homebrew/bin, /usr/local/bin. Официальный проверенный релиз: https://github.com/Tracktion/pluginval/releases/tag/v1.0.4 . Не скачивался/не запускался. Можно позднее использовать официальный бинарник из изолированной рабочей папки.

## Повторение после установки исправленных сборок

Исходник au_probe.cpp находится рядом с отчётом. Компиляция:

    xcrun clang++ -std=c++17 -O2 au_probe.cpp -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -o au_probe

Холодный запуск:

    ./au_probe

Установившийся режим (повторить AU_PROBE_OS=0,1,2 для 2x/4x/8x):

    AU_PROBE_WARMUP=1 AU_PROBE_OS=0 ./au_probe

Код 0 означает прохождение сравнений waveform; reported latency следует отдельно сверять с expected engine. stdout/stderr, exit JSON и installed-before/after manifest находятся рядом. Эти результаты относятся к конкретным SHA-256 до исправлений, а не к последующим сборкам.
