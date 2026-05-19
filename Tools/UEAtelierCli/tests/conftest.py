from __future__ import annotations

import sys
from pathlib import Path

import pytest
from click.testing import CliRunner

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
if str(PACKAGE_ROOT) not in sys.path:
    sys.path.insert(0, str(PACKAGE_ROOT))


@pytest.fixture
def runner() -> CliRunner:
    return CliRunner()
