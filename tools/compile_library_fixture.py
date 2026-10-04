#!/usr/bin/env python3
"""Compile the checked-in development fixture; never generates substitute data."""
import json
import pathlib
import sys


def compile_fixture(source):
    games = json.loads(source)
    fields = {'id', 'title', 'artwork_source', 'description', 'developer'}
    if not isinstance(games, list) or len(games) != 1000:
        raise ValueError('Expected exactly 1000 fixture records')
    seen = set()
    rows = []
    for game in games:
        if not isinstance(game, dict) or set(game) != fields:
            raise ValueError('Unexpected fixture fields')
        identity = game['id']
        if type(identity) is not int or not 0 < identity <= 0xffffffff or identity in seen:
            raise ValueError('Invalid or duplicate fixture identity')
        seen.add(identity)
        values = [game[key] for key in ('title', 'artwork_source', 'description', 'developer')]
        if any(not isinstance(value, str) or '\0' in value for value in values) or not values[0]:
            raise ValueError('Invalid fixture text')
        # Octal UTF-8 escapes are independent of compiler source encoding and cannot
        # consume a subsequent hexadecimal digit as a variable-length escape.
        quoted = ['"' + ''.join('\\%03o' % byte for byte in value.encode('utf-8')) + '"' for value in values]
        rows.append('{' + str(identity) + ',' + ','.join(quoted) + '}')
    return '#include "library/model.h"\nnamespace playden {\nstd::vector<GameSummary> DeveloperLibraryFixture() { return {\n' + ',\n'.join(rows) + '\n}; }\n}\n'


if __name__ == '__main__':
    try:
        output = compile_fixture(pathlib.Path(sys.argv[1]).read_text())
        pathlib.Path(sys.argv[2]).write_text('#include <vector>\n' + output)
    except (OSError, ValueError) as error:
        sys.exit(str(error))
