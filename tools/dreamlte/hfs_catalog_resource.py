"""Bounded offline rename of an existing ASCII HFS+ resource, without allocation.

Only a non-first file leaf record and its existing file-thread payload change.
The caller must back up both nodes, guard device ranges, and read them back.
This module never opens or writes a device. It refuses moves, growth, index-key
changes, non-ASCII neighbors, incomplete threads, and unexpected record types.
"""
import struct

def u16(data, offset=0):
    return struct.unpack_from('>H', data, offset)[0]

def parse_leaf(node):
    assert len(node) in (4096, 8192, 16384, 32768)
    assert struct.unpack_from('b', node, 8)[0] == -1 and node[9] == 1
    count = u16(node, 10)
    assert 0 < count < len(node) // 16
    offsets = [u16(node, len(node) - 2 * (i + 1)) for i in range(count + 1)]
    assert offsets[0] == 14 and offsets[-1] <= len(node) - 2 * (count + 1)
    assert all(a < b and a % 2 == 0 for a, b in zip(offsets, offsets[1:]))
    records = []
    for a, b in zip(offsets, offsets[1:]):
        length = u16(node, a)
        chars = u16(node, a + 6)
        assert length == 6 + 2 * chars and a + length + 2 <= b
        parent = struct.unpack_from('>I', node, a + 2)[0]
        name = node[a + 8:a + 8 + chars * 2].decode('utf-16-be')
        records.append(((parent, name), node[a + 2 + length:b]))
    return records

def pack_leaf(original, records):
    assert len(records) == u16(original, 10)
    node = bytearray(len(original))
    node[:14] = original[:14]
    at = 14
    for i, ((parent, name), data) in enumerate(records):
        encoded = name.encode('utf-16-be')
        assert len(encoded) <= 510 and len(data) % 2 == 0
        record = struct.pack('>HIH', 6 + len(encoded), parent, len(encoded) // 2) + encoded + data
        assert at + len(record) <= len(node) - 2 * (len(records) + 1)
        struct.pack_into('>H', node, len(node) - 2 * (i + 1), at)
        node[at:at + len(record)] = record
        at += len(record)
    struct.pack_into('>H', node, len(node) - 2 * (len(records) + 1), at)
    result = bytes(node)
    assert parse_leaf(result) == records
    return result

def rename_resource(file_node, file_index, thread_node, thread_index, parent,
                    old_name, new_name, case_sensitive=False):
    assert old_name != new_name and old_name.isascii() and new_name.isascii()
    assert 0 < len(new_name) <= len(old_name) <= 255
    assert not any(c in old_name + new_name for c in '/:\x00')
    assert file_node != thread_node
    files, threads = parse_leaf(file_node), parse_leaf(thread_node)
    assert 0 < file_index < len(files) and 0 <= thread_index < len(threads)
    key, data = files[file_index]
    assert key == (parent, old_name) and len(data) == 248 and u16(data) == 2
    file_id = struct.unpack_from('>I', data, 8)[0]
    new_key = (parent, new_name)
    def compare(k):
        assert k[1].isascii()
        return (k[0], k[1] if case_sensitive else k[1].casefold())
    assert compare(files[file_index - 1][0]) < compare(new_key)
    if file_index + 1 < len(files):
        assert compare(new_key) < compare(files[file_index + 1][0])
    thread_key, thread = threads[thread_index]
    assert thread_key == (file_id, '') and u16(thread) == 4
    assert struct.unpack_from('>I', thread, 4)[0] == parent
    assert len(thread) == 10 + 2 * u16(thread, 8)
    assert thread[10:].decode('utf-16-be') == old_name
    encoded = new_name.encode('utf-16-be')
    files[file_index] = (new_key, data)
    threads[thread_index] = (thread_key, thread[:8] + struct.pack('>H', len(new_name)) + encoded)
    new_file, new_thread = pack_leaf(file_node, files), pack_leaf(thread_node, threads)
    # Both node minimum keys are unchanged: no ancestor/index update is needed.
    assert parse_leaf(new_file)[0][0] == parse_leaf(file_node)[0][0]
    assert parse_leaf(new_thread)[0][0] == parse_leaf(thread_node)[0][0]
    return new_file, new_thread
