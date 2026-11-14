import struct,unittest
from hfs_catalog_resource import parse_leaf,pack_leaf,rename_resource

class ResourceRename(unittest.TestCase):
    def setUp(self):
        header=bytearray(4096);header[8]=255;header[9]=1
        struct.pack_into('>H',header,10,3)
        self.data=bytearray(248);struct.pack_into('>H',self.data,0,2);struct.pack_into('>I',self.data,8,90)
        self.old='DefaultModuleSettings~iphone.plist';self.new='DefaultModuleSettings.plist'
        self.files=pack_leaf(header,[((80,'ControlCenterUI.loctable'),bytes(self.data)),((80,self.old),bytes(self.data)),((80,'Info.plist'),bytes(self.data))])
        thread=struct.pack('>HHIH',4,0,80,len(self.old))+self.old.encode('utf-16-be')
        self.threads=pack_leaf(header,[((89,''),thread),((90,''),thread),((91,''),thread)])
    def call(self,**kw):
        args=dict(file_node=self.files,file_index=1,thread_node=self.threads,thread_index=1,parent=80,old_name=self.old,new_name=self.new)
        args.update(kw);return rename_resource(**args)
    def test_only_target_key_and_thread_name_change(self):
        f,t=self.call();a,b=parse_leaf(self.files),parse_leaf(f);x,y=parse_leaf(self.threads),parse_leaf(t)
        self.assertEqual(b[1],((80,self.new),bytes(self.data)))
        self.assertEqual(a[:1]+a[2:],b[:1]+b[2:]);self.assertEqual(x[:1]+x[2:],y[:1]+y[2:])
        self.assertEqual(y[1][1][10:].decode('utf-16-be'),self.new)
        self.assertEqual(f[:14],self.files[:14]);self.assertEqual(t[:14],self.threads[:14])
    def test_refuses_ancestor_change_move_duplicate_or_growth(self):
        for kwargs in [dict(file_index=0),dict(parent=81),dict(new_name='Info.plist'),dict(new_name='A.plist'),dict(new_name='Z'*40),dict(new_name='../file')]:
            with self.subTest(kwargs=kwargs),self.assertRaises(AssertionError):self.call(**kwargs)
    def test_refuses_wrong_or_incomplete_thread(self):
        for index in [0,2]:
            with self.assertRaises(AssertionError):self.call(thread_index=index)
        broken=bytearray(self.threads);broken[8]=0
        with self.assertRaises(AssertionError):self.call(thread_node=bytes(broken))
    def test_both_name_encodings_and_file_fork_preserved(self):
        f,t=self.call(case_sensitive=True)
        self.assertNotIn(self.old.encode('utf-16-be'),f)
        self.assertEqual(parse_leaf(f)[1][1],parse_leaf(self.files)[1][1])
        self.assertEqual(parse_leaf(t)[1][0],(90,''))

if __name__=='__main__':unittest.main()
