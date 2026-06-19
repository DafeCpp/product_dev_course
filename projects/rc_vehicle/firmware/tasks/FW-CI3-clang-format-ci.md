# FW-CI3 — CI-проверка clang-format для прошивки

**Статус:** [x] Реализовано (PR, 2026-06-12)
**Файлы:** `.github/workflows/firmware-clang-format.yml`

## Описание

Стиль C++ прошивки задан `projects/rc_vehicle/firmware/.clang-format`
(Google-based), но CI его не проверяет — несоответствия накапливаются.

**Цель:** PR с изменениями в `projects/rc_vehicle/firmware/**` автоматически
проверяется на соответствие `.clang-format`.

## Решение

Job `clang-format` в `.github/workflows/firmware-clang-format.yml`:

- Триггер — `pull_request` в `main`/`develop` с paths-фильтром по
  `projects/rc_vehicle/firmware/**` (по конвенциям существующих workflows).
- `pip install clang-format==22.1.5` — версия закреплена, т.к. разные
  мажорные версии форматируют по-разному.
- **Проверяются только строки, изменённые в PR** (`git clang-format --diff`
  от merge-base). Полная проверка дерева невозможна без массового
  переформатирования: на 2026-06-12 106 из 161 файла прошивки имеют
  небольшие (5-10 строк) отклонения от канонического форматирования,
  а единый reformat-коммит конфликтовал бы со всеми открытыми PR.

## Локальная проверка перед PR

```bash
pip install clang-format==22.1.5   # один раз (clang-format + git-clang-format)
git clang-format --diff $(git merge-base origin/develop HEAD) -- projects/rc_vehicle/firmware/
# применить исправления:
git clang-format $(git merge-base origin/develop HEAD) -- projects/rc_vehicle/firmware/
```

## Follow-up (отдельная задача, после разгрузки очереди PR)

- [ ] Массовое переформатирование всего дерева прошивки одним коммитом
      и перевод CI на полную проверку (`clang-format --dry-run -Werror`
      по всем файлам).

## Критерии приёмки

- [x] PR с неотформатированными изменёнными строками падает с диффом в логе
- [x] PR с отформатированными изменениями проходит
- [x] PR без изменений в `projects/rc_vehicle/firmware/**` job не запускает
