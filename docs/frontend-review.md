# Frontend Code Review — Experiment Portal

Дата: 2026-03-07
Ветка: develop

---

## 1. Незавершённая миграция цветов на MD3 [КРИТИЧНО]

После смены палитры на Material Design 3 (purple baseline) в файлах токенов
(`colors.scss`, `tokens.scss`, `effects.scss`, `index.scss`, `components.scss`),
в компонентных `.scss` осталось ~80 жёстко зашитых цветов старой teal/amber схемы.

### Маппинг замен

| Старый цвет (rgba) | MD3-аналог (rgba) | Семантика |
|---|---|---|
| `rgba(15, 118, 110, *)` | `rgba(103, 80, 164, *)` | primary tint |
| `rgba(16, 34, 58, *)` | `rgba(28, 27, 31, *)` | on-surface / ink |
| `rgba(91, 105, 118, *)` | `rgba(98, 91, 113, *)` | secondary / muted |
| `rgba(217, 119, 6, *)` | `rgba(125, 82, 96, *)` | tertiary accent |
| `rgba(13, 27, 42, *)` | `rgba(28, 27, 31, *)` | shadow / overlay ink |
| `rgba(12, 74, 110, *)` | `rgba(0, 99, 155, *)` | info tint |
| `rgba(11, 35, 52, *)` | `rgba(28, 27, 31, *)` | dark surface |
| `rgba(18, 42, 63, *)` | `rgba(49, 45, 56, *)` | dark surface alt |

### Затронутые файлы

- `src/App.scss` — ~20 вхождений (borders, backgrounds, gradients)
- `src/components/Layout.scss` — 5 вхождений (sidebar overlay, borders, badges)
- `src/components/TelemetryPanel.scss` — 5 вхождений (focus, borders, backgrounds)
- `src/components/AuditLog.scss` — 8 вхождений (borders, backgrounds, focus ring)
- `src/components/RunsList.scss` — 4 вхождения (selected row, borders)
- `src/components/common/MaterialSelect.scss` — 4 вхождения (border, focus, placeholder)
- `src/components/common/FloatingActionButton.scss` — 1 вхождение (shadow)
- `src/pages/RunDetail.scss` — 7 вхождений (mono hints, session card, gradients)
- `src/pages/TelemetryViewer.scss` — 9 вхождений (borders, inputs, drag outline)
- `src/pages/ExperimentsList.scss` — 2 вхождения (border, divider)
- `src/pages/ProjectsList.scss` — 3 вхождения (amber badge, divider, border)
- `src/pages/SensorsList.scss` — 4 вхождения (info bg, scrollbar, divider, border)
- `src/pages/Login.scss` — 1 вхождение (фон-градиент)
- `src/pages/Register.scss` — 1 вхождение (фон-градиент)
- `src/pages/AuthShell.scss` — 3 вхождения (radial gradients, scrollbar)

### Рекомендация

Заменить все hardcoded rgba на CSS-переменные или SCSS-переменные из `colors.scss`.
Где возможно — использовать `var(--border-soft)`, `var(--primary)` и т.д.
вместо inline rgba.

---

## 2. CSS — анти-паттерны

### 2.1 !important в FloatingActionButton.scss [ВЫСОКИЙ]

Файл: `src/components/common/FloatingActionButton.scss`
Строки: 2–26

~20+ деклараций с `!important`. Причина — конфликт специфичности с другими стилями.

**Решение:** Увеличить специфичность через правильную структуру селекторов
(BEM, scoped classes), убрать все `!important`.

### 2.2 Дублирование `.btn-sm` [СРЕДНИЙ]

Определён и в `src/styles/components.scss` (строки 63–67),
и в `src/App.scss` (строки 87–91).

**Решение:** Оставить только в `components.scss`, удалить из `App.scss`.

### 2.3 Глубокие цепочки селекторов в Layout.scss [СРЕДНИЙ]

Файл: `src/components/Layout.scss`, строки 374–483

```scss
.layout.sidebar-collapsed:not(.compact-viewport) .sidebar-brand__eyebrow,
.layout.sidebar-collapsed:not(.compact-viewport) .sidebar-brand__title,
...
```

**Решение:** Упростить через data-атрибуты или короткие классы:
```scss
[data-collapsed] .sidebar-brand__eyebrow { display: none; }
```

### 2.4 Неиспользуемый CSS-класс [НИЗКИЙ]

Файл: `src/pages/ExperimentDetail.scss`, строки 3–5

Класс `.experiment-detail__hero` определён, но не используется в `.tsx`.

**Решение:** Удалить или добавить в разметку, если планировался.

---

## 3. Accessibility

### 3.1 div вместо button [ВЫСОКИЙ]

Файл: `src/pages/SensorsList.tsx`, строки 140–143

```tsx
<div role="button" tabIndex={0} onClick={...} onKeyDown={...}>
```

**Решение:** Заменить на `<button className="sensor-card card">`.

### 3.2 Отсутствуют focus-visible стили [СРЕДНИЙ]

Многие интерактивные элементы (инпуты в AdminUsers, кнопки в формах)
не имеют кастомных стилей для `:focus-visible`.

**Решение:** Добавить глобальный стиль:
```scss
:focus-visible {
  outline: 2px solid var(--primary);
  outline-offset: 2px;
}
```

### 3.3 Контрастность placeholder-текста [НИЗКИЙ]

`rgba(91, 105, 118, 0.76)` на белом фоне может не проходить WCAG AA (4.5:1).

