#!/usr/bin/env python3
"""Benchmark and verify a 500-line native-style bracketed paste."""
import importlib.util
import json
import os
from pathlib import Path
import select
import struct
import sys
import tempfile
import time


BINARY = str(Path(sys.argv[1]).resolve())
spec = importlib.util.spec_from_file_location("pty_tests", Path(__file__).with_name("vim_pty_test.py"))
saved = sys.argv[:]
sys.argv = [sys.argv[0], BINARY]
pty_tests = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pty_tests)
sys.argv = saved
KEY = chr(0x10FFFD)


def payload():
    lines = (
        '2026-11-11 12:00:30.755 [TID: N/A] [main] INFO  c.d.m.m.v.l.ViewInitialListener -主视图注册登记成功',
        '2026-11-11 12:01:11.204 [TID: N/A] [http-nio-10031-exec-2] INFO  c.d.m.m.o.r.ObjectDataQueryController -===> 对象数据查询接口入参：[{"currentPage":1,"pageSize":1}]',
        'org.mybatis.spring.MyBatisSystemException: ### Error querying database. Cause: java.lang.RuntimeException: no exist datasource : gttvwc',
        '        at org.springframework.aop.framework.ReflectiveMethodInvocation.proceed(ReflectiveMethodInvocation.java:184)',
        '        at java.base/java.lang.Thread.run(Thread.java:1583)',
    )
    return ('\n'.join(lines[n % len(lines)] for n in range(500)) + '\n').encode()


def receive(editor, response, buffer, identifier, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        while b'\n' in buffer:
            line, _, tail = buffer.partition(b'\n')
            buffer[:] = tail
            message = json.loads(line)
            if message['id'] == identifier:
                return message
        ready, _, _ = select.select([response, editor.fd], [], [], 0.05)
        if editor.fd in ready:
            editor.drain(0.01)
        if response in ready:
            data = os.read(response, 65536)
            if not data:
                break
            buffer.extend(data)
    raise AssertionError(f'missing GUI response {identifier}')


def send(editor, data, timeout=10):
    if isinstance(data, str):
        data = data.encode()
    deadline = time.monotonic() + timeout
    while data:
        readable, writable, _ = select.select([editor.fd], [editor.fd], [], 0.1)
        if readable:
            editor.drain(0.01)
        if writable:
            try:
                written = os.write(editor.fd, data[:512])
                data = data[written:]
            except BlockingIOError:
                pass
        if time.monotonic() > deadline:
            raise AssertionError('timed out sending paste input')
    editor.drain()


def main():
    directory = Path(tempfile.mkdtemp(prefix='mijoe-paste-performance-'))
    read_request, request = os.pipe()
    response, write_response = os.pipe()
    os.set_inheritable(read_request, True)
    os.set_inheritable(write_response, True)
    editor = pty_tests.Editor(directory, b'',
                              extra_env={'MIJOE_GUI_REQUEST_FD': str(read_request),
                                         'MIJOE_GUI_RESPONSE_FD': str(write_response)})
    os.close(read_request)
    os.close(write_response)
    os.set_blocking(response, False)
    response_buffer = bytearray()
    identifier = 0

    def call(operation, timeout=20):
        nonlocal identifier
        identifier += 1
        os.write(request, struct.pack('!III', 0, identifier, operation))
        send(editor, KEY, timeout)
        return receive(editor, response, response_buffer, identifier, timeout)

    try:
        data = payload()
        send(editor, b'i')
        before_output = len(editor.output)
        started = time.monotonic()
        send(editor, b'\x1b[200~' + data + b'\x1b[201~')
        state = call(1, timeout=120)
        elapsed = time.monotonic() - started
        output_bytes = len(editor.output) - before_output
        assert elapsed < 5.0, f'500-line paste took {elapsed:.3f}s'
        assert output_bytes < 1024 * 1024, f'paste emitted {output_bytes} terminal bytes'
        assert state['mode'] == 'I', state
        assert state['changed'], state
        assert call(2, timeout=20)['ok']
        assert editor.path.read_bytes() == data
        assert call(6, timeout=20)['ok']
        undo_state = call(2, timeout=20)
        assert undo_state['ok'] and undo_state['mode'] == 'I', undo_state
        assert editor.path.read_bytes() == b''
        print(json.dumps({'lines': 500, 'input_bytes': len(data),
                          'elapsed_ms': round(elapsed * 1000, 3),
                          'terminal_output_bytes': output_bytes,
                          'content_exact': True, 'single_undo': True,
                          'mode_after_paste': state['mode'],
                          'mode_after_undo': undo_state['mode']}, ensure_ascii=False))
    finally:
        if editor.status is None:
            try:
                call(9, timeout=5)
            except Exception:
                pass
        editor.close()
        os.close(request)
        os.close(response)


if __name__ == '__main__':
    main()
