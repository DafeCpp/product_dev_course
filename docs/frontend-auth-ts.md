# Техническое задание: Frontend - Интеграция с Auth Service

## 1. Общее описание

**Frontend приложение** — React SPA для платформы отслеживания экспериментов (Experiment Tracking Platform). Приложение интегрируется с Auth Service через Auth Proxy (BFF паттерн) для аутентификации пользователей и защиты маршрутов.

### 1.1. Назначение
- Предоставление пользовательского интерфейса для работы с экспериментами
- Интеграция с системой аутентификации
- Управление сессиями пользователей
- Защита маршрутов от неавторизованного доступа

### 1.2. Технологический стек
- **Язык:** TypeScript
- **Фреймворк:** React 18+
- **Сборщик:** Vite
- **Роутинг:** React Router v7
- **Управление состоянием:** TanStack Query (React Query)
- **HTTP клиент:** Axios
- **Работа с датами:** date-fns
- **Тестирование:** Vitest, Testing Library

## 2. Функциональные требования

### 2.1. Аутентификация

#### 2.1.1. Страница входа

**Маршрут:** `/login`

**Описание:** Страница для входа пользователя в систему.

**Функциональность:**
- Форма входа с полями:
  - `username` (обязательное, текст)
  - `password` (обязательное, пароль)
- Кнопка "Войти"
- Обработка ошибок аутентификации
- Отображение сообщений об ошибках
- Редирект на главную страницу после успешного входа
- Если пользователь уже авторизован — редирект на главную

**Валидация:**
- Username не может быть пустым
- Password не может быть пустым
- Отображение ошибок валидации

**API интеграция:**
```typescript
POST /auth/login
Body: { username: string, password: string }
Response: { user: User, access_token: string, refresh_token: string }
```

**Обработка ошибок:**
- `401 Unauthorized` — "Неверное имя пользователя или пароль"
- `400 Bad Request` — "Неверный формат данных"
- `500 Internal Server Error` — "Ошибка сервера. Попробуйте позже"

#### 2.1.2. Страница регистрации (опционально)

**Маршрут:** `/register`

**Описание:** Страница для регистрации нового пользователя.

**Функциональность:**
- Форма регистрации с полями:
  - `username` (обязательное, 3-50 символов)
  - `email` (обязательное, валидный email)
  - `password` (обязательное, минимум 8 символов)
  - `confirmPassword` (обязательное, должно совпадать с password)
- Кнопка "Зарегистрироваться"
- Валидация на клиенте
- Обработка ошибок (например, пользователь уже существует)
- Редирект на страницу входа после успешной регистрации

**API интеграция:**
```typescript
POST /auth/register
Body: { username: string, email: string, password: string }
Response: { user: User, access_token: string, refresh_token: string }
```

#### 2.1.3. Выход из системы

**Описание:** Функция выхода пользователя из системы.

**Функциональность:**
- Кнопка "Выйти" в Layout/Header
- Очистка токенов (через Auth Proxy)
- Редирект на страницу входа
- Очистка кэша React Query

**API интеграция:**
```typescript
POST /auth/logout
Headers: Authorization: Bearer <token>
Response: { ok: true }
```

### 2.2. Управление токенами

#### 2.2.1. Хранение токенов

**Подход:** HttpOnly cookies через Auth Proxy (BFF паттерн)

**Принцип работы:**
- Токены НЕ хранятся в localStorage/sessionStorage
- Токены хранятся в HttpOnly cookies, устанавливаемых Auth Proxy
- Frontend работает через Auth Proxy (`http://localhost:8080`)
- Все запросы к API идут через Auth Proxy с `withCredentials: true`

#### 2.2.2. Автоматическое обновление токенов

**Описание:** Автоматическое обновление access token при истечении.

**Реализация:**
- Axios interceptor для обработки 401 ошибок
- При получении 401:
  1. Попытка обновить токен через `POST /auth/refresh`
  2. Повтор оригинального запроса
  3. Если refresh не удался — редирект на `/login`

**Код примера:**
```typescript
apiClient.interceptors.response.use(
  (response) => response,
  async (error) => {
    if (error.response?.status === 401 && !error.config._retry) {
      error.config._retry = true
      try {
        await axios.post(`${AUTH_PROXY_URL}/auth/refresh`, {}, { withCredentials: true })
        return apiClient(error.config)
      } catch {
        window.location.href = '/login'
      }
    }
    return Promise.reject(error)
  }
)
```

#### 2.2.3. Проверка авторизации

**Описание:** Проверка статуса авторизации пользователя.

**Реализация:**
- React Query запрос к `GET /auth/me`
- Кэширование результата на 5 минут
- Использование для защиты маршрутов

