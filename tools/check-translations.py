"""Check configuration UI literals and printf argument preservation."""
import ast
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / "locale/ru.json").read_text(encoding="utf-8"))
literal = re.compile(r'"(?:[^"\\]|\\.)*"')

def strings(text):
    return [''.join(ast.literal_eval(s) for s in literal.findall(m.group()))
            for m in re.finditer(literal.pattern + r'(?:\s*' + literal.pattern + r')*', text)]

def callargs(text, start):
    args, begin, depth, i = [], start, 0, start
    while i < len(text):
        if text[i] == '"':
            match = literal.match(text, i)
            i = match.end()
            continue
        if text[i] == "(": depth += 1
        elif text[i] == ")":
            if depth == 0: return args + [text[begin:i]]
            depth -= 1
        elif text[i] == "," and depth == 0:
            args.append(text[begin:i]); begin = i + 1
        i += 1
    raise ValueError("unbalanced source call")

labels = set()
for filename in ["config.c", "windows/config.c", "windows/kittyng-language.c", "windows/kittyng-proxy-ui.c", "ssh/ca-config.c"]:
    text = re.sub(r'/\*.*?\*/', '', (root / filename).read_text(), flags=re.S)
    for match in re.finditer(r'\b(ctrl_[a-z_]+)\s*\(', text):
        name = match.group(1)
        args = callargs(text, match.end())
        indices = [2] if name == "ctrl_settitle" else [3] if name == "ctrl_getset" else [1]
        if name in ["ctrl_radiobuttons", "ctrl_filesel"]: indices = range(1, len(args))
        if name in ["ctrl_alloc", "ctrl_columns", "ctrl_find_path", "ctrl_path_compare", "ctrl_path_elements"]: continue
        for index in indices:
            if index < len(args): labels.update(strings(args[index]))

# Audit every literal event-log template, including concatenated size formats.
event_labels = set()
for base in ("ssh", "proxy", "windows", "unix", "otherbackends", "utils"):
    for path in (root / base).rglob("*.c"):
        text = re.sub(r'/\*.*?\*/', '', path.read_text(encoding="utf8"), flags=re.S)
        for match in re.finditer(r'\b(ppl_printf|ppl_logevent|bpp_logevent|logeventf|logevent)\s*\(', text):
            args = callargs(text, match.end())
            index = 0 if match.group(1) in ("ppl_printf", "ppl_logevent", "bpp_logevent") else 1
            if len(args) <= index: continue
            expression = args[index].strip()
            for size in ("zu", "Iu"):
                expanded = re.sub(r'\bSIZEu\b', '"'+size+'"', expression)
                expanded = re.sub(r'\bPRIu32\b', '"u"', expanded)
                if re.fullmatch(r'(?:'+literal.pattern+r'\s*)+', expanded):
                    label = ''.join(ast.literal_eval(x) for x in literal.findall(expanded))
                    if re.search(r'[A-Za-z]{3}', label): event_labels.add(label)
# USER is a Telnet protocol field, not translatable prose.
event_labels.discard("    USER=%s")
for path in (root / "windows").glob("*.c"):
    text = path.read_text(encoding="utf8")
    for match in re.finditer(r'kr_messagebox\([^,]+,\s*('+literal.pattern+r')', text):
        labels.add(ast.literal_eval(match.group(1)))
labels.add("Duplicate session")
labels.update(event_labels)
print(f"Event log audit: {len(event_labels)} literal templates")

# Technical names and literal control sequences are intentionally retained.
technical = {"KiTTY-ng", "SSH", "Telnet", "Rlogin", "SUPDUP", "SSH-1", "SSH-2",
             "VT100+", "VT400", "Xterm R6", "Xterm 216+", "SCO", "Linux",
             "ESC[n~", "Control-H", "Control-? (127)", "Control-C", "IPv4", "IPv6",
             "ANSI", "OEM", "ClearType", "MIT-Magic-Cookie-1", "XDM-Authorization-1"}
missing = sorted(s for s in labels if s and s not in catalog and s not in technical)
format_pattern = re.compile(r'%(?:[-+ #0]*\d*(?:\.\d+|\.\*)?|\*)(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp%]')
contexts=json.loads((root / "locale/ru-contexts.json").read_text(encoding="utf8"))
all_pairs=list(catalog.items())+[pair for entries in contexts.values() for pair in entries.items()]
bad = [(en, ru) for en, ru in all_pairs if format_pattern.findall(en) != format_pattern.findall(ru)]
print(f"Configuration UI: {len(labels)} literals, {len(missing)} missing translations")
for text in missing: print(json.dumps(text, ensure_ascii=False))
for en, ru in bad: print("FORMAT MISMATCH:", repr(en), repr(ru))
raise SystemExit(bool(missing or bad))
