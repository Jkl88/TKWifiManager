# ESPConnect (русская адаптация)

Проект перенесен на локальную машину в формате, максимально близком к оригинальному `ESPConnect`, с сохранением дизайна и структуры приложения.

## Что уже сделано
- Исходник проекта склонирован без изменения архитектуры.
- Добавлена русская локаль интерфейса (`ru`) с переводом ключевых элементов UI.
- Русский язык подключен в систему локализации и доступен в переключателе языков.
- Для новых запусков русский выбран языком по умолчанию.
- Добавлен отдельный файл прогресса работ: `PROJECT_PROGRESS.md`.
- Добавлены базовые многопользовательские сценарии:
  - авторизация,
  - регистрация только по одноразовой ссылке от администратора,
  - админ-панель (блокировка, удаление пользователей, создание ссылки регистрации),
  - сохранение BIN-файлов отдельно для каждого пользователя.
- Добавлен backend на `FastAPI` + `PostgreSQL`:
  - `HTTPOnly` cookie-сессии,
  - JWT API-токены для OTA/bin API,
  - хранение токенов и одноразовых ссылок в БД,
  - проверка токенов и инвайтов на валидность и срок действия при каждом запросе.
- Авторизация доведена до production-ориентированного контура:
  - серверное хранилище сессий в БД,
  - refresh token в `HTTPOnly` cookie,
  - автоматическая ротация сессии через backend.

## Стек
- `Vue 3`
- `Vuetify`
- `TypeScript`
- `Vite`
- `Electron` (для desktop-сборки)

## Быстрый старт
```bash
npm install
copy .env.example .env
npm run dev
```

## Backend
```bash
python -m venv .venv
.venv\Scripts\activate
pip install -r backend/requirements.txt
uvicorn backend.app.main:app --reload
```

## Миграции Alembic
```bash
.venv\Scripts\activate
alembic -c backend/alembic.ini upgrade head
```

## Docker Compose Dev
```bash
docker compose -f docker-compose.dev.yml up --build
```

Сервисы dev:
- frontend: `http://localhost:5174`
- backend: `http://localhost:8000`
- postgres: внутри docker-сети (без внешнего проброса порта)

Правило dev:
- `docker-compose.dev.yml` используется только для локальной разработки на `localhost`;
- в `.env` для dev: `FRONTEND_ORIGIN=http://localhost:5174`, `COOKIE_SECURE=false`.

## Docker Compose Prod
```bash
docker compose -f docker-compose.prod.yml up --build
```

`docker-compose.prod.yml` не зашивает домен в код: `FRONTEND_ORIGIN` и `COOKIE_SECURE` берутся из `.env`.

Правило prod:
- `docker-compose.prod.yml` используется только для публичного домена (любой host);
- в `.env` обязательно укажи:
  - `FRONTEND_ORIGIN=https://<твой-домен>` (например `https://example.com`)
  - `COOKIE_SECURE=true` (если доступ по HTTPS);
- frontend в prod публикуется на `127.0.0.1:8081`, внешний доступ организуется reverse proxy (например, Caddy) на домен.

Перед запуском на сервере:
- DNS `A`/`AAAA` для твоего домена на IP машины;
- TLS: сертификат на внешнем reverse proxy (рекомендуется), который проксирует в `127.0.0.1:8081`.

Сервисы prod:
- frontend + reverse proxy: `127.0.0.1:8081` (локальный upstream для внешнего прокси)
- backend: внутри docker-сети
- postgres: внутри docker-сети

## API прошивок (проверка версии и загрузка)

Базовый префикс: `{ORIGIN}/api/firmware` (подставь свой origin, например `https://example.com`). Авторизация: cookie-сессия в браузере или заголовок `Authorization: Bearer <JWT>` (API-токен из личного кабинета, привязанный к проекту).

### Проверка / ссылка на скачивание latest

- **GET** `/api/firmware/latest/download-link`  
  Query: `project_name`, `controller`, опционально `type` (`firmware` | `bootloader` | `partition table` | `fs`, по умолчанию `firmware`).  
  Ответ: JSON с `download_url` (относительный путь) и `firmware_id`. Затем **GET** на этот URL с той же авторизацией — бинарный файл.

- **POST** `/api/firmware/resolve-download`  
  Тело JSON (для JWT с привязкой к проекту): `controller`, опционально `firmware_version` и `firmware_type` / `type`.  
  Возвращает метаданные и `download_url` для выбранной или latest-версии.

### Загрузка файла (multipart)

- **POST** `/api/firmware`  
  Форма (`multipart/form-data`): обязательный `file`; обычно указывают `project_name`, `controller`, `firmware_version`, `firmware_type` (или `type`).  
  Опционально **`latest`**: `true` — после загрузки эта версия помечается как **latest** для пары проект+контроллер; `false` — **не** помечать (в том числе отключает автоматическую пометку latest для версии `0.0.0`); если поле не передано — поведение как раньше (в т.ч. для `0.0.0` версия становится latest по правилам сервера).

Пример проверки ссылки (curl, подставь токен и хост):

```bash
curl -sS -H "Authorization: Bearer YOUR_JWT" \
  "https://example.com/api/firmware/latest/download-link?project_name=MyProject&controller=ESP32-S3&type=firmware"
```

## PostgreSQL
- Нужно создать базу данных `espconnect`.
- Строка подключения задается через `DATABASE_URL` в `.env`.
- Схема создается миграциями `alembic`.

## Полезные команды
```bash
npm run typecheck
npm run test
npm run build
```

## Важно
- Внешний вид и UX сохраняются как в оригинальном проекте.
- Администратор создается из переменных окружения `VITE_ADMIN_LOGIN` и `VITE_ADMIN_PASSWORD`.
- Backend-администратор создается из `ADMIN_LOGIN` и `ADMIN_PASSWORD`.
- Для регистрации обычного пользователя требуется одноразовый токен из админ-панели.
- JWT токены пользователя создаются в личном кабинете и хранятся в PostgreSQL.
- BIN-файлы хранятся на backend, а метаданные о них в PostgreSQL.
- Автосоздание таблиц через `create_all` удалено из runtime-старта backend.
- Web-сессии хранятся на сервере в таблице `user_sessions`.
- Refresh token хранится в `HTTPOnly` cookie и ротируется backend-ом.

## Что проверено вручную
- Вход администратором по cookie-сессии.
- Создание одноразовой ссылки регистрации.
- Регистрация пользователя по инвайту.
- Вход пользователем.
- Ротация refresh token и продолжение работы сессии.
- Создание JWT API токена из кабинета.
- Загрузка BIN файла.
- Получение списка BIN файлов.
- Скачивание BIN файла по API.