**API интеграция:**
```typescript
GET /auth/me
Headers: Authorization: Bearer <token>
Response: { id: string, username: string, email: string, password_change_required: boolean }
```

### 2.3. Защита маршрутов

#### 2.3.1. Компонент ProtectedRoute

**Описание:** Компонент-обертка для защиты маршрутов от неавторизованного доступа.

**Функциональность:**
- Проверка авторизации через `GET /auth/me`
- Отображение индикатора загрузки во время проверки
- Редирект на `/login` если пользователь не авторизован
- Рендер дочерних компонентов если пользователь авторизован

**Использование:**
```tsx
<Route path="/experiments" element={
  <ProtectedRoute>
    <ExperimentsPage />
  </ProtectedRoute>
} />
```

#### 2.3.2. Публичные маршруты

**Маршруты без защиты:**
- `/login` — страница входа
- `/register` — страница регистрации (если реализована)

**Маршруты с защитой:**
- `/` — главная страница
- `/experiments` — список экспериментов
- `/experiments/:id` — детали эксперимента
- `/runs/:id` — детали запуска
- `/sensors` — список датчиков
- Все остальные маршруты приложения

### 2.4. Смена пароля

#### 2.4.1. Обязательная смена пароля

**Описание:** Если пользователь имеет флаг `password_change_required: true`, необходимо принудительно запросить смену пароля.

**Функциональность:**
- Проверка флага `password_change_required` после входа
- Если `true`:
  - Отображение предупреждения
  - Редирект на страницу смены пароля
  - Блокировка доступа к основному функционалу
  - Модальное окно или отдельная страница для смены пароля

#### 2.4.2. Страница смены пароля

**Маршрут:** `/change-password`

**Описание:** Страница для смены пароля пользователя.

**Функциональность:**
- Форма с полями:
  - `old_password` (обязательное, текущий пароль)
  - `new_password` (обязательное, минимум 8 символов)
  - `confirm_password` (обязательное, должно совпадать с new_password)
- Валидация на клиенте
- Обработка ошибок
- После успешной смены:
  - Обновление данных пользователя
  - Редирект на главную страницу
  - Снятие блокировки доступа

**API интеграция:**
```typescript
POST /auth/change-password
Headers: Authorization: Bearer <token>
Body: { old_password: string, new_password: string }
Response: { id: string, username: string, email: string, password_change_required: false }
```

### 2.5. Отображение информации о пользователе

#### 2.5.1. Header/Layout

**Описание:** Отображение информации о текущем пользователе в шапке приложения.

**Функциональность:**
- Отображение username или email пользователя
- Кнопка "Выйти"
- Индикатор загрузки во время проверки авторизации
- Скрытие для неавторизованных пользователей

**Реализация:**
- Использование React Query для получения данных пользователя
- Кэширование данных пользователя
- Обновление при изменении состояния авторизации

## 3. API Клиент

### 3.1. Auth API Client

**Файл:** `src/api/auth.ts`

**Функции:**
```typescript
// Вход
login(credentials: LoginRequest): Promise<AuthResponse>

// Обновление токена
refresh(): Promise<AuthResponse>

// Выход
logout(): Promise<void>

// Получение профиля
me(): Promise<User>
```

**Конфигурация:**
- Base URL: `VITE_AUTH_PROXY_URL` (по умолчанию `http://localhost:8080`)
- `withCredentials: true` для работы с cookies
- Content-Type: `application/json`

### 3.2. Main API Client

**Файл:** `src/api/client.ts`

**Описание:** Основной API клиент для работы с Experiment Service и другими сервисами.

**Особенности:**
- Работа через Auth Proxy
- Автоматическое добавление токенов из cookies
- Interceptor для автоматического обновления токенов
- Обработка 401 ошибок
- **Автоматическая генерация и передача `trace_id` и `request_id` в заголовках**

**Конфигурация:**
- Base URL: `VITE_AUTH_PROXY_URL` (через Auth Proxy)
- `withCredentials: true`
- Content-Type: `application/json`
- Заголовки: `X-Trace-Id`, `X-Request-Id` (генерируются автоматически)

## 4. Типы TypeScript

### 4.1. User Types

**Файл:** `src/types/index.ts`

```typescript
interface User {
  id: string
  username: string
  email: string
  password_change_required: boolean
}

interface LoginRequest {
  username: string
  password: string
}

interface RegisterRequest {
  username: string
  email: string
  password: string
}

interface AuthResponse {
  user: User
  access_token?: string  // Может отсутствовать при работе через cookies
  refresh_token?: string // Может отсутствовать при работе через cookies
}

interface PasswordChangeRequest {
  old_password: string
  new_password: string
}
```

