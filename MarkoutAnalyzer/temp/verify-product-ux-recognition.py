#!/usr/bin/env python3
"""Verify project skill discovery without creating a thread or invoking a model."""
import json
import os
from pathlib import Path
import select
import subprocess
import time

root = Path(__file__).resolve().parent.parent
assert str(root) == '/Users/lukeh/Documents/workspace/codex/mt-plugins/MarkoutAnalyzer'
expected = root / 'skills/product-design-and-ux/SKILL.md'
stderr_path = root / 'temp/product-ux-discovery-stderr.log'
result_path = root / 'temp/product-ux-discovery-result.json'
buffer = bytearray()

with stderr_path.open('w') as error_log:
    process = subprocess.Popen(
        ['/Applications/ChatGPT.app/Contents/Resources/codex', 'app-server', '--stdio',
         '-c', 'analytics.enabled=false'],
        cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=error_log,
    )

    def send(message):
        process.stdin.write((json.dumps(message) + '\n').encode())
        process.stdin.flush()

    def receive(request_id):
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            while b'\n' in buffer:
                line, _, tail = buffer.partition(b'\n')
                buffer[:] = tail
                if not line.strip():
                    continue
                message = json.loads(line)
                if message.get('id') == request_id:
                    if 'error' in message:
                        raise RuntimeError(str(message['error']))
                    return message['result']
            ready, _, _ = select.select([process.stdout], [], [], max(0, deadline - time.monotonic()))
            if ready:
                chunk = os.read(process.stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError('Codex exited before the discovery response; inspect the local stderr log')
                buffer.extend(chunk)
        raise TimeoutError('Timed out waiting for Codex skill discovery')

    try:
        send({'id': 1, 'method': 'initialize', 'params': {
            'clientInfo': {'name': 'markout-skill-verifier', 'version': '1.0'},
            'capabilities': {'experimentalApi': False},
        }})
        receive(1)
        send({'method': 'initialized', 'params': {}})
        send({'id': 2, 'method': 'skills/list', 'params': {
            'cwds': [str(root), str(root.parent)], 'forceReload': True,
        }})
        response = receive(2)
        results = []
        for entry in response['data']:
            selected = [skill for skill in entry['skills'] if skill['name'] == 'product-design-and-ux']
            results.append({
                'cwd': entry['cwd'],
                'skills': [{key: skill.get(key) for key in ('name', 'path', 'scope', 'enabled')}
                           for skill in selected],
                'errors': entry['errors'],
            })
        project = next(entry for entry in results if Path(entry['cwd']).resolve() == root)
        parent = next(entry for entry in results if Path(entry['cwd']).resolve() == root.parent)
        matching = [skill for skill in project['skills'] if Path(skill['path']).resolve() == expected.resolve()]
        report = {
            'results': results,
            'project_recognized': len(matching) == 1 and matching[0]['enabled'] is True,
            'project_scope': matching[0]['scope'] if matching else None,
            'exposed_to_parent': any(Path(skill['path']).resolve() == expected.resolve() for skill in parent['skills']),
        }
        result_path.write_text(json.dumps(report, ensure_ascii=False, indent=2))
        print(json.dumps(report, ensure_ascii=False, indent=2))
        assert report['project_recognized'], 'Project skill was not recognized and enabled'
        assert report['project_scope'] == 'repo', 'Expected project scope'
        assert not report['exposed_to_parent'], 'Project skill unexpectedly exposed to parent'
        assert not project['errors'], 'Codex reported skill loading errors'
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
