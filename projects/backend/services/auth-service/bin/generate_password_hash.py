#!/usr/bin/env python3
"""Generate bcrypt password hash."""
import sys
import bcrypt

if len(sys.argv) < 2:
    print("Usage: python generate_password_hash.py <password>")
    sys.exit(1)

password = sys.argv[1]
hashed = bcrypt.hashpw(password.encode('utf-8'), bcrypt.gensalt(rounds=12))
print(hashed.decode('utf-8'))

