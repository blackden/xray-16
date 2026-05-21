# Encoding fixtures

Canonical phrase used across UTF-8 migration tests:

> прибытие на Скадовск

(an autosave label that crashed the engine on macOS before commit
`16d49d29c` because cp1251 bytes in the filename triggered EILSEQ on APFS).

Files in this directory:

| File | Encoding | Size | Purpose |
|---|---|---|---|
| `phrase.utf8` | UTF-8 | 38 B | ground truth |
| `phrase.cp1251` | windows-1251 | 20 B | rejected by APFS `fopen` pre-fix |
| `phrase.cp1250` | windows-1250 / ASCII fallback | 20 B | Polish-locale variant exercising cp1250 path |
| `phrase.expected.json` | UTF-8 | — | codepoint list + per-byte breakdown |

To regenerate (in repo root):

```bash
PHRASE='прибытие на Скадовск'
printf '%s' "$PHRASE"                              > tests/fixtures/encoding/phrase.utf8
iconv -f UTF-8 -t CP1251 < tests/fixtures/encoding/phrase.utf8 \
                                                   > tests/fixtures/encoding/phrase.cp1251
iconv -f UTF-8 -t CP1250//TRANSLIT < tests/fixtures/encoding/phrase.utf8 \
                                                   > tests/fixtures/encoding/phrase.cp1250
```

Both `.cp1251` and `.cp1250` outputs end up the same size (20 B) because
the cyrillic chars map 1-to-1 in cp1251 and TRANSLIT into ASCII
approximations in cp1250 (since cp1250 is Central European, not Cyrillic).
