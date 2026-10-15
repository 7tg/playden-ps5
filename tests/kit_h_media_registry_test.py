#!/usr/bin/env python3
import importlib.util,unittest
from pathlib import Path
P=Path(__file__).resolve().parents[1]/'tools/kit-h/media_registry.py'
spec=importlib.util.spec_from_file_location('media_registry',P);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
GUID='317df618-5e5a-468a-9f15-d827a9a08162'
class Registry(unittest.TestCase):
 def fixture(self):
  idl='uuid('+GUID+')'
  before='WINE REGISTRY Version 2\n#arch=win64\n\n[Software\\\\Retained]\n"value"="keep"\n\n'
  after=before
  for view,folder in [('', 'system32'),('Wow6432Node\\\\','syswow64')]:
   after+='[Software\\\\Classes\\\\'+view+'CLSID\\\\{'+GUID+'}\\\\InprocServer32] 123\n#time=abcdef\n@="C:\\\\windows\\\\'+folder+'\\\\winegstreamer.dll"\n"ThreadingModel"="Both"\n\n'
  after+='[System\\\\ControlSet001\\\\Enum\\\\DISPLAY\\\\Host]\n"value"="noise"\n'
  return before,after,idl
 def test_both_views_without_host_noise(self):
  before,after,idl=self.fixture();delta=m.registration_delta(before,after,idl)
  self.assertEqual(len(delta),2)
  merged=m.merge_registry(before,delta)
  self.assertEqual(m.blocks(merged)['Software\\\\Retained'],'"value"="keep"')
  self.assertNotIn('DISPLAY',merged);self.assertIn('#arch=win64',merged)
  with self.assertRaisesRegex(ValueError,'already has'):m.merge_registry(merged,delta)
 def test_missing_architecture_rejected(self):
  before,after,idl=self.fixture()
  with self.assertRaisesRegex(ValueError,'missing or invalid'):m.registration_delta(before,after.replace('Wow6432Node','Other'),idl)
 def test_host_path_rejected(self):
  before,after,idl=self.fixture()
  with self.assertRaisesRegex(ValueError,'host path'):m.registration_delta(before,after.replace('C:\\\\windows\\\\system32','Z:\\\\home\\\\builder'),idl)
if __name__=='__main__':unittest.main()