## 5. UI/UX Требования

### 5.1. Страница входа

**Дизайн:**
- Центрированная форма
- Поля ввода с labels
- Кнопка входа (primary стиль)
- Ссылка на регистрацию (если реализована)
- Отображение ошибок под формой или полями
- Индикатор загрузки при отправке формы

**Валидация:**
- Валидация в реальном времени
- Подсветка невалидных полей
- Сообщения об ошибках под полями

### 5.2. Защищенные страницы

**Поведение:**
- Показ индикатора загрузки при проверке авторизации
- Плавный переход после авторизации
- Сохранение URL для редиректа после входа (опционально)

### 5.3. Обработка ошибок

**Принципы:**
- Понятные сообщения об ошибках для пользователя
- Логирование ошибок в консоль для разработки
- Не показывать технические детали пользователю
- Предложение действий (например, "Попробуйте еще раз" или "Обратитесь в поддержку")

### 5.4. Индикаторы загрузки

**Где показывать:**
- При отправке форм (кнопка disabled + spinner)
- При проверке авторизации (full-page или inline)
- При загрузке данных (skeleton или spinner)

## 6. Конфигурация

### 6.1. Переменные окружения

**Файл:** `.env`

```env
# URL Auth Proxy (BFF)
VITE_AUTH_PROXY_URL=http://localhost:8080

# URL API (если используется напрямую, без proxy)
VITE_API_URL=http://localhost:8002
```

### 6.2. Vite конфигурация

**Файл:** `vite.config.ts`

**Настройки:**
- Проксирование запросов к `/api/*` на API Gateway (если используется)
- Настройка CORS (если необходимо)
- Настройка dev server

## 7. Структура проекта

```
experiment-portal/
├── src/
│   ├── api/
│   │   ├── auth.ts          # Auth API клиент
│   │   └── client.ts        # Основной API клиент
│   ├── components/
│   │   ├── ProtectedRoute.tsx  # Компонент защиты маршрутов
│   │   ├── Layout.tsx          # Layout с информацией о пользователе
│   │   └── ...
│   ├── pages/
│   │   ├── Login.tsx        # Страница входа
│   │   ├── Register.tsx     # Страница регистрации (опционально)
│   │   ├── ChangePassword.tsx  # Страница смены пароля
│   │   └── ...
│   ├── types/
│   │   └── index.ts         # TypeScript типы
│   ├── hooks/
│   │   └── useAuth.ts       # Хук для работы с аутентификацией (опционально)
│   ├── App.tsx              # Главный компонент с роутингом
│   └── main.tsx             # Точка входа
├── .env                     # Переменные окружения
├── vite.config.ts           # Конфигурация Vite
└── package.json             # Зависимости
```

## 8. Интеграция с Auth Proxy

### 8.1. Принцип работы

**Auth Proxy (BFF)** — промежуточный слой между Frontend и Auth Service:

1. Frontend отправляет запросы на Auth Proxy (`http://localhost:8080`)
2. Auth Proxy проксирует запросы к Auth Service
3. Auth Proxy управляет HttpOnly cookies с токенами
4. Auth Proxy добавляет токены в заголовки для других сервисов

### 8.2. Endpoints через Auth Proxy

**Auth endpoints:**
- `POST /auth/login` — вход (устанавливает cookies)
- `POST /auth/refresh` — обновление токена (использует cookies)
- `POST /auth/logout` — выход (очищает cookies)
- `GET /auth/me` — профиль пользователя (использует cookies)

**API endpoints:**
- Все запросы к Experiment Service идут через Auth Proxy
- Auth Proxy автоматически добавляет токен из cookie в заголовок `Authorization`

### 8.3. Настройка Axios

```typescript
const apiClient = axios.create({
  baseURL: import.meta.env.VITE_AUTH_PROXY_URL || 'http://localhost:8080',
  withCredentials: true, // Важно для работы с cookies
  headers: {
    'Content-Type': 'application/json',
  },
})
```

## 9. Тестирование

### 9.1. Unit тесты

**Компоненты:**
- `ProtectedRoute` — проверка редиректа и рендера
- `Login` — проверка валидации и отправки формы
- `Layout` — проверка отображения пользователя

### 9.2. Integration тесты

**Сценарии:**
- Полный flow входа пользователя
- Проверка защиты маршрутов
- Автоматическое обновление токенов
- Обработка ошибок аутентификации

### 9.3. E2E тесты (опционально)

**Сценарии:**
- Регистрация → Вход → Работа с приложением → Выход
- Вход → Истечение токена → Автоматическое обновление
- Вход с обязательной сменой пароля

## 10. Безопасность

### 10.1. Хранение токенов

