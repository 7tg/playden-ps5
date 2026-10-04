import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fixture', ROOT / 'tools/compile_library_fixture.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

class FixtureTests(unittest.TestCase):
    def setUp(self):
        self.games = json.loads((ROOT / 'fixtures/library.json').read_text())

    def test_compiles_exact_records_without_source_injection(self):
        self.games[0]['title'] = '"; dangerous\\\nÉ'
        output = fixture.compile_fixture(json.dumps(self.games))
        self.assertIn('\\042\\073\\040', output)
        self.assertNotIn('dangerous', output)
        self.assertEqual(output.count('{100'), 1000)

    def test_rejects_duplicate_and_invalid_fixture(self):
        self.games[1]['id'] = self.games[0]['id']
        with self.assertRaises(ValueError):
            fixture.compile_fixture(json.dumps(self.games))
        with self.assertRaises(ValueError):
            fixture.compile_fixture('[]')

    def test_rejects_unexpected_fields(self):
        self.games[0]['installed'] = True
        with self.assertRaises(ValueError):
            fixture.compile_fixture(json.dumps(self.games))

if __name__ == '__main__':
    unittest.main()
