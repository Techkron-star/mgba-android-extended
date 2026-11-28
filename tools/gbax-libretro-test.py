# SPDX-License-Identifier: MPL-2.0
"""Drive actual Libretro callbacks and SDK guest across independent sessions."""
import ctypes as C
import pathlib
import struct
import sys
import tempfile

lib = C.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
class Game(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p),
                ("size", C.c_size_t), ("meta", C.c_char_p)]
class Variable(C.Structure):
    _fields_ = [("key", C.c_char_p), ("value", C.c_char_p)]
ENV = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL = C.CFUNCTYPE(None)
INPUT = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
lib.retro_set_environment.argtypes = [ENV]
lib.retro_set_video_refresh.argtypes = [VIDEO]
lib.retro_set_audio_sample_batch.argtypes = [AUDIO]
lib.retro_set_input_poll.argtypes = [POLL]
lib.retro_set_input_state.argtypes = [INPUT]
lib.retro_load_game.argtypes = [C.POINTER(Game)]
lib.retro_load_game.restype = C.c_bool
lib.retro_serialize_size.restype = C.c_size_t
lib.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
lib.retro_serialize.restype = C.c_bool
lib.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
lib.retro_unserialize.restype = C.c_bool

with tempfile.TemporaryDirectory() as tmp:
    save_dir = C.create_string_buffer(tmp.encode())
    options = {b'mgba_gbax_rewind_budget': b'0', b'mgba_gbax_rewind_command': b'idle'}
    @ENV
    def env(cmd, ptr):
        if cmd == 15:
            var = C.cast(ptr, C.POINTER(Variable))
            value = options.get(var.contents.key)
            if value is not None:
                var.contents.value = value
                return True
            return False
        if cmd in (9, 31): # system/save dir
            C.cast(ptr, C.POINTER(C.c_void_p))[0] = C.addressof(save_dir)
            return True
        if cmd == 10: # pixel format
            return True
        return False
    video = VIDEO(lambda *_: None)
    audio = AUDIO(lambda _, frames: frames)
    poll = POLL(lambda: None)
    inp = INPUT(lambda *_: 0)
    lib.retro_set_environment(env)
    lib.retro_set_video_refresh(video)
    lib.retro_set_audio_sample_batch(audio)
    lib.retro_set_input_poll(poll)
    lib.retro_set_input_state(inp)

    def run(path, expected, extended):
        lib.retro_init()
        game = Game(str(pathlib.Path(path).resolve()).encode(), None, 0, None)
        assert lib.retro_load_game(C.byref(game))
        n0 = lib.retro_serialize_size()
        for _ in range(20):
            lib.retro_run()
        n = lib.retro_serialize_size()
        if extended:
            assert n == n0 == 4 + 164 + 0x61000 + 1024*(65536+4)
            buf = C.create_string_buffer(n)
            assert lib.retro_serialize(buf, n)
            assert buf[4:12] == b"GBAXST01"
            status, = struct.unpack_from("<I", buf, 4+164+0x21000)
            assert status == expected, hex(status)
            options[b'mgba_gbax_rewind_budget'] = b'32'
            for _ in range(125):
                lib.retro_run()
            assert lib.retro_serialize(buf, n)
            before = bytes(buf[4+164:4+164+0x61000])
            options[b'mgba_gbax_rewind_command'] = b'step-a'
            lib.retro_run()
            assert lib.retro_serialize(buf, n)
            after = bytes(buf[4+164:4+164+0x61000])
            assert before != after, 'rewind command should change native state'
            options[b'mgba_gbax_rewind_budget'] = b'0'
            options[b'mgba_gbax_rewind_command'] = b'idle'
            assert lib.retro_unserialize(buf, n)
            assert not lib.retro_unserialize(buf, n-1)
            buf[0] = b"\x00"
            buf[1] = b"\x00"
            buf[2] = b"\x00"
            buf[3] = b"\x00"
            assert not lib.retro_unserialize(buf, n)
        else:
            assert 0 < n < 2*1024*1024
            buf = C.create_string_buffer(n)
            assert lib.retro_serialize(buf, n)
        lib.retro_unload_game()
        lib.retro_deinit()

    run(sys.argv[2], 0x600d0001, True)
    files = list(pathlib.Path(tmp).glob("gbax-*.savx.*"))
    assert len(files) == 2
    valid = [f for f in files if f.stat().st_size == 16777252]
    assert valid
    assert valid[0].read_bytes()[-4:] == bytes.fromhex("fecaedfe")
    run(sys.argv[2], 0x600d0002, True)
    run(sys.argv[3], 0, False)
    assert len(list(pathlib.Path(tmp).glob("gbax-*.savx.*"))) == 2
    print("Libretro marked SDK, persistence, fixed state capacity and legacy smoke test passed")
