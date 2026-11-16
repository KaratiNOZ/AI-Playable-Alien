"""Manage this repository's releases using Git's existing GitHub credentials.

Credentials stay in memory and are sent only to GitHub's API. Never print them.
"""
import argparse
import json
from pathlib import Path
import subprocess
import urllib.request
import urllib.error

REPO = 'KaratiNOZ/AI-Playable-Alien'
API = 'https://api.github.com/repos/' + REPO

def credential():
    p = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\n\n',
                       text=True, capture_output=True, check=True)
    fields = dict(line.split('=', 1) for line in p.stdout.splitlines() if '=' in line)
    token = fields.get('password')
    if not token:
        raise SystemExit('Git did not supply a GitHub credential')
    return token

def request(url, token=None, payload=None, binary=None):
    headers = {'Accept': 'application/vnd.github+json', 'User-Agent': 'AlienPlayable-release',
               'X-GitHub-Api-Version': '2022-11-28'}
    if token:
        headers['Authorization'] = 'Bearer ' + token
    data = None
    if payload is not None:
        data = json.dumps(payload).encode('utf-8')
        headers['Content-Type'] = 'application/json'
    if binary is not None:
        data = Path(binary).read_bytes()
        headers['Content-Type'] = 'application/zip'
    req = urllib.request.Request(url, data=data, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        raise SystemExit(f'GitHub HTTP {error.code}: {error.read().decode("utf-8", errors="replace")[:1200]}')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['info', 'publish', 'verify'])
    parser.add_argument('--tag')
    parser.add_argument('--commit')
    parser.add_argument('--notes')
    parser.add_argument('--asset')
    parser.add_argument('--title')
    parser.add_argument('--prerelease', action='store_true')
    args = parser.parse_args()
    if args.action == 'info':
        for release in request(API + '/releases?per_page=3'):
            print(json.dumps({k: release[k] for k in ('tag_name', 'name', 'prerelease', 'body', 'html_url')}, ensure_ascii=False))
            print('assets:', [(a['name'], a['size']) for a in release['assets']])
    elif args.action == 'verify':
        r = request(API + '/releases/tags/' + args.tag)
        print(json.dumps({k: r[k] for k in ('tag_name', 'name', 'draft', 'prerelease', 'html_url')}, ensure_ascii=False))
        print('assets:', [(a['name'], a['size'], a.get('digest')) for a in r['assets']])
    else:
        if not all((args.tag, args.commit, args.notes, args.asset, args.title)):
            parser.error('publish requires tag, commit, notes, asset and title')
        token = credential()
        release = request(API + '/releases', token, payload={
            'tag_name': args.tag, 'target_commitish': args.commit, 'name': args.title,
            'body': Path(args.notes).read_text(encoding='utf-8'), 'draft': True, 'prerelease': args.prerelease})
        # Leave a draft on failure; never publish an incomplete release.
        from urllib.parse import quote
        upload = f'https://uploads.github.com/repos/{REPO}/releases/{release["id"]}/assets?name={quote(Path(args.asset).name)}'
        asset = request(upload, token, binary=args.asset)
        if asset['size'] != Path(args.asset).stat().st_size:
            raise SystemExit('Uploaded asset size mismatch; release remains a draft')
        req = urllib.request.Request(API + '/releases/' + str(release['id']),
            data=json.dumps({'draft': False}).encode(), method='PATCH',
            headers={'Authorization': 'Bearer '+token, 'User-Agent': 'AlienPlayable-release',
                     'Content-Type': 'application/json', 'Accept': 'application/vnd.github+json'})
        with urllib.request.urlopen(req, timeout=60) as response:
            print('Published:', json.load(response)['html_url'])

if __name__ == '__main__':
    main()