✅ **Правильно:** HttpOnly cookies через Auth Proxy
❌ **Неправильно:** localStorage, sessionStorage, обычные cookies

### 10.2. Защита от XSS

- Использование HttpOnly cookies предотвращает доступ JavaScript к токенам
- Санитизация пользовательского ввода
- Использование React (автоматическая защита от XSS)

### 10.3. Защита от CSRF

- Использование SameSite cookies (настраивается на Auth Proxy)
- Проверка origin в заголовках (настраивается на Auth Proxy)

### 10.4. Валидация данных

- Валидация всех форм на клиенте
- Не доверять клиентской валидации — сервер также валидирует
- Использование TypeScript для типобезопасности

## 11. Обработка ошибок

### 11.1. Типы ошибок

**401 Unauthorized:**
- Попытка автоматического обновления токена
- Если не удалось — редирект на `/login`

**403 Forbidden:**
- Отображение сообщения "Доступ запрещен"
- Возможность вернуться на предыдущую страницу

**400 Bad Request:**
- Отображение сообщения об ошибке валидации
- Подсветка невалидных полей

**500 Internal Server Error:**
- Отображение общего сообщения об ошибке
- Предложение повторить попытку позже

### 11.2. Логирование

- Логирование всех ошибок в консоль (development)
- Отправка критичных ошибок в систему мониторинга (production)
- **Обязательное включение trace_id и request_id во все логи**

#### 11.2.1. Trace ID и Request ID

**Требования:**
- Frontend должен генерировать `trace_id` для каждого пользовательского действия (UUID v4)
- `trace_id` передается в заголовке `X-Trace-Id` при всех запросах к API
- `request_id` генерируется для каждого HTTP запроса (UUID v4) и передается в заголовке `X-Request-Id`
- `trace_id` сохраняется в течение всей пользовательской сессии или действия (например, весь flow входа)
- `request_id` уникален для каждого отдельного HTTP запроса

**Реализация:**
- Axios interceptor для автоматической генерации и добавления `trace_id` и `request_id` в заголовки
- Генерация нового `trace_id` при начале новой пользовательской сессии/действия
- Логирование всех ошибок с включением `trace_id` и `request_id`

**Пример конфигурации Axios:**
```typescript
// Генерация trace_id для сессии
const traceId = generateTraceId() // UUID v4

apiClient.interceptors.request.use((config) => {
  // Генерируем request_id для каждого запроса
  const requestId = generateRequestId() // UUID v4
  
  config.headers['X-Trace-Id'] = traceId
  config.headers['X-Request-Id'] = requestId
  
  // Логирование запроса с trace_id и request_id
  console.log({
    trace_id: traceId,
    request_id: requestId,
    method: config.method,
    url: config.url,
  })
  
  return config
})

apiClient.interceptors.response.use(
  (response) => response,
  (error) => {
    // Логирование ошибки с trace_id и request_id
    const traceId = error.config?.headers?.['X-Trace-Id']
    const requestId = error.config?.headers?.['X-Request-Id']
    
    console.error({
      trace_id: traceId,
      request_id: requestId,
      error: error.message,
      status: error.response?.status,
      url: error.config?.url,
    })
    
    return Promise.reject(error)
  }
)
```

**Формат логов:**
Все логи должны включать:
```typescript
{
  timestamp: new Date().toISOString(),
  trace_id: "550e8400-e29b-41d4-a716-446655440000",
  request_id: "660e8400-e29b-41d4-a716-446655440001",
  level: "error",
  message: "API request failed",
  url: "/api/experiments",
  method: "GET",
  status: 500
}
```

## 12. Производительность

### 12.1. Оптимизация

- Кэширование данных пользователя (React Query, 5 минут)
- Lazy loading для страниц
- Code splitting для уменьшения bundle size

### 12.2. Метрики

- Время загрузки страницы входа: < 1 секунда
- Время проверки авторизации: < 500ms
- Плавные переходы между страницами

## 13. Доступность (A11y)

### 13.1. Требования

- Все формы должны иметь labels
- Кнопки должны иметь понятные названия
- Сообщения об ошибках должны быть доступны для screen readers
- Навигация с клавиатуры должна работать
- Достаточный контраст цветов

## 14. Будущие улучшения

- [ ] Страница деталей проекта
- [ ] Управление участниками проекта через UI
- [ ] Двухфакторная аутентификация (2FA)
- [ ] "Запомнить меня" функциональность
- [ ] Восстановление пароля
- [ ] Социальная аутентификация (OAuth)
- [ ] Персонализация (темы, настройки)
- [ ] Уведомления о безопасности (новый вход, смена пароля)
- [ ] История входов
- [ ] Управление активными сессиями

