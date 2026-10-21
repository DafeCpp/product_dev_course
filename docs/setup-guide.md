# Setup Guide - Настройка окружения

Этот гайд поможет настроить все необходимые инструменты для курса.

## Содержание
- [Установка базовых инструментов](#установка-базовых-инструментов)
- [Python окружение](#python-окружение)
- [Docker](#docker)
- [PostgreSQL](#postgresql)
- [Git и GitHub](#git-и-github)
- [Редактор кода](#редактор-кода)
- [Опционально: Node.js для фронтенда](#опционально-nodejs-для-фронтенда)
- [Проверка установки](#проверка-установки)

## Установка базовых инструментов

### macOS

```bash
# Установка Homebrew (если еще не установлен)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Установка основных инструментов
brew install python@3.14
brew install node
brew install git
brew install --cask docker
```

### Linux (Ubuntu/Debian)

```bash
# Обновление системы
sudo apt update && sudo apt upgrade -y

# Python 3.14
# В Ubuntu/Debian нужная версия часто отсутствует в стандартных репозиториях.
# Рекомендуемый способ — pyenv (сборка из исходников).
sudo apt install -y \
  build-essential libssl-dev zlib1g-dev libbz2-dev libreadline-dev libsqlite3-dev \
  curl libncursesw5-dev xz-utils tk-dev libxml2-dev libxmlsec1-dev libffi-dev liblzma-dev

curl -fsSL https://pyenv.run | bash

# Добавьте в ~/.bashrc (или ~/.zshrc) и перезапустите shell:
# export PYENV_ROOT="$HOME/.pyenv"
# [[ -d "$PYENV_ROOT/bin" ]] && export PATH="$PYENV_ROOT/bin:$PATH"
# eval "$(pyenv init -)"

# Установка Python 3.14.x и выбор версии в репозитории
pyenv install 3.14.2
cd product_dev_course
pyenv local 3.14.2

# Node.js (LTS)
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash -
sudo apt install nodejs -y

# Docker
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker $USER

# Git
sudo apt install git -y
```

### Windows

1. **Python:** Скачать с [python.org](https://www.python.org/downloads/) (3.14+)
   - Обязательно отметить "Add Python to PATH"
2. **Node.js:** Скачать с [nodejs.org](https://nodejs.org/) (LTS версия)
3. **Docker Desktop:** Скачать с [docker.com](https://www.docker.com/products/docker-desktop/)
4. **Git:** Скачать с [git-scm.com](https://git-scm.com/download/win)

## Python окружение

### Создание виртуального окружения

```bash
# Переход в директорию проекта
cd product_dev_course/course-materials/semester-1/module-1-web-api/week-01

# Создание venv
python3.14 -m venv venv

# Активация (macOS/Linux)
source venv/bin/activate

# Активация (Windows)
venv\Scripts\activate

# Проверка версии
python --version
# Должно быть: Python 3.14.x
```

### Установка базовых зависимостей

```bash
# Обновление pip
pip install --upgrade pip

# Установка основных библиотек
pip install aiohttp aiohttp-cors aiohttp-swagger3
pip install sqlalchemy alembic asyncpg
pip install pydantic python-jose[cryptography] passlib[bcrypt]
pip install pytest pytest-asyncio aiohttp-pytest testsuite
pip install ruff black mypy
pip install redis aioredis celery
pip install structlog prometheus-client

# Сохранение зависимостей
pip freeze > requirements.txt
```

## Docker

### Проверка установки

```bash
# Версия Docker
docker --version
# Должно быть: Docker version 24.x или выше

# Версия Docker Compose
docker compose version
# Должно быть: Docker Compose version v2.x

# Проверка работы
docker run hello-world
```

### Базовый Dockerfile для Python

```dockerfile
FROM python:3.14-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY . .

CMD ["python", "main.py"]
```

### Базовый docker-compose.yml

```yaml
version: '3.8'

services:
  app:
    build: .
    ports:
      - "8000:8000"
    environment:
      - DATABASE_URL=postgresql://user:password@db:5432/mydb
    depends_on:
      - db

  db:
    image: postgres:15-alpine
    environment:
      POSTGRES_USER: user
      POSTGRES_PASSWORD: password
      POSTGRES_DB: mydb
    volumes:
      - postgres_data:/var/lib/postgresql/data
    ports:
      - "5432:5432"

volumes:
  postgres_data:
```

## PostgreSQL

### Локальная установка (опционально)

**macOS:**
```bash
brew install postgresql@15
brew services start postgresql@15
```

**Linux:**
```bash
sudo apt install postgresql postgresql-contrib -y
sudo systemctl start postgresql
sudo systemctl enable postgresql
```

**Рекомендация:** Использовать Docker для PostgreSQL:

```bash
docker run --name postgres-dev \
  -e POSTGRES_USER=dev \
  -e POSTGRES_PASSWORD=devpassword \
  -e POSTGRES_DB=coursedb \
  -p 5432:5432 \
  -d postgres:15-alpine
```

### Подключение к БД

```bash
# Через psql
psql -h localhost -U dev -d coursedb

# Через pgAdmin (GUI)
# Скачать: https://www.pgadmin.org/download/
```

## Git и GitHub

### Первоначальная настройка

```bash
# Настройка имени и email
git config --global user.name "Ваше Имя"
git config --global user.email "your.email@example.com"

# Настройка редактора по умолчанию
git config --global core.editor "code --wait"  # для VS Code

# Проверка настроек
git config --list
```

### SSH ключ для GitHub

```bash
# Генерация SSH ключа
ssh-keygen -t ed25519 -C "your.email@example.com"

# Добавление ключа в ssh-agent
eval "$(ssh-agent -s)"
ssh-add ~/.ssh/id_ed25519

# Копирование публичного ключа
cat ~/.ssh/id_ed25519.pub
# Скопировать вывод и добавить на GitHub:
# GitHub → Settings → SSH and GPG keys → New SSH key
```

### Клонирование репозитория курса

```bash
git clone git@github.com:your-org/product_dev_course.git
cd product_dev_course
```

## Редактор кода

### VS Code (рекомендуется)

**Установка:**
- Скачать с [code.visualstudio.com](https://code.visualstudio.com/)

**Необходимые расширения:**

```bash
# Установка через командную строку
code --install-extension ms-python.python
code --install-extension ms-python.vscode-pylance
code --install-extension charliermarsh.ruff
code --install-extension ms-vscode.vscode-typescript-next
code --install-extension dbaeumer.vscode-eslint
code --install-extension esbenp.prettier-vscode
code --install-extension ms-azuretools.vscode-docker
code --install-extension eamodio.gitlens
```

**Или установить вручную:**
- Python
- Pylance
- Ruff
- TypeScript
- ESLint
- Prettier
- Docker
- GitLens

**Настройки VS Code (settings.json):**

```json
{
  "python.linting.enabled": true,
  "python.linting.ruffEnabled": true,
  "python.formatting.provider": "black",
  "editor.formatOnSave": true,
  "editor.codeActionsOnSave": {
    "source.organizeImports": true
  },
  "[typescript]": {
    "editor.defaultFormatter": "esbenp.prettier-vscode"
  },
  "[python]": {
    "editor.defaultFormatter": "ms-python.black-formatter"
  }
}
```

### PyCharm (альтернатива)

- Professional версия имеет отличную поддержку aiohttp, Docker, БД
- Community версия подходит для Python разработки

## Опционально: Node.js для фронтенда

Если планируете разрабатывать фронтенд часть (необязательно для курса):

### Установка Node.js

```bash
# macOS
brew install node

# Linux
curl -fsSL https://deb.nodesource.com/setup_24.x | sudo -E bash -
sudo apt install nodejs -y

# Windows
# Скачать с nodejs.org
```

### Альтернатива: установка Node.js через nvm (рекомендуется для разработки)

Если хотите легко переключаться между версиями Node (например, под разные проекты), используйте `nvm`.

```bash
# Установка nvm
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash

# Подключение nvm в текущей сессии (без перезапуска терминала)
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"

# Установка и выбор последней LTS-версии Node
nvm install --lts
nvm use --lts
node -v
```

### Быстрая настройка TypeScript проекта

```bash
# Создание React приложения с TypeScript
npx create-react-app my-frontend --template typescript

# Или Vite для более быстрой разработки
npm create vite@latest my-frontend -- --template react-ts
```

**Примечание:** Фронтенд часть не является обязательной для курса, но может быть полезна для понимания полной картины взаимодействия клиент-сервер.

## Проверка установки

### Скрипт проверки всех инструментов

Создайте файл `check_setup.py`:

```python
#!/usr/bin/env python3
import sys
import subprocess
import importlib.util

def check_command(command, version_flag="--version"):
    try:
        result = subprocess.run(
            [command, version_flag],
            capture_output=True,
            text=True,
            timeout=5
        )
        return True, result.stdout.split('\n')[0]
    except Exception as e:
        return False, str(e)

def check_python_package(package):
    return importlib.util.find_spec(package) is not None

print("🔍 Проверка окружения для курса...\n")

# Проверка базовых команд
commands = [
    ("python3", "--version"),
    ("pip", "--version"),
    ("docker", "--version"),
    ("docker", "compose version"),
    ("git", "--version"),
]

for cmd in commands:
    name = cmd[0] if len(cmd) == 2 else f"{cmd[0]} {cmd[1]}"
    success, output = check_command(*cmd)
    status = "✅" if success else "❌"
    print(f"{status} {name}: {output if success else 'НЕ УСТАНОВЛЕНО'}")

# Проверка Python пакетов
print("\n📦 Проверка Python пакетов:")
packages = ["aiohttp", "sqlalchemy", "pytest", "testsuite", "pydantic"]
for pkg in packages:
    installed = check_python_package(pkg)
    status = "✅" if installed else "❌"
    print(f"{status} {pkg}: {'установлен' if installed else 'НЕ УСТАНОВЛЕН'}")

print("\n✨ Проверка завершена!")
```

Запустите:
```bash
python3 check_setup.py
```

### Тестовый проект

Создайте простое aiohttp приложение для проверки:

```python
# test_app.py
from aiohttp import web

async def read_root(request):
    return web.json_response({"message": "Setup successful!"})

async def health_check(request):
    return web.json_response({"status": "healthy"})

def create_app():
    app = web.Application()
    app.router.add_get('/', read_root)
    app.router.add_get('/health', health_check)
    return app

if __name__ == '__main__':
    app = create_app()
    web.run_app(app, host='0.0.0.0', port=8000)
```

Запустите:
```bash
python test_app.py
```

Откройте в браузере:
- http://localhost:8000 - должно показать JSON с сообщением
- http://localhost:8000/health - проверка здоровья приложения

## Troubleshooting

### Python не находится

**macOS/Linux:**
```bash
# Добавьте в ~/.bashrc или ~/.zshrc
export PATH="/usr/local/opt/python@3.14/bin:$PATH"
```

**Windows:**
- Переустановите Python с галочкой "Add to PATH"
- Или добавьте вручную в системные переменные

### Docker требует sudo

```bash
# Linux: добавьте пользователя в группу docker
sudo usermod -aG docker $USER
# Перелогиньтесь для применения изменений
```

### Порт уже занят

```bash
# Найти процесс на порту 8000
lsof -i :8000  # macOS/Linux
netstat -ano | findstr :8000  # Windows

# Завершить процесс или использовать другой порт
python main.py  # измените порт в коде
```

### Проблемы с PostgreSQL в Docker

```bash
# Остановить и удалить контейнер
docker stop postgres-dev
docker rm postgres-dev

# Очистить volume
docker volume rm postgres_data

# Запустить заново
docker run --name postgres-dev ...
```

### aiohttp не запускается

```bash
# Убедитесь что установлены все зависимости
pip install aiohttp aiohttp-cors

# Проверьте версию Python (должна быть 3.14+)
python --version
```

## Следующие шаги

После настройки окружения:

1. Прочитайте [Code Style Guide](code-style-guide.md)
2. Ознакомьтесь с [PR Guidelines](pr-guidelines.md)
3. Поднимите стек по [локальной разработке с Docker](local-dev-docker-setup.md): миграции БД применяются init-контейнерами `*-migrate`, а не при старте сервисов
4. Перейдите к [Неделе 1](../course-materials/semester-1/module-1-web-api/week-01/README.md)

## Полезные ссылки

- [aiohttp Documentation](https://docs.aiohttp.org/)
- [Docker Getting Started](https://docs.docker.com/get-started/)
- [PostgreSQL Tutorial](https://www.postgresqltutorial.com/)
- [SQLAlchemy Tutorial](https://docs.sqlalchemy.org/en/20/tutorial/)
- [pytest Documentation](https://docs.pytest.org/)
- [testsuite Documentation](https://github.com/yandex/yandex-taxi-testsuite)

---

Если у вас возникли проблемы, создайте Issue в репозитории курса с тегом `setup-help`.

