import struct,unittest
from hfs_catalog_resource import parse_leaf,pack_leaf,rename_resource,set_file_logical_size

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

class FileLogicalSize(unittest.TestCase):
    def setUp(self):
        header=bytearray(4096);header[8]=255;header[9]=1
        struct.pack_into('>H',header,10,2)
        data=bytearray(248);struct.pack_into('>H',data,0,2)
        struct.pack_into('>I',data,8,90);struct.pack_into('>H',data,42,0o100755)
        struct.pack_into('>Q',data,88,547008);struct.pack_into('>I',data,100,134)
        data[104:168]=bytes(range(64));data[168:248]=bytes(range(80))
        self.node=pack_leaf(header,[((80,'lib.dylib'),bytes(data)),((80,'other'),bytes(data))])
        # Nonzero unused node bytes must also survive this fixed-field edit.
        b=bytearray(self.node);b[2000:2100]=bytes(range(100));self.node=bytes(b)
    def call(self,**kw):
        args=dict(node=self.node,index=0,parent=80,name='lib.dylib',file_id=90,
                  old_size=547008,new_size=548864,block_size=4096)
        args.update(kw);return set_file_logical_size(**args)
    def test_grow_and_reverse_preserve_all_nonlength_bytes(self):
        changed=self.call()
        self.assertEqual(parse_leaf(changed)[0][1][96:],parse_leaf(self.node)[0][1][96:])
        self.assertEqual(changed[2000:2100],self.node[2000:2100])
        back=self.call(node=changed,old_size=548864,new_size=547008)
        self.assertEqual(back,self.node)
    def test_refuses_allocation_growth_stale_size_wrong_identity(self):
        for kw in [dict(new_size=548865),dict(new_size=0),dict(old_size=547007),dict(file_id=91),dict(name='other'),dict(index=-1),dict(block_size=512)]:
            with self.subTest(kw=kw),self.assertRaises(AssertionError):self.call(**kw)
    def test_refuses_symlink_and_noop(self):
        data=bytearray(parse_leaf(self.node)[0][1]);struct.pack_into('>H',data,42,0o120777)
        node=pack_leaf(self.node,[((80,'lib.dylib'),bytes(data)),parse_leaf(self.node)[1]])
        with self.assertRaises(AssertionError):self.call(node=node)
        with self.assertRaises(AssertionError):self.call(new_size=547008)

if __name__=='__main__':unittest.main()
