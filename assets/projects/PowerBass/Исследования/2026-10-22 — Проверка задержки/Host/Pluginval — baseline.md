# 2026-10-22 — (Codex) pluginval baseline установленных AU/VST3

Скачан официальный pluginval v1.0.4 с GitHub Tracktion: https://github.com/Tracktion/pluginval/releases/tag/v1.0.4 . Архив, распакованный .app и provenance JSON лежат в Host/tools. В системные Applications ничего не устанавливалось.

Проверка подписи: Developer ID Application Tracktion Software Corp (YLB5W3GE5T), stapled notarization ticket. spctl --assess --type execute вернул 0: accepted, source=Notarized Developer ID. Gatekeeper, quarantine и настройки безопасности не менялись. Сохранены SHA-256 архива/бинарника, codesign и spctl выводы.

Запуск --help завершился кодом 0; его полный вывод сохранён в tools/pluginval-help.log. Настройки проверки: strictness-level5, random-seed20261022, skip-gui-tests, sample-rates44100/48000/96000, block-sizes16/64/128/512/1024, timeout-ms60000. Отдельный процесс для каждого установленного AU/VST3. Команды, хеш каждого проверенного плагина и временные метки находятся в pluginval-before/summary.json.

**Запуск подтвердил:** PowerKick VST3, PowerKick AU, PowerBass VST3, PowerBass AU — 4/4 завершили проверку SUCCESS с кодом0, по16 групп тестов каждый, суммарно около4,2секунды. Пользовательские файлы Presets до/после совпали по составу и SHA-256. AU дополнительно вызвали auval, exit0.

**Предупреждения и пропуски:**

- У обоих AU: Current program is -1... Is this correct? Это наблюдение program interface, его соответствие внутреннему browser пресетов отдельно не доказано. Пресеты через внутренний browser не обязательно представлены хосту как AU programs.
- Для VST3 пропущен дополнительный Steinberg validator, поскольку путь к нему не задан. Собственные VST3 тесты pluginval выполнены.
- GUI tests явно пропущены. Открытые DAW-проекты, звук вслух, аудиоустройство, пользовательские пресеты и установленные плагины не менялись.
- Уровень5 проверяет совместимость API/состояния/автоматизации в охваченных сценариях; этот PASS не доказывает точность PDC, физический round-trip, отсутствие всех audio glitches или предпочтительность звучания.

Это baseline конкретных старых SHA-256 перед исправлениями. После установки новых сборок требуется новый запуск с новым каталогом результатов. Полные stdout и собственные логи pluginval сохранены, не только последние строки SUCCESS.
