"""Check GUI resource/control labels and command-line help coverage."""
import ast
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / "locale/ru.json").read_text(encoding="utf-8"))
literal = r'"(?:[^"\\]|\\.)*"'
joined = literal + r'(?:\s*' + literal + r')*'

def value(text):
    return "".join(ast.literal_eval(part) for part in re.findall(literal, text)).replace("&", "")

labels = set()
for name in ("windows/puttygen.c", "windows/pageant.c"):
    text = re.sub(r'/\*.*?\*/', '', (root / name).read_text(), flags=re.S)
    for call in re.finditer(r'\b(?:AppendMenu|beginbox|statictext|staticedit|staticpassedit|staticbtn|static2btn|staticddl|bigeditctrl|radioline)\s*\((.*?)\);', text, re.S):
        labels.update(value(m[0]) for m in re.finditer(joined, call[1]))
for name in ("windows/puttygen.rc", "windows/pageant.rc"):
    for match in re.finditer(r'(?:CAPTION|(?:C|L|R)TEXT|(?:DEF)?PUSHBUTTON)\s+(' + literal + ')', (root / name).read_text()):
        labels.add(value(match[1]))
for name in ("windows/plink.c", "pscp.c", "psftp.c"):
    text = (root / name).read_text()
    start = text.index("static void usage(void)")
    end = text.index("static void version(void)", start) if "static void version(void)" in text[start:] else text.index("void version(void)", start)
    for match in re.finditer(r'\bprintf\(\s*(' + joined + ')', text[start:end]):
        text_value = value(match[1])
        if re.search(r'[A-Za-z]{3}', text_value) and not re.fullmatch(r'\s*-[\w-]+(?:\s+[-\[\]:\w-]+)*\s*', text_value):
            labels.add(text_value)
technical = {"", "RSA", "DSA", "ECDSA", "EdDSA", "SSH-1 (RSA)",
             "  -sanitise-stderr, -sanitise-stdout, -no-sanitise-stderr, -no-sanitise-stdout\n"}
missing = sorted(labels - set(catalog) - technical)
for text in missing:
    print("MISSING:", json.dumps(text, ensure_ascii=False))
print(f"Utility UI/help: {len(labels)} strings, {len(missing)} missing")
raise SystemExit(bool(missing))
