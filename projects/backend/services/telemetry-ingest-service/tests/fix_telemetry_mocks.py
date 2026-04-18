#!/usr/bin/env python3
"""Fix telemetry service test mocks"""
import re

FILE_PATH = '/home/lostpointer/product_dev_course/projects/backend/services/telemetry-ingest-service/tests/test_telemetry_service.py'

with open(FILE_PATH, 'r') as f:
    content = f.read()

# Fix mock_pool = AsyncMock() -> MagicMock()
content = re.sub(r'mock_pool = AsyncMock\(\)', 'mock_pool = MagicMock()', content)

# Fix old pattern: mock_pool.acquire.return_value.__aenter__.return_value = mock_conn
# New pattern: mock_cm with __aenter__ and __aexit__
old_pattern = r'mock_pool\.acquire\.return_value\.__aenter__\.return_value = mock_conn'
new_pattern = '''mock_cm = MagicMock()
            mock_cm.__aenter__ = AsyncMock(return_value=mock_conn)
            mock_cm.__aexit__ = AsyncMock(return_value=None)
            mock_pool.acquire.return_value = mock_cm'''
content = re.sub(old_pattern, new_pattern, content)

# Fix AsyncMock transaction -> MagicMock
content = re.sub(
    r'mock_transaction = AsyncMock\(\)\s+mock_transaction\.__aenter__ = AsyncMock',
    'mock_transaction = MagicMock()\n            mock_transaction.__aenter__ = AsyncMock',
    content
)

with open(FILE_PATH, 'w') as f:
    f.write(content)

print("✅ Fixed test_telemetry_service.py mocks")
