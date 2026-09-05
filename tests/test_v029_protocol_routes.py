#!/usr/bin/env python3
"""Compatibility wrapper: v0.29 route gate superseded by v0.30 class gate."""
import runpy
from pathlib import Path
runpy.run_path(str(Path(__file__).with_name('test_v030_hardware_class_fixes.py')), run_name='__main__')
