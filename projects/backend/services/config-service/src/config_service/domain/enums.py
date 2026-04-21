"""Domain enumerations."""
from __future__ import annotations

from enum import Enum


class ConfigType(str, Enum):
    feature_flag = "feature_flag"
    qos = "qos"
