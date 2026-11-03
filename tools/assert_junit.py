#!/usr/bin/env python3
"""Fail CI if a required suite was skipped, absent, or failed."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
p=argparse.ArgumentParser();p.add_argument('xml',type=Path);p.add_argument('expected',nargs='+');args=p.parse_args()
cases=ET.parse(args.xml).findall('.//testcase');by_name={case.get('name'):case for case in cases}
assert len(by_name)==len(cases),'duplicate test names'
assert set(by_name)==set(args.expected),(set(by_name),set(args.expected))
for name,case in by_name.items():
    assert case.find('skipped') is None and case.get('status')!='notrun',f'{name} was skipped, not passed'
    assert case.find('failure') is None and case.find('error') is None,f'{name} failed'
print('PASS required suites executed without skips:',', '.join(sorted(by_name)))