**Решение:** Проверить contrast ratio, при необходимости увеличить непрозрачность.

---

## 4. Производительность

### 4.1 Нет мемоизации строк таблицы [СРЕДНИЙ]

Файл: `src/components/RunsList.tsx`, строки 132–190

Все `<tr>` перерисовываются при любом изменении runs.

**Решение:** Вынести строку в отдельный компонент с `React.memo`:
```tsx
const RunTableRow = React.memo(({ run, selected, onSelect }) => (
  <tr>...</tr>
));
```

### 4.2 TelemetryPanel — полный re-render при стриминге [СРЕДНИЙ]

Файл: `src/components/TelemetryPanel.tsx`, строки 85–92

Обновление `recordsBySensor` (высокочастотное) вызывает re-render
всего компонента, включая заголовок и настройки.

**Решение:** Вынести график в отдельный компонент,
использовать `useTransition()` для низкоприоритетных обновлений.

### 4.3 Нет виртуализации длинных списков [НИЗКИЙ]

Файл: `src/pages/RunDetail.tsx`, строки 562–623

Все сессии рендерятся сразу. При 100+ сессиях это дорого.

**Решение:** Использовать `react-window` или `react-virtualized`.

### 4.4 Пересоздание функций каждый рендер [НИЗКИЙ]

Файл: `src/pages/RunDetail.tsx`, строка 209

`formatDuration` пересоздаётся при каждом рендере.

**Решение:** Обернуть в `useCallback` или вынести за пределы компонента.

---

## 5. Логика и качество кода

### 5.1 IS_TEST скрывает ошибки от пользователей [ВЫСОКИЙ]

Файлы: `src/pages/Login.tsx:96`, `src/pages/Register.tsx:142`

```tsx
{IS_TEST && error && <div className="error-message">{error}</div>}
```

Пользователи в продакшене не видят ошибок авторизации.

**Решение:** Убрать `IS_TEST &&`, всегда показывать ошибки.

### 5.2 Браузерный prompt() [СРЕДНИЙ]

Файл: `src/pages/RunDetail.tsx`, строка 302

```tsx
const reason = prompt('Причина ошибки:')
```

**Решение:** Заменить на модальный диалог (MUI Dialog или кастомный).

### 5.3 Non-null assertion для route params [СРЕДНИЙ]

Файл: `src/pages/ExperimentDetail.tsx`, строка 26

```tsx
const deleteMutation = useMutation({
  mutationFn: () => experimentsApi.delete(id!),
```

**Решение:** Добавить guard `if (!id) return null` перед использованием.

### 5.4 Нет Error Boundary [СРЕДНИЙ]

Ошибка рендера любого компонента роняет всё приложение.

**Решение:** Создать `ErrorBoundary` компонент и обернуть им роуты.

### 5.5 Catch без деталей ошибки [НИЗКИЙ]

Файл: `src/pages/ExperimentsList.tsx`, строки 73–94

```tsx
} catch {
  notifyError(`Ошибка экспорта ${formatType.toUpperCase()}`)
}
```

**Решение:** Логировать ошибку и показывать детали:
```tsx
} catch (err) {
  console.error('Export failed:', err);
  notifyError(`Ошибка экспорта: ${err instanceof Error ? err.message : 'неизвестная ошибка'}`);
}
```

---

## 6. Консистентность

### 6.1 Разные форматы дат [СРЕДНИЙ]

- `ExperimentsList.tsx`: `'dd MMM yyyy HH:mm'`
- `AdminUsers.tsx`: `'dd.MM.yyyy HH:mm'`

**Решение:** Создать `utils/dateFormat.ts` с единым набором форматов.

### 6.2 Разная обработка ошибок на страницах [СРЕДНИЙ]

Login/Register — IS_TEST gate, SensorsList — свой паттерн, AdminUsers — ещё один.

**Решение:** Единый компонент отображения ошибок или toast-система.

### 6.3 Дублирование строковых литералов [НИЗКИЙ]

"Загрузка...", "Ошибка", названия статусов повторяются.

**Решение:** `constants/messages.ts` с централизованными строками.

---

## Порядок реализации

### Фаза 1 — Высокий приоритет
1. [ ] Заменить ~80 hardcoded цветов на MD3 переменные во всех SCSS
2. [ ] Убрать `IS_TEST` gate с ошибок в Login/Register
3. [ ] Убрать `!important` из FloatingActionButton.scss

### Фаза 2 — Средний приоритет
4. [ ] Мемоизация RunsList (React.memo для строк)
5. [ ] Вынести график TelemetryPanel в отдельный компонент
6. [ ] Заменить div[role=button] на <button> в SensorsList
7. [ ] Заменить prompt() на модальный диалог в RunDetail
8. [ ] Добавить ErrorBoundary
9. [ ] Guard для route params (убрать non-null assertions)
10. [ ] Удалить дублирование .btn-sm
11. [ ] Унифицировать формат дат
12. [ ] Добавить :focus-visible стили

### Фаза 3 — Низкий приоритет
13. [ ] Виртуализация длинных списков (react-window)
14. [ ] useCallback для formatDuration и аналогичных
15. [ ] Централизация строковых литералов
16. [ ] Удалить неиспользуемый CSS (.experiment-detail__hero)
17. [ ] Упростить селекторы в Layout.scss
18. [ ] Проверка контрастности (WCAG AA)
19. [ ] Детализация catch-блоков
